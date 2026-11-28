#include "platform/pc/audio/PcSoundBank.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
const unsigned int BANK_MAGIC = 0x42414E4B;
const int BANK_VERSION = 8;
const int EVENT_VERSION = 4;
const int HEADER_WORDS = 14;
const int SOUND_RECORD_WORDS = 15;
const int SETTINGS_MAGIC = 3;

const int STEP_END = 0;
const int STEP_PLAY = 1;
const int STEP_CACHE = 5;
const int STEP_VERSION = 9;

enum HeaderWord
{
    HEADER_MAGIC = 0,
    HEADER_VERSION = 1,
    HEADER_EVENTS = 5,
    HEADER_ENVIRONMENTS = 6,
    HEADER_PRESETS = 7,
    HEADER_SOUNDS = 8,
    HEADER_SETTINGS = 9,
    HEADER_EVENT_COUNT = 10,
    HEADER_ENVIRONMENT_COUNT = 11,
    HEADER_PRESET_COUNT = 12,
    HEADER_SOUND_COUNT = 13
};

enum SoundWord
{
    SOUND_NAME = 0,
    SOUND_DATA_NAME = 1,
    SOUND_CHANNELS = 3,
    SOUND_SAMPLE_RATE = 5,
    SOUND_DATA_SIZE = 6,
    SOUND_MAX_INSTANCES = 7,
    SOUND_LENGTH_MS = 9,
    SOUND_MARKER_COUNT = 14
};

std::string Key(const char* name)
{
    std::string key(name);
    for (size_t i = 0; i < key.size(); ++i)
    {
        if (key[i] >= 'a' && key[i] <= 'z')
            key[i] = static_cast<char>(key[i] - ('a' - 'A'));
    }
    return key;
}

class FieldReader
{
public:
    explicit FieldReader(const char* text) : m_cursor(text) {}

    bool AtEnd() const { return *m_cursor == '\0'; }

    void SkipLineBreaks()
    {
        while (*m_cursor == '\r' || *m_cursor == '\n')
            ++m_cursor;
    }

    bool Kind(int* kind)
    {
        if (m_cursor[0] == '\0' || m_cursor[1] != ';')
            return false;
        *kind = m_cursor[0] - '0';
        m_cursor += 2;
        return true;
    }

    bool Text(std::string* value)
    {
        const char* end = strchr(m_cursor, ';');
        if (end == NULL)
            return false;
        value->assign(m_cursor, end);
        m_cursor = end + 1;
        return true;
    }

    bool Integer(int base, int* value)
    {
        std::string text;
        if (!Text(&text))
            return false;
        char* end = NULL;
        long parsed = strtol(text.c_str(), &end, base);
        if (text.empty() || *end != '\0')
            return false;
        *value = static_cast<int>(parsed);
        return true;
    }

    bool Float(float* value)
    {
        std::string text;
        if (!Text(&text))
            return false;
        char* end = NULL;
        double parsed = strtod(text.c_str(), &end);
        if (text.empty() || *end != '\0')
            return false;
        *value = static_cast<float>(parsed);
        return true;
    }

private:
    const char* m_cursor;
};

bool ParseChoices(const std::string& text, std::vector<PcSoundBank::Choice>* choices)
{
    size_t position = 0;
    while (position < text.size())
    {
        size_t nameEnd = text.find(':', position);
        if (nameEnd == std::string::npos)
            return false;
        size_t weightEnd = text.find(':', nameEnd + 1);
        if (weightEnd == std::string::npos)
            weightEnd = text.size();
        PcSoundBank::Choice choice;
        choice.sound = text.substr(position, nameEnd - position);
        choice.weight = atoi(text.substr(nameEnd + 1, weightEnd - nameEnd - 1).c_str());
        if (choice.sound.empty() || choice.weight < 0)
            return false;
        choices->push_back(choice);
        position = weightEnd + 1;
    }
    return !choices->empty();
}
}

PcSoundBank::PcSoundBank()
{
}

bool PcSoundBank::Load(std::vector<unsigned char>& bytes, std::string* error)
{
    m_bytes.swap(bytes);
    m_sounds.clear();
    m_events.clear();
    m_skipped.clear();
    m_soundIndex.clear();
    m_eventIndex.clear();

    if (m_bytes.size() < HEADER_WORDS * 4 || Word(HEADER_MAGIC * 4) != static_cast<int>(BANK_MAGIC))
    {
        *error = "not a Miles sound bank";
        return false;
    }
    if (Word(HEADER_VERSION * 4) != BANK_VERSION)
    {
        char message[64];
        snprintf(message, sizeof(message), "bank version %d is not supported", Word(HEADER_VERSION * 4));
        *error = message;
        return false;
    }

    if (Word(HEADER_ENVIRONMENT_COUNT * 4) != 0 || Word(HEADER_PRESET_COUNT * 4) != 0)
        m_skipped.push_back("bank: environment and preset tables are ignored");

    int settings = Word(HEADER_SETTINGS * 4);
    if (settings != 0)
    {
        if (!Contains(settings, 8 * 4))
        {
            *error = "the bank's settings block is outside the file";
            return false;
        }
        if (Word(settings) == SETTINGS_MAGIC)
        {
            for (int i = 1; i < 8; ++i)
            {
                if (Word(settings + i * 4) != 0)
                {
                    m_skipped.push_back("bank: global limits, blends, ramps, LFOs, buses and variables are ignored");
                    break;
                }
            }
        }
    }

    return ReadSounds(error) && ReadEvents(error);
}

int PcSoundBank::Word(size_t offset) const
{
    const unsigned char* bytes = &m_bytes[offset];
    return static_cast<int>((static_cast<unsigned int>(bytes[0]) << 24) | (bytes[1] << 16) | (bytes[2] << 8)
                            | bytes[3]);
}

bool PcSoundBank::Contains(long long offset, long long length) const
{
    return offset >= 0 && length >= 0 && offset + length <= static_cast<long long>(m_bytes.size());
}

const char* PcSoundBank::String(long long offset) const
{
    if (!Contains(offset, 1))
        return NULL;
    const void* end = memchr(&m_bytes[offset], '\0', m_bytes.size() - offset);
    return end != NULL ? reinterpret_cast<const char*>(&m_bytes[offset]) : NULL;
}

bool PcSoundBank::ReadSounds(std::string* error)
{
    int table = Word(HEADER_SOUNDS * 4);
    int count = Word(HEADER_SOUND_COUNT * 4);
    if (count < 0 || !Contains(table, static_cast<long long>(count) * 8))
    {
        *error = "the bank's sound table is outside the file";
        return false;
    }

    m_sounds.reserve(count);
    for (int entry = 0; entry < count; ++entry)
    {
        const char* name = String(Word(table + entry * 8));
        int record = Word(table + entry * 8 + 4);
        if (name == NULL || !Contains(record, SOUND_RECORD_WORDS * 4))
        {
            *error = "a sound record is outside the file";
            return false;
        }

        std::string recordName = name;
        const char* dataName = String(static_cast<long long>(record) + Word(record + SOUND_DATA_NAME * 4));
        unsigned long size = 0;
        unsigned long offset = 0;
        char extension[16] = {0};
        if (dataName == NULL || sscanf(dataName, "*%lu*%lu.%15s", &size, &offset, extension) != 3
            || !Contains(offset, size) || size < 12
            || !((strcmp(extension, "binka") == 0 && memcmp(&m_bytes[offset], "1FCB", 4) == 0)
                || (strcmp(extension, "wav") == 0 && memcmp(&m_bytes[offset], "RIFF", 4) == 0
                    && memcmp(&m_bytes[offset+8], "WAVE", 4) == 0)))
        {
            m_skipped.push_back("sound " + recordName + ": not an embedded Bink Audio or PCM WAVE file");
            continue;
        }
        if (Word(record + SOUND_MARKER_COUNT * 4) != 0)
            m_skipped.push_back("sound " + recordName + ": markers are ignored");

        Sound sound;
        sound.name = recordName;
        sound.data = &m_bytes[offset];
        sound.size = size;
        sound.channels = Word(record + SOUND_CHANNELS * 4);
        sound.sampleRate = Word(record + SOUND_SAMPLE_RATE * 4);
        sound.maxInstances = Word(record + SOUND_MAX_INSTANCES * 4);
        sound.lengthMs = Word(record + SOUND_LENGTH_MS * 4);
        m_soundIndex[Key(sound.name.c_str())] = m_sounds.size();
        m_sounds.push_back(sound);
    }
    return true;
}

bool PcSoundBank::ReadEvents(std::string* error)
{
    int table = Word(HEADER_EVENTS * 4);
    int count = Word(HEADER_EVENT_COUNT * 4);
    if (count < 0 || !Contains(table, static_cast<long long>(count) * 8))
    {
        *error = "the bank's event table is outside the file";
        return false;
    }

    m_events.reserve(count);
    for (int entry = 0; entry < count; ++entry)
    {
        const char* name = String(Word(table + entry * 8));
        const char* text = String(Word(table + entry * 8 + 4));
        if (name == NULL || text == NULL)
        {
            *error = "an event is outside the file";
            return false;
        }

        Event event;
        std::string reason;
        if (!ParseEvent(name, text, &event, &reason))
        {
            m_skipped.push_back("event " + std::string(name) + ": " + reason);
            continue;
        }
        m_eventIndex[Key(name)] = m_events.size();
        m_events.push_back(event);
    }
    return true;
}

bool PcSoundBank::ParseEvent(const std::string& name, const char* text, Event* event, std::string* reason)
{
    FieldReader reader(text);
    event->name = name;

    for (;;)
    {
        reader.SkipLineBreaks();
        if (reader.AtEnd())
            break;

        int kind;
        if (!reader.Kind(&kind))
        {
            *reason = "malformed step";
            return false;
        }
        if (kind == STEP_END)
            break;

        if (kind == STEP_VERSION)
        {
            int version;
            if (!reader.Integer(10, &version) || version != EVENT_VERSION)
            {
                *reason = "unsupported event version";
                return false;
            }
        }
        else if (kind == STEP_PLAY)
        {
            std::string fields[8];
            for (int i = 0; i < 8; ++i)
            {
                if (!reader.Text(&fields[i]))
                {
                    *reason = "malformed play step";
                    return false;
                }
            }
            if (!fields[1].empty() || !fields[3].empty() || !fields[4].empty() || !fields[6].empty()
                || !fields[7].empty())
            {
                *reason = "uses presets, markers, selectors or variables";
                return false;
            }

            Play play;
            int stream;
            int loadOnDemand;
            int presetPersistent;
            std::string positionMarker;
            if (!ParseChoices(fields[0], &play.choices) || !reader.Integer(10, &stream)
                || !reader.Integer(10, &loadOnDemand) || !reader.Integer(10, &presetPersistent)
                || !reader.Integer(16, &play.delayMinimumMs) || !reader.Integer(16, &play.delayMaximumMs)
                || !reader.Integer(16, &play.priority) || !reader.Integer(16, &play.loopCount)
                || !reader.Text(&positionMarker) || !reader.Float(&play.volumeMinimum)
                || !reader.Float(&play.volumeMaximum) || !reader.Float(&play.pitchMinimum)
                || !reader.Float(&play.pitchMaximum) || !reader.Float(&play.fadeTime)
                || !reader.Integer(10, &play.limitPolicy) || !reader.Integer(10, &play.selectionMode))
            {
                *reason = "malformed play step";
                return false;
            }
            if (!positionMarker.empty())
            {
                *reason = "uses markers";
                return false;
            }
            if (play.selectionMode > SELECT_IN_ORDER)
            {
                *reason = "unsupported selection mode";
                return false;
            }
            play.loopCount = static_cast<signed char>(play.loopCount);
            play.stream = stream != 0;
            play.loadOnDemand = loadOnDemand != 0;
            play.completionEvent = fields[2];
            play.labels = fields[5];
            event->plays.push_back(play);
        }
        else if (kind == STEP_CACHE)
        {
            std::string cacheName;
            std::string list;
            if (!reader.Text(&cacheName) || !reader.Text(&list))
            {
                *reason = "malformed cache step";
                return false;
            }
            size_t position = 0;
            while (position < list.size())
            {
                size_t end = list.find(':', position);
                if (end == std::string::npos)
                    end = list.size();
                if (end > position)
                    event->cache.push_back(list.substr(position, end - position));
                position = end + 1;
            }
        }
        else
        {
            char message[48];
            snprintf(message, sizeof(message), "step kind %d is not supported", kind);
            *reason = message;
            return false;
        }
    }

    if (event->plays.empty() && event->cache.empty())
    {
        *reason = "has nothing to play";
        return false;
    }
    return true;
}

const PcSoundBank::Event* PcSoundBank::FindEvent(const char* name) const
{
    std::unordered_map<std::string, size_t>::const_iterator found = m_eventIndex.find(Key(name));
    return found != m_eventIndex.end() ? &m_events[found->second] : NULL;
}

const PcSoundBank::Sound* PcSoundBank::FindSound(const char* name) const
{
    std::unordered_map<std::string, size_t>::const_iterator found = m_soundIndex.find(Key(name));
    return found != m_soundIndex.end() ? &m_sounds[found->second] : NULL;
}
