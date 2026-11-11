#!/usr/bin/env python3
"""Package the PC build to give to players.

    python tools/package_pc.py

Build the game and the launcher first (docs/pc-platform.md). On Windows this
writes build/LCEReborn-pc-<commit>.zip, on Linux
build/LCEReborn-linux-<commit>.tar.gz, holding the game, the launcher, the
dedicated server, the Qt libraries the launcher uses, resources/, a README, the project's license and,
in third_party/, the notices and licenses of every library in the programs.
On Linux it also writes the same files as build/LCEReborn-linux-<commit>.AppImage,
which needs appimagetool and the AppImage runtime (tools/build/Dockerfile.linux
installs both).

Only those files are taken from the build folder, so the game files, saves,
logs and settings that playing from it leaves there stay out.

The package names the commit it was built from as its source, so the tree
must have no uncommitted changes and the commit should be pushed before the
package is given out.

Options:
  --build DIR     the PC build folder (default build/pc)
  --allow-dirty   package a tree with uncommitted changes, for testing; the
                  package is named ...-dirty and must not be given out, since
                  its source is not published
  --no-appimage   on Linux, write only the tar.gz
"""
import argparse
import datetime
import glob
import io
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import textwrap
import zipfile

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
DEFAULT_BUILD = os.path.join(ROOT, "build", "pc")
OUTPUT_DIR = os.path.join(ROOT, "build")
THIRD_PARTY = os.path.join(ROOT, "build", "third_party")
LICENSE_TEXTS = os.path.join(TOOLS, "package", "licenses")
NOTICE_TEXTS = os.path.join(TOOLS, "package", "notices")
# The licenses of the AppImage runtime and the libraries in it (package/notices/AppImageRuntime.txt).
APPIMAGE_RUNTIME_LICENSES = ("MIT", "LGPL-2.1-only", "BSD-2-Clause", "BSD-3-Clause", "GPL-2.0-only")
TOP = "LCEReborn"

LINUX = sys.platform.startswith("linux")
if LINUX:
    SYSTEM = "linux"
    PROGRAMS = ("LCEReborn", "LCERebornLauncher", "LCERebornServer")
    QT_PLUGIN_DIRS = ("imageformats", "networkinformation", "platforminputcontexts", "platformthemes",
                      "tls", "wayland-decoration-client", "wayland-graphics-integration-client",
                      "wayland-shell-integration", "xcbglintegrations")
    QT_PLATFORMS = ("libqxcb.so", "libqwayland.so")
    ALLOWED_NAME = re.compile(r"(^[^.]+$|\.so(\.\d+)*$|\.(png|ico|txt|mp3)$)")
else:
    SYSTEM = "pc"
    PROGRAMS = ("LCEReborn.exe", "LCERebornLauncher.exe", "LCERebornServer.exe")
    QT_PLUGIN_DIRS = ("generic", "imageformats", "networkinformation", "platforms", "styles", "tls")
    ALLOWED_NAME = re.compile(r"\.(exe|dll|png|ico|txt|js|json|mp3)$", re.I)
REDIST_URL = "https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist"


class PackageError(Exception):
    """An input is missing or the package cannot be made."""


def git(*args):
    result = subprocess.run(("git", "-C", ROOT) + args, capture_output=True, text=True)
    if result.returncode != 0:
        raise PackageError("git %s failed: %s" % (" ".join(args), result.stderr.strip()))
    return result.stdout.strip()


def source_info(allow_dirty):
    commit = git("rev-parse", "HEAD")
    dirty = git("status", "--porcelain", "--untracked-files=no") != ""
    if dirty and not allow_dirty:
        raise PackageError("The tree has uncommitted changes, so no published source matches the build. "
                           "Commit them first, or pass --allow-dirty for a test package.")
    if not git("branch", "-r", "--contains", commit):
        print("warning: %s is not on any remote branch; push it before giving out the package" % commit[:12])
    url = git("remote", "get-url", "origin")
    match = re.match(r"(?:https://|git@)github\.com[/:](.+?)(?:\.git)?$", url)
    if not match:
        raise PackageError("origin (%s) is not a GitHub repository, so the source link cannot be written" % url)
    timestamp = int(git("show", "-s", "--format=%ct", commit))
    return {
        "commit": commit,
        "dirty": dirty,
        "repository": "https://github.com/" + match.group(1),
        "date": datetime.datetime.fromtimestamp(timestamp, datetime.timezone.utc),
    }


def read(path):
    if not os.path.isfile(path):
        raise PackageError("%s is missing" % os.path.relpath(path, ROOT))
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def block(text, start, end, path):
    match = re.search(re.escape(start) + r".*?" + re.escape(end), text, re.S)
    if not match:
        raise PackageError("The license notice in %s was not found" % os.path.relpath(path, ROOT))
    return match.group(0)


def uncomment(text):
    lines = [re.sub(r"^\s*(//|\*|/\*)", "", line).rstrip() for line in text.splitlines()]
    indents = [len(line) - len(line.lstrip()) for line in lines[1:] if line.strip()]
    indent = min(indents) if indents else 0
    return "\n".join([lines[0].strip()] + [line[indent:] for line in lines[1:]]).strip()


def cache_value(build, name):
    text = read(os.path.join(build, "CMakeCache.txt"))
    match = re.search(r"^%s:[A-Z]+=(.*)$" % re.escape(name), text, re.M)
    if not match:
        raise PackageError("%s is not set in %s/CMakeCache.txt" % (name, os.path.relpath(build, ROOT)))
    return match.group(1).strip()


def qt_prefix(build):
    return os.path.normpath(os.path.join(cache_value(build, "Qt6_DIR"), "..", "..", ".."))


def windows_qt_files(build):
    """The Qt files windeployqt put in the build folder, with their names in Qt's SBOM."""
    files = []
    qt_libraries = sorted(glob.glob(os.path.join(build, "Qt6*.dll")))
    if not qt_libraries:
        raise PackageError("No Qt libraries in %s. Build the launcher, which deploys them." % build)
    for path in qt_libraries:
        files.append((path, os.path.basename(path), "./bin/" + os.path.basename(path)))
    for folder in QT_PLUGIN_DIRS:
        for path in sorted(glob.glob(os.path.join(build, folder, "*.dll"))):
            arcname = folder + "/" + os.path.basename(path)
            files.append((path, arcname, "./plugins/" + arcname))
    return files


def linked_libraries(binary, library_dir):
    """The libraries from library_dir that binary loads, by the name it asks for."""
    environment = dict(os.environ, LD_LIBRARY_PATH=library_dir)
    result = subprocess.run(["ldd", binary], capture_output=True, text=True, env=environment)
    if result.returncode != 0:
        raise PackageError("ldd %s failed: %s" % (binary, result.stderr.strip()))
    found = []
    for name, path in re.findall(r"^\s*(\S+) => (/\S+)", result.stdout, re.M):
        if os.path.dirname(os.path.realpath(path)) == os.path.realpath(library_dir):
            found.append((name, os.path.realpath(path)))
    return found


def linux_qt_files(build):
    """The Qt plugins the launcher needs and every library from Qt's lib/ that it and they load, under the
    names they are loaded by. Qt's own files are paired with their names in Qt's SBOM; the rest (ICU) with None."""
    prefix = qt_prefix(build)
    library_dir = os.path.join(prefix, "lib")
    files = []
    for folder in QT_PLUGIN_DIRS:
        for path in sorted(glob.glob(os.path.join(prefix, "plugins", folder, "*.so"))):
            arcname = "plugins/%s/%s" % (folder, os.path.basename(path))
            files.append((path, arcname, "./" + arcname))
    for name in QT_PLATFORMS:
        path = os.path.join(prefix, "plugins", "platforms", name)
        if not os.path.isfile(path):
            raise PackageError("%s is missing from Qt" % os.path.relpath(path, ROOT))
        files.append((path, "plugins/platforms/" + name, "./plugins/platforms/" + name))

    loaded = {}
    pending = [os.path.join(build, PROGRAMS[1])] + [path for path, _, _ in files]
    while pending:
        for name, path in linked_libraries(pending.pop(0), library_dir):
            if name not in loaded:
                loaded[name] = path
                pending.append(path)
    for name in sorted(loaded):
        path = loaded[name]
        sbom_name = None if name.startswith("libicu") else "./lib/" + os.path.basename(path)
        files.append((path, "lib/" + name, sbom_name))
    return files


def program_files(build):
    """The files to package, as (path, name in the package), and the Qt files among them as
    (name in the package, name in Qt's SBOM or None)."""
    files = []
    for name in PROGRAMS:
        path = os.path.join(build, name)
        if not os.path.isfile(path):
            raise PackageError("%s is missing. Build the game and the launcher first." % os.path.relpath(path, ROOT))
        files.append((path, name))
    if not LINUX:
        files.append((os.path.join(build, PROGRAMS[0]), "LCEReborn-Live.exe"))
    qt_files = linux_qt_files(build) if LINUX else windows_qt_files(build)
    for path, arcname, _ in qt_files:
        files.append((path, arcname))
    resources = os.path.join(ROOT, "resources", "pc")
    for folder, _, names in os.walk(resources):
        for name in sorted(names):
            path = os.path.join(folder, name)
            files.append((path, "resources/" + os.path.relpath(path, resources).replace(os.sep, "/")))
    if not LINUX:
        live = os.path.join(ROOT, "live")
        for folder, _, names in os.walk(live):
            for name in sorted(names):
                if os.path.splitext(name)[1].lower() not in (".js", ".json"):
                    continue
                path = os.path.join(folder, name)
                files.append((path, "live/" + os.path.relpath(path, live).replace(os.sep, "/")))
        runtime = os.path.join(build, "live-runtime", "node.exe")
        if os.path.isfile(runtime):
            files.append((runtime, "live/node.exe"))
    return files, [(arcname, sbom_name) for _, arcname, sbom_name in qt_files]


def license_ids(expression):
    tokens = re.findall(r"[A-Za-z0-9.+\-]+", expression)
    return [token for token in tokens if token not in ("AND", "OR", "WITH")]


def qt_notices(build, qt_files):
    qt_root = qt_prefix(build)
    sboms = glob.glob(os.path.join(qt_root, "sbom", "qtbase-*.spdx.json"))
    if len(sboms) != 1:
        raise PackageError("Expected one qtbase SBOM in %s/sbom" % qt_root)
    with open(sboms[0], encoding="utf-8") as handle:
        sbom = json.load(handle)

    packages = dict((package["SPDXID"], package) for package in sbom["packages"])
    files = dict((entry["SPDXID"], entry["fileName"]) for entry in sbom["files"])
    owner = {}
    depends = {}
    for relation in sbom["relationships"]:
        kind = relation["relationshipType"]
        if kind == "CONTAINS" and relation["relatedSpdxElement"] in files:
            owner[files[relation["relatedSpdxElement"]]] = relation["spdxElementId"]
        elif kind == "DEPENDS_ON":
            depends.setdefault(relation["spdxElementId"], []).append(relation["relatedSpdxElement"])

    modules = []
    for arcname, sbom_name in qt_files:
        if sbom_name is None:
            continue
        if sbom_name not in owner:
            raise PackageError("%s is not described by Qt's SBOM, so its notices are unknown" % arcname)
        if owner[sbom_name] not in modules:
            modules.append(owner[sbom_name])

    reached = []
    pending = list(modules)
    while pending:
        spdx_id = pending.pop(0)
        if spdx_id in reached or spdx_id not in packages:
            continue
        reached.append(spdx_id)
        pending.extend(dependency for dependency in depends.get(spdx_id, []) if "3rdparty" in dependency)

    attributions = []
    seen = set()
    for spdx_id in reached:
        package = packages[spdx_id]
        if "3rdparty" not in spdx_id or package.get("licenseConcluded", "NOASSERTION") == "NOASSERTION":
            continue
        key = (package["licenseConcluded"], package.get("copyrightText", ""))
        if key in seen:
            continue
        seen.add(key)
        attributions.append(package)

    core = [package for package in sbom["packages"] if package["name"] == "Core"][0]
    commit = core["downloadLocation"].rsplit("@", 1)[-1]
    version = core["versionInfo"]
    extracted = dict((info["licenseId"], info["extractedText"]) for info in sbom.get("hasExtractedLicensingInfos", []))
    return {
        "version": version,
        "commit": commit,
        "modules": [packages[spdx_id]["name"] for spdx_id in modules],
        "attributions": attributions,
        "extracted": extracted,
    }


def match(pattern, text, path):
    found = re.search(pattern, text)
    if not found:
        raise PackageError("Could not read %r from %s" % (pattern, os.path.relpath(path, ROOT)))
    return found.group(1)


def game_components(build):
    deps = json.loads(read(os.path.join(TOOLS, "build", "deps.json")))
    sdl_source = os.path.join(build, "_deps", "sdl3-src")
    version_path = os.path.join(sdl_source, "include", "SDL3", "SDL_version.h")
    version_header = read(version_path)
    sdl_version = ".".join(match(r"#define SDL_%s_VERSION\s+(\d+)" % part, version_header, version_path)
                           for part in ("MAJOR", "MINOR", "MICRO"))

    zlib_header = os.path.join(THIRD_PARTY, "zlib", "zlib.h")
    stb_image = os.path.join(THIRD_PARTY, "stb", "stb_image.h")
    stb_truetype = os.path.join(ROOT, "source", "vendor", "stb_truetype.hpp")
    rapidxml = os.path.join(TOOLS, "build", "third_party", "rapidxml", "rapidxml.hpp")
    bink = os.path.join(ROOT, "source", "platform", "pc", "audio", "BinkAudioDecoder.cpp")
    opus_source = os.path.join(build, "_deps", "opus-src")
    boost_license = os.path.join(LICENSE_TEXTS, "BSL-1.0.txt")
    rapidxml_header = read(rapidxml)
    truetype_header = read(stb_truetype)

    return [
        {
            "name": "SDL",
            "version": sdl_version,
            "source": "https://github.com/libsdl-org/SDL",
            "license": ["Zlib"],
            "notice": read(os.path.join(sdl_source, "LICENSE.txt")).strip(),
        },
        {
            "name": "Opus",
            "version": match(r"([0-9]+\.[0-9]+\.[0-9]+)", read(os.path.join(opus_source, "package_version")),
                             os.path.join(opus_source, "package_version")),
            "source": "https://github.com/xiph/opus",
            "license": ["BSD-3-Clause"],
            "notice": read(os.path.join(opus_source, "COPYING")).strip(),
        },
        {
            "name": "zlib",
            "version": deps["zlib"]["version"],
            "source": "https://zlib.net",
            "license": ["Zlib"],
            "notice": uncomment(block(read(zlib_header), "Copyright (C)", "madler@alumni.caltech.edu", zlib_header)),
        },
        {
            "name": "libpng",
            "version": deps["libpng"]["version"] + ", with this project's changes to png.h and pngwrite.c",
            "source": "https://github.com/pnggroup/libpng",
            "license": ["Libpng"],
            "notice": read(os.path.join(THIRD_PARTY, "libpng", "LICENSE")).strip(),
        },
        {
            "name": "Boost",
            "version": deps["boost"]["version"] + ", headers",
            "source": "https://www.boost.org",
            "license": ["BSL-1.0"],
            "notice": read(boost_license).strip(),
        },
        {
            "name": "RapidXml",
            "version": match(r"// Version ([\d.]+)", rapidxml_header, rapidxml),
            "source": "https://rapidxml.sourceforge.net",
            "license": ["BSL-1.0"],
            "notice": match(r"// (Copyright .*)", rapidxml_header, rapidxml)
                      + "\nUsed under the Boost Software License 1.0.",
        },
        {
            "name": "stb_image",
            "version": deps["stb_image"]["version"],
            "source": "https://github.com/nothings/stb",
            "license": ["public domain"],
            "notice": uncomment(block(read(stb_image), "This software is dual-licensed", "see fit.", stb_image)),
        },
        {
            "name": "stb_truetype",
            "version": match(r"stb_truetype\.h - v([\d.]+)", truetype_header, stb_truetype),
            "source": "https://github.com/nothings/stb",
            "license": ["public domain"],
            "notice": uncomment(block(truetype_header, "This software is dual-licensed", "see fit.", stb_truetype)),
        },
        {
            "name": "Bink Audio decoder, ported from FFmpeg",
            "version": "libavformat/binka.c and libavcodec/binkaudio.c",
            "source": "https://ffmpeg.org",
            "license": ["GPL-3.0-only"],
            "notice": uncomment(block(read(bink), "The container parsing", "License, as that license allows.", bink)),
        },
    ]


def launcher_components(qt_files):
    """The libraries beside the launcher that are not part of Qt: ICU, which Qt for Linux uses."""
    icu = sorted(arcname for arcname, sbom_name in qt_files if sbom_name is None and "/libicu" in arcname)
    if not icu:
        return []
    return [{
        "name": "ICU",
        "version": "%s (%s)" % (match(r"\.so\.(\d+)", icu[0], icu[0]),
                                ", ".join(os.path.basename(name) for name in icu)),
        "source": "https://github.com/unicode-org/icu",
        "license": ["Unicode-DFS-2016"],
        "notice": read(os.path.join(NOTICE_TEXTS, "ICU.txt")).strip(),
    }]


def rule(title):
    return "%s\n%s\n" % (title, "=" * len(title))


def component_text(component):
    return "%s %s\n%s\nLicense: %s\n\n%s\n" % (component["name"], component["version"], component["source"],
                                             ", ".join(component["license"]), component["notice"])


def notices_text(source, game, qt, launcher, appimage=False):
    out = [rule("Third-party notices")]
    out.append("LCEReborn is licensed under the GNU General Public License, version 3\n"
               "or later (LICENSE.txt). It contains the libraries below. The texts of\n"
               "the licenses named here are in licenses/.\n")

    out.append(rule("In " + PROGRAMS[0] + " and " + PROGRAMS[2]))
    for component in game:
        out.append(component_text(component))
        out.append("-" * 72 + "\n")

    out.append(rule("Qt, in " + PROGRAMS[1]))
    archive = "https://download.qt.io/archive/qt/%s/%s/submodules/qtbase-everywhere-src-%s.tar.xz" % (
        ".".join(qt["version"].split(".")[:2]), qt["version"], qt["version"])
    files = ("the libQt6*.so libraries in lib/ and the plugins in plugins/." if LINUX
             else "the Qt6*.dll libraries and the plugin folders beside it.")
    out.append(textwrap.fill("The launcher uses Qt %s (qtbase): %s Copyright (C) The Qt Company Ltd. and other "
                             "contributors. Qt is used under the GNU Lesser General Public License, version 3 "
                             "(licenses/LGPL-3.0-only.txt, which adds to licenses/GPL-3.0-only.txt)."
                             % (qt["version"], files), 72) + "\n\n"
               "The Qt libraries are unmodified and loaded at run time, so you can\n"
               "replace them with your own build of Qt %s. Their source code is at\n"
               "%s\n"
               "and in git at https://code.qt.io/qt/qtbase.git, commit\n"
               "%s.\n\n"
               "%s\n\n"
               "The Qt libraries contain the following third-party code.\n\n"
               % (qt["version"], archive, qt["commit"],
                  textwrap.fill("Modules shipped: %s." % ", ".join(qt["modules"]), 72)))
    for package in qt["attributions"]:
        details = re.search(r"Name: (.*)", package.get("comment", ""))
        lines = [details.group(1).strip() if details else package["name"].split("_Attribution_")[-1]]
        if package.get("versionInfo", "unknown") != "unknown":
            lines[0] += " " + package["versionInfo"]
        if package.get("downloadLocation", "NOASSERTION") != "NOASSERTION":
            lines.append(package["downloadLocation"])
        lines.append("License: %s" % package["licenseConcluded"])
        if package.get("copyrightText", "NOASSERTION") != "NOASSERTION":
            lines.append(package["copyrightText"].strip())
        out.append("\n".join(lines) + "\n")
    for component in launcher:
        out.append(rule("%s, in lib/, used by Qt" % component["name"]))
        out.append(component_text(component))
    if appimage:
        out.append(rule("The AppImage runtime"))
        out.append(read(os.path.join(NOTICE_TEXTS, "AppImageRuntime.txt")).strip() + "\n")
    out.append(rule("Source"))
    out.append("The source of this build is at\n%s/tree/%s\n" % (source["repository"], source["commit"]))
    return "\n".join(out)


def license_files(game, qt, launcher, appimage=False):
    needed = set(["GPL-3.0-only", "LGPL-3.0-only"])
    if appimage:
        needed.update(APPIMAGE_RUNTIME_LICENSES)
    for component in game + launcher:
        needed.update(identifier for identifier in component["license"] if identifier != "public domain")
    for package in qt["attributions"]:
        needed.update(license_ids(package["licenseConcluded"]))
    needed.discard("LicenseRef-Qt-Commercial")

    files = []
    for identifier in sorted(needed):
        if identifier in qt["extracted"]:
            files.append(("third_party/licenses/%s.txt" % identifier, qt["extracted"][identifier].strip() + "\n"))
            continue
        path = os.path.join(LICENSE_TEXTS, identifier + ".txt")
        if not os.path.isfile(path):
            raise PackageError("No text for the license %s. Add it to %s from its upstream source."
                               % (identifier, os.path.relpath(LICENSE_TEXTS, ROOT)))
        files.append(("third_party/licenses/%s.txt" % identifier, read(path)))
    return files


def glibc_requirement(paths):
    """The newest glibc symbol version the programs and libraries ask for."""
    versions = set()
    for path in paths:
        with open(path, "rb") as handle:
            versions.update(re.findall(rb"GLIBC_(\d+\.\d+(?:\.\d+)?)\x00", handle.read()))
    if not versions:
        raise PackageError("No glibc version found in the programs")
    return max((version.decode() for version in versions), key=lambda text: [int(part) for part in text.split(".")])


def requirements_text(glibc):
    if not LINUX:
        return ("It needs the Microsoft Visual C++ Redistributable for Visual Studio\n"
                "2015-2022 (x64), which most Windows PCs already have:\n"
                "%s\n" % REDIST_URL)
    return ("It needs 64-bit Linux with glibc %s or newer, an X11 or Wayland\n"
            "desktop and OpenGL 3.3. The launcher's X11 support also needs\n"
            "libxcb-cursor0 (xcb-util-cursor), which some systems do not install by\n"
            "default.\n" % glibc)


def server_text():
    if LINUX:
        start = ("Set up the game with the launcher first, then start it with --content-dir "
                 "~/.local/share/LCEReborn/assets to use the content the launcher imported. The AppImage "
                 "can't start it; use the tar.gz.")
    else:
        start = ("Set up the game with the launcher first, then start it from this folder, where it finds "
                 "the content the launcher imported.")
    text = ("%s plays a world for others with no window. %s It keeps its worlds, server.properties and "
            "ops.json in saves in the folder it is started from; --save-dir picks another, and one apart "
            "from your own saves is safest. Players join on TCP port 25570, and the server takes commands "
            "typed into it. See docs/pc-platform.md in the source." % (PROGRAMS[2], start))
    return "Dedicated server\n----------------\n\n%s\n\n" % textwrap.fill(text, 72, break_on_hyphens=False)


def packs_text():
    folder = "~/.local/share/LCEReborn/packs" if LINUX else "the packs folder beside the launcher"
    text = ("The packs that came free with the game load on their own. To use DLC packs you own, or custom "
            ".pck packs, copy each pack's folder, the one holding its .pck file, into %s and it loads the "
            "next time the game starts. Packs on the game's main menu lists them and opens the folder. A "
            "game you join sends you the packs it uses that you don't have." % folder)
    return "Packs\n-----\n\n%s\n\n" % textwrap.fill(text, 72, break_on_hyphens=False)


def readme_text(source, glibc):
    readme = read(os.path.join(ROOT, "README.md"))
    disclaimers = block(readme, "## Disclaimers", "\n## ", os.path.join(ROOT, "README.md"))
    disclaimers = disclaimers[len("## Disclaimers"):-len("\n## ")].strip()
    build = source["commit"][:12] + (" with uncommitted changes" if source["dirty"] else "")
    title = "LCEReborn for Linux" if LINUX else "LCEReborn for PC"
    folder = ("~/.local/share/LCEReborn. Everything the launcher\n"
              "and the game keep, including saves, stays in that folder.\n\n" if LINUX
              else "the folder beside it. Everything the launcher\n"
                   "and the game keep, including saves, stays in this folder.\n\n")
    return ("%s\n"
            "%s\n\n"
            "A PC build of LCEReborn, the reconstructed source of Minecraft: Wii U\n"
            "Edition. Build %s.\n\n"
            "This download contains no game content. Start %s:\n"
            "the first time, it asks for your own dumps of the base game and update\n"
            "688 and imports them into %s"
            "%s\n"
            "%s"
            "%s"
            "Source and license\n"
            "------------------\n\n"
            "LCEReborn is free software under the GNU General Public License,\n"
            "version 3 or later (LICENSE.txt). The source of this build is at\n"
            "%s/tree/%s\n\n"
            "third_party/NOTICES.txt lists the libraries in it and their licenses.\n\n"
            "Disclaimers\n"
            "-----------\n\n"
            "%s\n" % (title, "=" * len(title), build, PROGRAMS[1], folder, requirements_text(glibc), packs_text(), server_text(),
                      source["repository"], source["commit"], disclaimers))


def check_names(entries):
    for arcname, _ in entries:
        if not ALLOWED_NAME.search(os.path.basename(arcname)):
            raise PackageError("Refusing to package %s: not a program, library, image or text file" % arcname)


def write_zip(path, entries, date):
    stamp = date.timetuple()[:6]
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for arcname, content in entries:
            # Keep the launcher at the archive root so extracting into a folder already named
            # LCEReborn doesn't create a second nested LCEReborn directory.
            info = zipfile.ZipInfo(arcname, stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            if isinstance(content, str):
                content = content.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")
            archive.writestr(info, content)


def write_tar(path, entries, date):
    stamp = int(date.timestamp())
    with tarfile.open(path, "w:gz", compresslevel=9, format=tarfile.PAX_FORMAT) as archive:
        folders = sorted(set(os.path.dirname(arcname) for arcname, _ in entries) - {""})
        for folder in [""] + folders:
            info = tarfile.TarInfo((TOP + "/" + folder).rstrip("/"))
            info.type = tarfile.DIRTYPE
            info.mode = 0o755
            info.mtime = stamp
            archive.addfile(info)
        for arcname, content in entries:
            if isinstance(content, str):
                content = content.replace("\r\n", "\n").encode("utf-8")
            info = tarfile.TarInfo(TOP + "/" + arcname)
            info.size = len(content)
            info.mode = 0o755 if arcname in PROGRAMS else 0o644
            info.mtime = stamp
            archive.addfile(info, io.BytesIO(content))


def appimage_tools():
    tool = shutil.which("appimagetool")
    runtime = os.environ.get("APPIMAGE_RUNTIME", "")
    if tool is None or not os.path.isfile(runtime):
        raise PackageError("The AppImage needs appimagetool on PATH and APPIMAGE_RUNTIME set to the AppImage type 2 "
                           "runtime, as tools/build/Dockerfile.linux installs them; --no-appimage skips it")
    return tool, runtime


def write_appimage(path, entries, date, tools):
    """Lays the package out as an AppDir that starts the launcher and packs it with appimagetool."""
    tool, runtime = tools
    with open(os.path.join(ROOT, "resources", "launcher", "icon.png"), "rb") as handle:
        icon = handle.read()
    extra = [
        ("AppRun", '#!/bin/sh\nhere="$(dirname "$(readlink -f "$0")")"\nexec "$here/LCERebornLauncher" "$@"\n'),
        ("lcereborn.desktop", "[Desktop Entry]\nType=Application\nName=LCEReborn\n"
                              "Comment=Minecraft: Wii U Edition from your own copy of the game\n"
                              "Exec=LCERebornLauncher\nIcon=lcereborn\nCategories=Game;\nTerminal=false\n"),
        ("lcereborn.png", icon),
        (".DirIcon", icon),
    ]
    with tempfile.TemporaryDirectory() as temp:
        app_dir = os.path.join(temp, "LCEReborn.AppDir")
        for arcname, content in entries + extra:
            target = os.path.join(app_dir, arcname)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            if isinstance(content, str):
                content = content.replace("\r\n", "\n").encode("utf-8")
            with open(target, "wb") as handle:
                handle.write(content)
            os.chmod(target, 0o755 if arcname in PROGRAMS + ("AppRun",) else 0o644)
        environment = dict(os.environ, ARCH="x86_64", SOURCE_DATE_EPOCH=str(int(date.timestamp())))
        result = subprocess.run([tool, "--no-appstream", "--runtime-file", runtime, app_dir, path], env=environment,
                                capture_output=True, text=True)
        if result.returncode != 0:
            raise PackageError("appimagetool failed:\n%s%s" % (result.stdout, result.stderr))
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def package(build, allow_dirty, appimage=True):
    """Writes the packages and returns their paths and source_info()."""
    source = source_info(allow_dirty)
    appimage = appimage and LINUX
    tools = appimage_tools() if appimage else None
    files, qt_files = program_files(build)
    qt = qt_notices(build, qt_files)
    game = game_components(build)
    launcher = launcher_components(qt_files)
    glibc = glibc_requirement([path for path, arcname in files if arcname in PROGRAMS or ".so" in arcname]) \
        if LINUX else None

    entries = []
    for path, arcname in files:
        with open(path, "rb") as handle:
            entries.append((arcname, handle.read()))
    entries.append(("README.txt", readme_text(source, glibc)))
    entries.append(("LICENSE.txt", read(os.path.join(ROOT, "LICENSE"))))
    if not LINUX:
        node_license = os.path.join(build, "live-runtime", "Node-LICENSE.txt")
        if os.path.isfile(node_license):
            entries.append(("third_party/Node-LICENSE.txt", read(node_license)))
    common = list(entries)
    entries.append(("third_party/NOTICES.txt", notices_text(source, game, qt, launcher)))
    entries.extend(license_files(game, qt, launcher))
    check_names(entries)

    stem = os.path.join(OUTPUT_DIR, "LCEReborn-%s-%s%s" % (SYSTEM, source["commit"][:12],
                                                          "-dirty" if source["dirty"] else ""))
    outputs = []
    if LINUX:
        outputs.append(stem + ".tar.gz")
        write_tar(outputs[-1], entries, source["date"])
    else:
        outputs.append(stem + ".zip")
        write_zip(outputs[-1], entries, source["date"])
    print("Wrote %s (%d files)" % (os.path.relpath(outputs[-1], ROOT), len(entries)))

    if appimage:
        entries = common + [("third_party/NOTICES.txt", notices_text(source, game, qt, launcher, appimage=True))]
        entries.extend(license_files(game, qt, launcher, appimage=True))
        check_names(entries)
        outputs.append(stem + ".AppImage")
        write_appimage(outputs[-1], entries, source["date"], tools)
        print("Wrote %s" % os.path.relpath(outputs[-1], ROOT))
    return outputs, source


def main():
    parser = argparse.ArgumentParser(description="Package the PC build to give to players.")
    parser.add_argument("--build", default=DEFAULT_BUILD, help="the PC build folder (default build/pc)")
    parser.add_argument("--allow-dirty", action="store_true",
                        help="package a tree with uncommitted changes, for testing only")
    parser.add_argument("--no-appimage", action="store_true", help="on Linux, write only the tar.gz")
    args = parser.parse_args()

    try:
        package(os.path.abspath(args.build), args.allow_dirty, not args.no_appimage)
    except PackageError as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
