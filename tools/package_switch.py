#!/usr/bin/env python3
"""Create a complete Switch SD-card ZIP from a client artifact and prepared game files."""

import argparse
from pathlib import Path, PurePosixPath
import shutil
import sys
import tempfile
import zipfile

from prepare import is_original_candy_pack


GAME_FOLDERS = ("Common", "music", "WiiU")
CLIENT_ROOT = "LCEReborn"
OUTPUT_ROOT = f"switch/{CLIENT_ROOT}"
REQUIRED_CLIENT_FILES = {
    "LCEReborn.nro",
    "resources/experimental/shield_base_1_14.png",
    "resources/experimental/shield_icon.png",
}


def client_files(archive):
    files = {}
    for entry in archive.infolist():
        path = PurePosixPath(entry.filename)
        if path.is_absolute() or ".." in path.parts or "\\" in entry.filename or ":" in entry.filename:
            raise ValueError(f"Unsafe client archive path: {entry.filename}")
        if entry.is_dir() or not path.parts or path.parts[0] != CLIENT_ROOT:
            continue
        relative = path.relative_to(CLIENT_ROOT)
        if is_original_candy_pack(relative):
            continue
        if relative.parts and (relative.parts[0] in ("packs", "resources")
                               or relative.as_posix() in ("LCEReborn.nro", "bearssl-LICENSE.txt")):
            name = relative.as_posix()
            if name in files:
                raise ValueError(f"Duplicate client archive entry: {name}")
            files[name] = entry
    missing = REQUIRED_CLIENT_FILES - {name for name, entry in files.items() if entry.file_size > 0}
    if missing:
        raise ValueError("Client archive is missing: " + ", ".join(sorted(missing)))
    return files


def package(client, assets, output):
    if not (assets / "Common/Media/MediaWiiU.arc").is_file():
        raise ValueError("Select the prepared assets folder containing Common/Media/MediaWiiU.arc.")
    for name in GAME_FOLDERS:
        if not (assets / name).is_dir():
            raise ValueError(f"Prepared assets are missing the {name}/ directory.")
    if assets.resolve() in output.resolve().parents:
        raise ValueError("Write the ZIP outside the input assets folder.")
    if output.exists():
        raise ValueError(f"Output already exists: {output}")

    with zipfile.ZipFile(client) as source:
        files = client_files(source)
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".switch-package-", dir=output.parent) as temporary:
            pending = Path(temporary) / "package.zip"
            with zipfile.ZipFile(pending, "w", zipfile.ZIP_DEFLATED, compresslevel=1) as target:
                for name, entry in sorted(files.items()):
                    with source.open(entry) as incoming, target.open(f"{OUTPUT_ROOT}/{name}", "w") as outgoing:
                        shutil.copyfileobj(incoming, outgoing, 1024 * 1024)
                for folder in GAME_FOLDERS:
                    for path in sorted((assets / folder).rglob("*")):
                        relative = path.relative_to(assets)
                        if path.is_file() and not is_original_candy_pack(relative):
                            target.write(path, f"{OUTPUT_ROOT}/assets/{relative.as_posix()}")

            # Reserve the output name exclusively before publishing the completed ZIP.
            with output.open("xb"):
                pass
            try:
                pending.replace(output)
            except OSError:
                output.unlink()
                raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True, help="downloaded nintendo-switch-nro.zip")
    parser.add_argument("--assets", type=Path, required=True, help="prepared assets from the base game and Update 688")
    parser.add_argument("--output", type=Path, required=True, help="path for the complete SD-card ZIP")
    args = parser.parse_args()
    try:
        package(args.client, args.assets, args.output)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, f"error: {error}\n")
    print(f"Open {args.output} and extract its switch/ directory to the root of the SD card.")


if __name__ == "__main__":
    sys.exit(main())
