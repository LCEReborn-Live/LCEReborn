#!/usr/bin/env python3
"""Fetch a checksum-verified official Node.js runtime for the Live bridge."""
import argparse
import hashlib
import os
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile


def download(url, destination):
    request = urllib.request.Request(url, headers={"User-Agent": "LCEReborn-Live-build"})
    with urllib.request.urlopen(request, timeout=60) as response, open(destination, "wb") as output:
        shutil.copyfileobj(response, output)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True, help="Node version, for example v22.16.0")
    parser.add_argument("--platform", choices=("win-x64", "linux-x64"), required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    version = args.version.strip()
    if not version.startswith("v") or any(char not in "v0123456789." for char in version):
        parser.error("version must look like v22.16.0")
    suffix = ".zip" if args.platform == "win-x64" else ".tar.xz"
    archive_name = "node-%s-%s%s" % (version, args.platform, suffix)
    base = "https://nodejs.org/dist/%s/" % version
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="lcereborn-node-") as temporary:
        sums = os.path.join(temporary, "SHASUMS256.txt")
        archive = os.path.join(temporary, archive_name)
        download(base + "SHASUMS256.txt", sums)
        expected = None
        with open(sums, encoding="ascii") as handle:
            for line in handle:
                fields = line.split()
                if len(fields) == 2 and fields[1].lstrip("*") == archive_name:
                    expected = fields[0].lower()
                    break
        if expected is None:
            raise RuntimeError("official Node checksum list has no entry for " + archive_name)
        download(base + archive_name, archive)
        digest = hashlib.sha256()
        with open(archive, "rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != expected:
            raise RuntimeError("Node.js archive checksum does not match the official checksum list")

        if args.platform == "win-x64":
            with zipfile.ZipFile(archive) as package:
                names = set(package.namelist())
                prefix = "node-%s-win-x64/" % version
                node_name, license_name = prefix + "node.exe", prefix + "LICENSE"
                if node_name not in names or license_name not in names:
                    raise RuntimeError("Node.js Windows archive is missing its executable or license")
                payload = (("node.exe", package.read(node_name)), ("Node-LICENSE.txt", package.read(license_name)))
        else:
            prefix = "node-%s-linux-x64/" % version
            with tarfile.open(archive, "r:xz") as package:
                members = {member.name: member for member in package.getmembers() if member.isfile()}
                node_member, license_member = members.get(prefix + "bin/node"), members.get(prefix + "LICENSE")
                if not node_member or not license_member:
                    raise RuntimeError("Node.js Linux archive is missing its executable or license")
                node_file, license_file = package.extractfile(node_member), package.extractfile(license_member)
                payload = (("node", node_file.read()), ("Node-LICENSE.txt", license_file.read()))

        os.makedirs(args.output, exist_ok=True)
        for name, content in payload:
            target = os.path.join(args.output, name)
            with open(target + ".tmp", "wb") as handle:
                handle.write(content)
            os.replace(target + ".tmp", target)
        if args.platform == "linux-x64":
            os.chmod(os.path.join(args.output, "node"), 0o755)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, tarfile.TarError, zipfile.BadZipFile) as error:
        print("error: %s" % error, file=sys.stderr)
        sys.exit(1)
