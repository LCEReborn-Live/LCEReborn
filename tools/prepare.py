#!/usr/bin/env python3
"""Prepare the game files the Wii U build needs from your own dumps.

    python tools/prepare.py --base <base game dump> --update <update dump>

Each dump is an extracted title folder with code/, content/ and meta/, as
produced by common Wii U dumping and decryption tools. The update must be
version 688, the version this source reconstructs.

Outputs, all ignored by git:
  source/platform/wiiu/shaders/   the compiled GX2 shaders, generated from the
                                  update's Minecraft.Client.rpx
  assets/                         Common/, music/ and WiiU/: the base game's
                                  content with the update's files copied over it
  assets/.prepared.json           what was prepared, from which inputs

Options:
  --shaders-only   regenerate the shaders from --update and leave assets alone
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import time

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, os.path.join(TOOLS, "gamedata"))
import rpl  # noqa: E402
import shaders  # noqa: E402

TITLE_ID = "00050000101D9D00"
CONTENT_FOLDERS = ("Common", "music", "WiiU")
SHADER_DIR = os.path.join(ROOT, "source", "platform", "wiiu", "shaders")
ASSETS_DIR = os.path.join(ROOT, "assets")
MANIFEST = ".prepared.json"


class PrepareError(Exception):
    """An input dump is missing, malformed or the wrong version."""


def read_meta(dump):
    path = os.path.join(dump, "meta", "meta.xml")
    if not os.path.isfile(path):
        raise PrepareError("%s has no meta/meta.xml; point at an extracted title folder" % dump)
    with open(path, "rb") as handle:
        text = handle.read().decode("utf-8", "replace")
    title_id = re.search(r"<title_id[^>]*>\s*([0-9A-Fa-f]{16})\s*<", text)
    version = re.search(r"<title_version[^>]*>\s*(\d+)\s*<", text)
    if not title_id or not version:
        raise PrepareError("%s has no title_id or title_version" % path)
    return title_id.group(1).upper(), int(version.group(1))


def check_dump(dump, role, recipe):
    if not os.path.isdir(dump):
        raise PrepareError("%s dump not found: %s" % (role, dump))
    for folder in ("code", "content", "meta"):
        if not os.path.isdir(os.path.join(dump, folder)):
            raise PrepareError("%s dump %s has no %s/ folder; point at an extracted title folder"
                               % (role, dump, folder))
    title_id, version = read_meta(dump)
    if title_id != TITLE_ID:
        raise PrepareError("%s dump %s is title %s, not Minecraft: Wii U Edition (%s)"
                           % (role, dump, title_id, TITLE_ID))
    if role == "update" and version != recipe["update_version"]:
        raise PrepareError("update dump %s is version %d; this source needs version %d"
                           % (dump, version, recipe["update_version"]))
    if role == "base" and version == recipe["update_version"]:
        raise PrepareError("%s looks like the update, not the base game; pass it as --update" % dump)
    missing = [f for f in CONTENT_FOLDERS if not os.path.isdir(os.path.join(dump, "content", f))]
    if role == "base" and missing:
        raise PrepareError("base dump %s is missing content folders: %s" % (dump, ", ".join(missing)))
    return version


def prepare_shaders(update, recipe):
    executable = os.path.join(update, *recipe["executable"]["path"].split("/"))
    if not os.path.isfile(executable):
        raise PrepareError("update dump has no %s" % recipe["executable"]["path"])
    return shaders.generate(executable, SHADER_DIR)


def is_original_candy_pack(path):
    names = {"candy texture pack", "candy texture pack.pck"}
    return any(part.casefold() in names for part in str(path).replace("\\", "/").split("/"))


def content_files(dump):
    """Map content-relative path to absolute path for the game folders."""
    files = {}
    for folder in CONTENT_FOLDERS:
        top = os.path.join(dump, "content", folder)
        if not os.path.isdir(top):
            continue
        for directory, folders, names in os.walk(top):
            folders[:] = [folder for folder in folders if not is_original_candy_pack(folder)]
            for name in names:
                if is_original_candy_pack(name):
                    continue
                full = os.path.join(directory, name)
                files[os.path.relpath(full, os.path.join(dump, "content")).replace(os.sep, "/")] = full
    return files


def prepare_assets(base, update):
    merged = content_files(base)
    base_count = len(merged)
    update_files = content_files(update)
    overridden = sum(1 for path in update_files if path in merged)
    merged.update(update_files)
    staging = ASSETS_DIR + ".partial"
    if os.path.exists(staging):
        shutil.rmtree(staging)
    total = 0
    started = time.time()
    for index, (relative, source) in enumerate(sorted(merged.items()), 1):
        target = os.path.join(staging, *relative.split("/"))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(source, target)
        total += os.path.getsize(target)
        if index % 100 == 0 or index == len(merged):
            print("  copied %d of %d files (%.1f GB)" % (index, len(merged), total / 1e9), flush=True)
    if os.path.exists(ASSETS_DIR):
        shutil.rmtree(ASSETS_DIR)
    os.replace(staging, ASSETS_DIR)
    return {"files": len(merged), "bytes": total, "from_base": base_count - overridden,
            "from_update": len(update_files), "update_overrides": overridden,
            "seconds": round(time.time() - started, 1)}


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", help="extracted base game dump (folder with code/, content/, meta/)")
    parser.add_argument("--update", required=True, help="extracted update v688 dump (folder with code/, content/, meta/)")
    parser.add_argument("--shaders-only", action="store_true", help="regenerate the shaders and leave assets alone")
    args = parser.parse_args(argv)
    if not args.shaders_only and not args.base:
        parser.error("--base is required unless --shaders-only is given")

    recipe = shaders.load_recipe()
    try:
        update_version = check_dump(args.update, "update", recipe)
        base_version = check_dump(args.base, "base", recipe) if not args.shaders_only else None
        print("generating shaders into %s" % os.path.relpath(SHADER_DIR, ROOT))
        generated = prepare_shaders(args.update, recipe)
        for name, digest in sorted(generated.items()):
            print("  %s  %s" % (digest[:16], name))
        if args.shaders_only:
            return 0
        print("copying base content, then update content over it, into %s" % os.path.relpath(ASSETS_DIR, ROOT))
        assets = prepare_assets(args.base, args.update)
    except (PrepareError, shaders.GenerationError, rpl.RplError, OSError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 1

    manifest = {
        "prepared_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "title_id": TITLE_ID,
        "base_version": base_version,
        "update_version": update_version,
        "executable_sha256": recipe["executable"]["sha256"],
        "shaders": generated,
        "assets": assets,
    }
    with open(os.path.join(ASSETS_DIR, MANIFEST), "w", encoding="utf-8", newline="\n") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")
    print("assets: %d files, %.2f GB (%d from the base game, %d from the update, %d of them overriding the base)"
          % (assets["files"], assets["bytes"] / 1e9, assets["from_base"], assets["from_update"], assets["update_overrides"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
