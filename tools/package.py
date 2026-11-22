#!/usr/bin/env python3
"""Package the built RPX as a Wii U homebrew bundle that Cemu and Aroma run.

    python tools/package.py --update <update dump>
    python tools/package.py --base <base dump> --update <update dump> --rpx <downloaded RPX>

By default, this uses the output of tools/prepare.py and tools/build.py.
Pass --base and --rpx to package downloaded code directly with your dumps.
This writes build/LCEReborn.wuhb unless you pass --output:

  code/root.rpx   Minecraft.Client.rpx from tools/build.py
  meta/           the name the menu shows, with the menu icon and boot screens
                  from the update dump's meta/
  content/        assets/, the game content tools/prepare.py made from your dumps

The bundle is its own title with its own saves; it needs no Wii U game installed.
Open it in Cemu with tools/launch_cemu.py, or copy it to
wiiu/apps/ on the SD card for Aroma.

Use --code-only to package the RPX for use with prepared content in
wiiu/LCEReborn/content on an SD card or Cemu's virtual SD card.
"""
import argparse
import gzip
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


def content_sources(base, update):
    recipe = shaders.load_recipe()
    prepare.check_dump(update, "update", recipe)
    for name in META_IMAGES:
        if not os.path.isfile(os.path.join(update, "meta", name + ".tga")):
            raise PackageError("update dump %s has no meta/%s.tga" % (update, name))
    if base is not None:
        prepare.check_dump(base, "base", recipe)
        files = prepare.content_files(base)
        files.update(prepare.content_files(update))
        return files

    manifest = os.path.join(prepare.ASSETS_DIR, prepare.MANIFEST)
    if not os.path.isfile(manifest):
        raise PackageError("assets/ has not been prepared. Run tools/prepare.py with your base game "
                           "and update dumps first, or pass --base.")
    return prepared_content()


def read_file(path):
    with open(path, "rb") as handle:
        return handle.read()


def prepared_content():
    files = {}
    for directory, _, names in os.walk(prepare.ASSETS_DIR):
        for name in names:
            path = os.path.join(directory, name)
            relative = os.path.relpath(path, prepare.ASSETS_DIR).replace(os.sep, "/")
            if relative != prepare.MANIFEST:
                files[relative] = path
    return files


def build_bundle(rpx, update, files):
    root = wuhb.Dir("")
    code = root.add(wuhb.Dir("code"))
    code.add(wuhb.file_bytes("root.rpx", read_file(rpx)))

    meta = root.add(wuhb.Dir("meta"))
    meta.add(wuhb.file_bytes("meta.ini", wuhb.meta_ini(LONG_NAME, SHORT_NAME, AUTHOR)))
    content = root.add(wuhb.Dir("content"))
    if update is not None:
        for name in META_IMAGES:
            image = read_file(os.path.join(update, "meta", name + ".tga"))
            meta.add(wuhb.file_bytes(name + ".tga.gz", gzip.compress(image, 9, mtime=0)))
        for relative, path in sorted(files.items()):
            content.add_path(relative, os.path.getsize(path), lambda path=path: read_file(path))
    return root


def package(rpx, update, files, output):
    root = build_bundle(rpx, update, files)
    os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
    partial = output + ".part"

    def progress(done, total):
        if done % 200 == 0 or done == total:
            print("  %d of %d files" % (done, total), flush=True)

    print("writing %s" % output, flush=True)
    try:
        wuhb.write(partial, root, progress)
    except BaseException:
        if os.path.exists(partial):
            os.unlink(partial)
        raise
    os.replace(partial, output)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--update", help="extracted update v688 dump (folder with code/, content/, meta/)")
    mode.add_argument("--code-only", action="store_true",
                      help="use game content from wiiu/LCEReborn/content on the SD card at runtime")
    parser.add_argument("--base", help="package directly from this extracted base game dump and --update")
    parser.add_argument("--rpx", default=RPX, help="local or downloaded Minecraft.Client.rpx")
    parser.add_argument("--output", default=BUNDLE, help="output WUHB path")
    args = parser.parse_args(argv)
    if args.code_only and args.base:
        parser.error("--base requires --update")
    try:
        if not os.path.isfile(args.rpx):
            raise PackageError("%s is missing. Download wiiu-rpx or run python tools/build.py" % args.rpx)
        if os.path.abspath(args.rpx) == os.path.abspath(args.output):
            raise PackageError("the output path must differ from the input RPX")
        files = {} if args.code_only else content_sources(args.base, args.update)
        package(args.rpx, args.update, files, args.output)
    except (PackageError, prepare.PrepareError, wuhb.WuhbError, OSError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
    print("packaged %s (title %016X)" % (args.output, wuhb.title_id(LONG_NAME, SHORT_NAME, AUTHOR)))
    if args.code_only:
        print("game files go in wiiu/LCEReborn/content on the SD card or Cemu's virtual SD card")
    print('run it with: python tools/launch_cemu.py "<path to Cemu>" --bundle "%s"' % args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
