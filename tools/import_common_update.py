"""Import a local PS4 Common update into prepared game assets, with backups.

The original archive is retained. Its entries are also merged into the runtime
archive, retaining the existing platform UI and missing localization keys.
Requires a client with extended atlas and version 2 model support.
"""

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import struct


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, count):
        if count < 0 or self.pos + count > len(self.data):
            raise ValueError("truncated resource")
        result = self.data[self.pos:self.pos + count]
        self.pos += count
        return result

    def integer(self):
        return struct.unpack(">I", self.take(4))[0]

    def string(self):
        # Preserve modified UTF-8 verbatim, including encoded surrogate pairs.
        return self.take(struct.unpack(">H", self.take(2))[0])

    def end(self):
        if self.pos != len(self.data):
            raise ValueError("unexpected resource data")


def integer(value):
    return struct.pack(">I", value)


def string(value):
    return struct.pack(">H", len(value)) + value


def read_archive(data):
    reader = Reader(data)
    entries = {}
    for _ in range(reader.integer()):
        name, offset, size = reader.string(), reader.integer(), reader.integer()
        if offset + size > len(data):
            raise ValueError("invalid archive entry")
        payload = data[offset:offset + size]
        if name in entries and entries[name] != payload:
            raise ValueError("conflicting duplicate archive entry")
        entries[name] = payload
    return entries


def write_archive(entries):
    offset = 4 + sum(2 + len(name) + 8 for name in entries)
    header, payload = [integer(len(entries))], []
    for name, data in entries.items():
        header.append(string(name) + integer(offset) + integer(len(data)))
        payload.append(data)
        offset += len(data)
    return b"".join(header + payload)


def read_languages(data):
    reader = Reader(data)
    version, count = reader.integer(), reader.integer()
    if version != 2 or reader.take(1) != b"\x01":
        raise ValueError("expected a version 2 language table")
    ids = [reader.integer() for _ in range(reader.integer())]
    entries = [(reader.string(), reader.integer()) for _ in range(count)]
    languages = {}
    for name, size in entries:
        part = Reader(reader.take(size))
        if part.integer() != 2 or part.take(1) != b"\x01" or part.string() != name:
            raise ValueError("invalid language section")
        if part.integer() != len(ids):
            raise ValueError("language key count mismatch")
        languages[name] = {key: part.string() for key in ids}
        part.end()
    reader.end()
    return languages


def merge_languages(old_data, new_data):
    old, new = read_languages(old_data), read_languages(new_data)
    aliases = {
        b"cs-CZ": b"cs-CS", b"da-DK": b"da-DA", b"el-GR": b"el-EL",
        b"en-GB": b"en-EN", b"es-MX": b"la-LAS", b"nb-NO": b"no-NO",
        b"sv-SE": b"sv-SV", b"zh-HANT": b"zh-CHT",
    }
    fallback = dict(old[b"en-EN"])
    fallback.update(new[b"en-EN"])
    ids = sorted(fallback)
    sections = {}
    for name in dict.fromkeys(list(old) + list(new)):
        values = dict(fallback)
        values.update(old.get(name, {}))
        values.update(new.get(aliases.get(name, name), {}))
        sections[name] = (integer(2) + b"\x01" + string(name) + integer(len(ids))
                          + b"".join(string(values[key]) for key in ids))
    return (integer(2) + integer(len(sections)) + b"\x01" + integer(len(ids))
            + b"".join(integer(key) for key in ids)
            + b"".join(string(name) + integer(len(data)) for name, data in sections.items())
            + b"".join(sections.values()))


def validate_models(data):
    reader = Reader(data)
    version, count = reader.integer(), reader.integer()
    if version not in (0, 1, 2) or count > 4096:
        raise ValueError("unsupported model table")
    names = []
    for _ in range(count):
        names.append(reader.string().decode("utf-8"))
        reader.take(8)
        for _ in range(reader.integer()):
            reader.string()
            if version >= 2:
                reader.string()
            reader.take(36 if version > 0 else 24)
            reader.take(reader.integer() * 37)
    reader.end()
    return names


def import_update(source, assets, backup):
    source, assets, backup = source.resolve(), assets.resolve(), backup.resolve()
    common = assets / "Common"
    if source == common or source in common.parents or common in source.parents:
        raise ValueError("source and destination must be separate")
    if backup == assets or assets in backup.parents or backup in assets.parents:
        raise ValueError("backup must be outside the assets directory")
    source_files = sorted(p for p in source.rglob("*") if p.is_file())
    model_names = validate_models((source / "res/TitleUpdate/res/models.bin").read_bytes())
    old_archive = read_archive((common / "Media/MediaWiiU.arc").read_bytes())
    new_archive = read_archive((source / "Media/MediaOrbis.arc").read_bytes())
    merged = dict(old_archive)
    merged.update(new_archive)
    merged[b"languages.loc"] = merge_languages(old_archive[b"languages.loc"], new_archive[b"languages.loc"])
    archive_data = write_archive(merged)
    if read_archive(archive_data) != merged:
        raise ValueError("merged archive verification failed")
    # Validate the generated localization before changing any existing files.
    languages = read_languages(merged[b"languages.loc"])
    backup.mkdir(parents=True, exist_ok=False)
    records = []

    def install(relative, data):
        target = common / relative
        old_data = target.read_bytes() if target.is_file() else None
        status = "new" if old_data is None else "same" if old_data == data else "changed"
        if status == "changed":
            saved = backup / "Common" / relative
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, saved)
        if status != "same":
            target.parent.mkdir(parents=True, exist_ok=True)
            temp = target.with_name(target.name + ".importing")
            temp.write_bytes(data)
            temp.replace(target)
        digest = hashlib.sha256(data).hexdigest()
        if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
            raise ValueError("installed checksum mismatch: " + str(relative))
        records.append({"path": relative.as_posix(), "status": status, "sha256": digest})

    for path in source_files:
        if path.is_symlink():
            raise ValueError("source contains a symlink: " + str(path))
        install(path.relative_to(source), path.read_bytes())
    install(Path("Media/MediaWiiU.arc"), archive_data)
    manifest = {
        "source": str(source), "imported_at": datetime.now(timezone.utc).isoformat(),
        "backup": str(backup), "files": records, "models": model_names,
        "archive_entries": len(merged), "languages": len(languages),
        "note": "Resources only; new entities, blocks and generation need game code.",
    }
    manifest_text = json.dumps(manifest, indent=2) + "\n"
    marker = Path("res/TitleUpdate/common-update.json")
    install(marker, manifest_text.encode())
    (backup / "import.json").write_text(manifest_text, encoding="utf-8")
    print(json.dumps({"files": len(source_files), "changes": dict(Counter(r["status"] for r in records)),
                      "models": len(model_names), "archive_entries": len(merged),
                      "backup": str(backup)}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--common", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--backup-dir", type=Path, required=True)
    args = parser.parse_args()
    import_update(args.common, args.assets, args.backup_dir)


if __name__ == "__main__":
    main()
