#!/usr/bin/env python3
"""Build and package the Linux PC version in Docker, from any host.

    python tools/build_linux.py

Builds the game and the launcher for Linux in an Ubuntu 22.04 container
(tools/build/Dockerfile.linux), so the result also runs on older
distributions, then packages them with tools/package_pc.py as
build/LCEReborn-linux-<commit>.tar.gz. Run tools/build/deps.py and
tools/build/layout.py once first, as for the other builds.

The first run creates the image and installs Qt for Linux; both, and the build
folder, are kept in Docker (the volumes lcereborn-linux-qt and
lcereborn-linux-build), so later runs rebuild only what changed. On a Linux
machine you can also build natively (docs/pc-platform.md).

Options:
  --clean         discard the cached build first (Qt and the image are kept)
  --allow-dirty   package a tree with uncommitted changes, for testing
  --no-package    build only
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
DOCKERFILE = os.path.join(TOOLS, "build", "Dockerfile.linux")
IMAGE_NAME = "lcereborn-linux"
QT_VOLUME = "lcereborn-linux-qt"
BUILD_VOLUME = "lcereborn-linux-build"


class BuildError(Exception):
    """The Linux build could not be made."""


def image_tag():
    with open(DOCKERFILE, "rb") as handle:
        return "%s:%s" % (IMAGE_NAME, hashlib.sha256(handle.read().replace(b"\r\n", b"\n")).hexdigest()[:16])


def ensure_image():
    tag = image_tag()
    present = subprocess.run(["docker", "image", "inspect", tag], stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL).returncode == 0
    if not present:
        print("building the image %s" % tag, flush=True)
        with open(DOCKERFILE, "rb") as handle:
            if subprocess.run(["docker", "build", "-t", tag, "-"], stdin=handle).returncode != 0:
                raise BuildError("building the image failed")
    return tag


def prepare_node_runtime():
    node = shutil.which("node") or shutil.which("nodejs")
    if node is None:
        raise BuildError("Node.js 22 is required to prepare the Live runtime; install Node.js 22 and retry")
    version = subprocess.run([node, "--version"], capture_output=True, text=True)
    if version.returncode != 0 or not version.stdout.strip().startswith("v22."):
        raise BuildError("Node.js 22 is required to prepare the Live runtime (found %s)"
                         % version.stdout.strip())
    destination = os.path.join(ROOT, "build", "live-runtime")
    command = [sys.executable, os.path.join(TOOLS, "build", "node_runtime.py"), "--version",
               version.stdout.strip(), "--platform", "linux-x64", "--output", destination]
    if subprocess.run(command).returncode != 0:
        raise BuildError("preparing the checksum-verified Node.js runtime failed")


def main():
    parser = argparse.ArgumentParser(description="Build and package the Linux PC version in Docker.")
    parser.add_argument("--clean", action="store_true", help="discard the cached build first")
    parser.add_argument("--allow-dirty", action="store_true", help="package a tree with uncommitted changes")
    parser.add_argument("--no-package", action="store_true", help="build only")
    args = parser.parse_args()

    try:
        if shutil.which("docker") is None:
            raise BuildError("docker is not installed or not on PATH")
        if subprocess.run(["docker", "info"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
            raise BuildError("the Docker daemon is not running")
        if not os.path.isdir(os.path.join(ROOT, "build", "third_party", "boost", "boost")):
            raise BuildError("Third-party sources are missing. Run:\n"
                             "  python tools/build/deps.py build/deps\n"
                             "  python tools/build/layout.py build/deps build/third_party")
        if not args.no_package:
            prepare_node_runtime()
        image = ensure_image()
        if args.clean:
            subprocess.run(["docker", "volume", "rm", "-f", BUILD_VOLUME], stdout=subprocess.DEVNULL)

        steps = ["git config --global --add safe.directory /src",
                 "python3 /src/tools/build/qt.py",
                 "cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release",
                 "cmake --build /build"]
        if not args.no_package:
            steps.append("mkdir -p /build/live-runtime && cp /src/build/live-runtime/* /build/live-runtime/")
            steps.append("python3 /src/tools/package_pc.py --build /build" + (" --allow-dirty" if args.allow_dirty
                                                                              else ""))
        command = ["docker", "run", "--rm", "-v", ROOT + ":/src", "-v", QT_VOLUME + ":/src/build/qt",
                   "-v", BUILD_VOLUME + ":/build", image, "sh", "-c", " && ".join(steps)]
        print("+ docker run %s %s" % (image, " && ".join(steps)), flush=True)
        if subprocess.run(command).returncode != 0:
            raise BuildError("the Linux build failed")
    except BuildError as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
