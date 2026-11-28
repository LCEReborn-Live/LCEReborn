#include "game/core/NbtIo.hpp"

#include "game/core/BufferedOutputStream.hpp"
#include "game/core/ByteArrayInputStream.hpp"
#include "game/core/ByteArrayOutputStream.hpp"
#include "game/core/DataInput.hpp"
#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutput.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/core/InputStream.hpp"
#include "game/core/OutputStream.hpp"
#include "game/core/System.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/EndTag.hpp"
#include "game/data/Tag.hpp"
#include "game/world/biome/arrayWithLength.hpp"
#include "game/core/MemSect.hpp"

#include <string>

namespace
{
// The normal reader handles data from game archives. Check external bytes before
// using it so truncated data and invalid list sizes cannot reach Tag::load.
class NbtValidator
{
public:
    explicit NbtValidator(arrayWithLength<unsigned char> bytes) : mBytes(bytes.data), mRemaining(bytes.length) {}

    bool check()
    {
        unsigned int type;
        return readNumber(1, type) && type == Tag::TAG_COMPOUND && skipString()
            && skipPayload(type, 0) && mRemaining == 0;
    }

private:
    bool skip(size_t length)
    {
        if (length > mRemaining)
            return false;
        mBytes += length;
        mRemaining -= length;
        return true;
    }

    bool readNumber(unsigned int width, unsigned int& number)
    {
        if (width > mRemaining)
            return false;
        number = 0;
        for (unsigned int i = 0; i < width; ++i)
            number = (number << 8) | mBytes[i];
        return skip(width);
    }

    bool skipString()
    {
        unsigned int size;
        if (!readNumber(2, size) || size > mRemaining)
            return false;
        for (unsigned int i = 0; i < size;)
        {
            const auto first = mBytes[i++];
            if (first < 0x80)
                continue;
            unsigned int continuation = first < 0xe0 ? 1 : 2;
            if (first < 0xc0 || first >= 0xf0 || continuation > size - i)
                return false;
            while (continuation-- != 0)
                if ((mBytes[i++] & 0xc0) != 0x80)
                    return false;
        }
        return skip(size);
    }

    bool skipPayload(unsigned int type, int depth)
    {
        if (depth > 64)
            return false;

        switch (type)
        {
        case Tag::TAG_BYTE: return skip(1);
        case Tag::TAG_SHORT: return skip(2);
        case Tag::TAG_INT:
        case Tag::TAG_FLOAT: return skip(4);
        case Tag::TAG_LONG:
        case Tag::TAG_DOUBLE: return skip(8);
        case Tag::TAG_STRING: return skipString();
        case Tag::TAG_BYTE_ARRAY:
        case Tag::TAG_INT_ARRAY:
        {
            unsigned int count;
            const size_t stride = type == Tag::TAG_BYTE_ARRAY ? 1 : 4;
            return readNumber(4, count) && count <= mRemaining / stride && skip(count * stride);
        }
        case Tag::TAG_LIST:
        {
            unsigned int childType;
            unsigned int count;
            if (!readNumber(1, childType) || !readNumber(4, count) || childType > Tag::TAG_INT_ARRAY
                || (childType == Tag::TAG_END && count != 0) || count > mRemaining || count > 1000000)
                return false;
            for (unsigned int i = 0; i < count; ++i)
                if (!skipPayload(childType, depth + 1))
                    return false;
            return true;
        }
        case Tag::TAG_COMPOUND:
        {
            unsigned int childType;
            while (readNumber(1, childType))
            {
                if (childType == Tag::TAG_END)
                    return true;
                if (!skipString() || !skipPayload(childType, depth + 1))
                    return false;
            }
            return false;
        }
        default: return false;
        }
    }

    const unsigned char* mBytes;
    size_t mRemaining;
};
}

Tag* NbtIo::readUnnamedTag(DataInput* input, int depth)
{
    unsigned char type = input->readByte();

    if (type == 0)
    {
        return new EndTag();
    }

    if (type == 255)
    {
        return new EndTag();
    }

    input->readUTF();

    Tag* tag = Tag::newTag(type);

    tag->load(input, depth);

    return tag;
}

CompoundTag* NbtIo::read(DataInput* input)
{
    Tag* tag = readUnnamedTag(input, 0);

    if (tag->getId() == Tag::TAG_COMPOUND)
    {
        return (CompoundTag*)tag;
    }

    delete tag;

    return NULL;
}

CompoundTag* NbtIo::readCompressed(InputStream* stream)
{
    MemSect(26);

    DataInputStream input(stream);

    CompoundTag* tag = read(&input);

    input.close();

    MemSect(0);

    return tag;
}

CompoundTag* NbtIo::readChecked(arrayWithLength<unsigned char> bytes)
{
    return NbtValidator(bytes).check() ? decompress(bytes) : nullptr;
}

void NbtIo::writeUnnamedTag(Tag* tag, DataOutput* output)
{
    output->writeByte(tag->getId());

    if (tag->getId() == Tag::TAG_END)
        return;

    output->writeUTF(L"");

    tag->write(output);
}

void NbtIo::write(CompoundTag* tag, DataOutput* output)
{
    writeUnnamedTag(tag, output);
}

void NbtIo::writeCompressed(CompoundTag* tag, OutputStream* stream)
{
    BufferedOutputStream bufferedStream(stream, 1024);
    DataOutputStream output(&bufferedStream);

    write(tag, &output);

    output.close();
}

CompoundTag* NbtIo::decompress(arrayWithLength<unsigned char> data)
{
    ByteArrayInputStream byteStream(data);
    DataInputStream input((InputStream*)&byteStream);

    CompoundTag* tag = read(&input);

    byteStream.reset();

    input.close();

    return tag;
}

arrayWithLength<unsigned char> NbtIo::compress(CompoundTag* tag)
{
    ByteArrayOutputStream byteStream;
    DataOutputStream output(&byteStream);

    write(tag, &output);

    arrayWithLength<unsigned char> bytes(byteStream.mCount, true);

    System::arraycopy(byteStream.mBuffer, 0, &bytes, 0, byteStream.mCount);

    output.close();

    return bytes;
}
