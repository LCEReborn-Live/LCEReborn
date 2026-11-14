#!/usr/bin/env python3
"""Lay out the third-party sources the way tools/rpx/build_redhills.py --third-party reads them.

    python tools/build/layout.py <deps> <destination>

<deps> is the directory tools/build/deps.py fills. The destination receives:

  zlib/, libpng/, boost/ (holding boost/), stb/   the pinned releases, unchanged
  curl/include/curl/                              curl 7.38.0's headers with the Wii U curlbuild.h
  libpng-wiiu/                                    the Wii U png.h and pngwrite.c (overrides/libpng),
                                                  libpng's private headers beside them
                                                  and libpng's prebuilt pnglibconf.h
  rapidxml/                                       RapidXml 1.13 (third_party/rapidxml)

Unchanged trees are linked where the file system allows it and copied otherwise.
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OVERRIDES = os.path.join(HERE, "overrides")
RAPIDXML = os.path.join(HERE, "third_party", "rapidxml")
UNCHANGED = ("zlib", "libpng", "boost", "stb")
# Copied beside the Wii U png.h so that pngwrite.c's quoted includes find it rather than upstream's.
PNG_PRIVATE_HEADERS = ("pngconf.h", "pngdebug.h", "pnginfo.h", "pngpriv.h", "pngstruct.h")


def link_or_copy(source, target):
    try:
        # Keep the layout relocatable: Linux builds mount the repository at a
        # different path inside Docker, so absolute host paths become broken.
        relative_source = os.path.relpath(source, os.path.dirname(target))
        os.symlink(relative_source, target, target_is_directory=True)
    except (OSError, NotImplementedError):
        shutil.copytree(source, target)


def copy_files(sources, directory):
    os.makedirs(directory, exist_ok=True)
    for source in sources:
        shutil.copyfile(source, os.path.join(directory, os.path.basename(source)))


def lay_out(deps, destination):
    deps = os.path.abspath(deps)
    staging = destination + ".partial"
    if os.path.lexists(staging):
        shutil.rmtree(staging)
    os.makedirs(staging)
    for name in UNCHANGED:
        source = os.path.join(deps, name)
        if not os.path.isdir(source):
            raise SystemExit("%s is missing from %s; run tools/build/deps.py first" % (name, deps))
        link_or_copy(source, os.path.join(staging, name))
    curl = os.path.join(staging, "curl", "include", "curl")
    shutil.copytree(os.path.join(deps, "curl", "include", "curl"), curl)
    copy_files([os.path.join(OVERRIDES, "curl", "curl", "curlbuild.h")], curl)
    copy_files([os.path.join(OVERRIDES, "libpng", "png.h"), os.path.join(OVERRIDES, "libpng", "pngwrite.c")]
               + [os.path.join(deps, "libpng", name) for name in PNG_PRIVATE_HEADERS],
               os.path.join(staging, "libpng-wiiu"))
    shutil.copyfile(os.path.join(deps, "libpng", "scripts", "pnglibconf.h.prebuilt"),
                    os.path.join(staging, "libpng-wiiu", "pnglibconf.h"))
    copy_files([os.path.join(RAPIDXML, name) for name in sorted(os.listdir(RAPIDXML))],
               os.path.join(staging, "rapidxml"))
    if os.path.lexists(destination):
        shutil.rmtree(destination)
    os.replace(staging, destination)


def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__)
    lay_out(argv[0], argv[1])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
