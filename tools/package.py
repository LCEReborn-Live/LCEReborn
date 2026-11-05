#!/usr/bin/env python3
"""Package the built RPX as a Wii U homebrew bundle that Cemu and Aroma run.

    python tools/package.py --update <update dump>

Run tools/prepare.py and tools/build.py first. This writes build/LCEReborn.wuhb:

  code/root.rpx   Minecraft.Client.rpx from tools/build.py
  meta/           the name the menu shows, with the menu icon and boot screens
                  from the update dump's meta/
  content/        assets/, the game content tools/prepare.py made from your dumps

The bundle is its own title with its own saves; it needs no Wii U game installed.
Open it in Cemu (File > Load, or Cemu -g build/LCEReborn.wuhb), or copy it to
wiiu/apps/ on the SD card for Aroma.
"""
import argparse
import gzip
import json
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
sys.path.insert(0, os.path.join(TOOLS, "gamedata"))
import prepare  # noqa: E402
import shaders  # noqa: E402
import wuhb  # noqa: E402

BUILD_DIR = os.path.join(ROOT, "build")
RPX = os.path.join(BUILD_DIR, "Minecraft.Client.rpx")
BUNDLE = os.path.join(BUILD_DIR, "LCEReborn.wuhb")
# The title ID comes from these names, so the title and its saves change with them.
LONG_NAME = "Legacy Console Edition Reborn"
SHORT_NAME = "LCEReborn"
AUTHOR = "LCEReborn"
META_IMAGES = ("iconTex", "bootTvTex", "bootDrcTex")


class PackageError(Exception):
    """An input is missing or the bundle cannot be written."""


def check_inputs(update):
    if not os.path.isfile(RPX):
        raise PackageError("%s is missing. Build it first: python tools/build.py" % os.path.relpath(RPX, ROOT))
    manifest = os.path.join(prepare.ASSETS_DIR, prepare.MANIFEST)
    if not os.path.isfile(manifest):
        raise PackageError("assets/ has not been prepared. Run tools/prepare.py with your base game "
                           "and update dumps first.")
    try:
        prepare.check_dump(update, "update", shaders.load_recipe())
    except prepare.PrepareError as error:
        raise PackageError(str(error))
    for name in META_IMAGES:
        if not os.path.isfile(os.path.join(update, "meta", name + ".tga")):
            raise PackageError("update dump %s has no meta/%s.tga" % (update, name))
    with open(manifest, encoding="utf-8") as handle:
        return json.load(handle)


def read_file(path):
    with open(path, "rb") as handle:
        return handle.read()


def add_content(content):
    """Add every file in assets/ apart from the manifest, in a fixed order."""
    for directory, folders, names in os.walk(prepare.ASSETS_DIR):
        folders.sort()
        for name in sorted(names):
            path = os.path.join(directory, name)
            relative = os.path.relpath(path, prepare.ASSETS_DIR).replace(os.sep, "/")
            if relative != prepare.MANIFEST:
                content.add_path(relative, os.path.getsize(path), lambda path=path: read_file(path))


def build_bundle(update):
    root = wuhb.Dir("")
    code = root.add(wuhb.Dir("code"))
    code.add(wuhb.file_bytes("root.rpx", read_file(RPX)))

    meta = root.add(wuhb.Dir("meta"))
    meta.add(wuhb.file_bytes("meta.ini", wuhb.meta_ini(LONG_NAME, SHORT_NAME, AUTHOR)))
    for name in META_IMAGES:
        image = read_file(os.path.join(update, "meta", name + ".tga"))
        meta.add(wuhb.file_bytes(name + ".tga.gz", gzip.compress(image, 9, mtime=0)))

    add_content(root.add(wuhb.Dir("content")))
    return root


def package(update):
    root = build_bundle(update)
    partial = BUNDLE + ".part"

    def progress(done, total):
        if done % 200 == 0 or done == total:
            print("  %d of %d files" % (done, total), flush=True)

    print("writing %s" % os.path.relpath(BUNDLE, ROOT), flush=True)
    try:
        wuhb.write(partial, root, progress)
    except BaseException:
        if os.path.exists(partial):
            os.unlink(partial)
        raise
    os.replace(partial, BUNDLE)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--update", required=True, help="extracted update v688 dump (folder with code/, content/, meta/)")
    args = parser.parse_args(argv)
    try:
        check_inputs(args.update)
        package(args.update)
    except (PackageError, wuhb.WuhbError, OSError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
    print("packaged %s (title %016X)" % (os.path.relpath(BUNDLE, ROOT), wuhb.title_id(LONG_NAME, SHORT_NAME, AUTHOR)))
    print("run it with: Cemu -g \"%s\"" % BUNDLE)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
