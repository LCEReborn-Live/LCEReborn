#!/usr/bin/env python3
"""Build, package and publish a PC release on GitHub.

    python tools/release_pc.py

Run it where the PC build is made (docs/pc-platform.md: on Windows, the
same Visual Studio x64 developer prompt), on a clean tree whose commit is
pushed. It:

  1. builds everything in the PC build folder, so the game and the launcher
     match the commit;
  2. packages them with tools/package_pc.py;
  3. shows the release it is about to publish and asks before publishing:
     the name is the commit and the date it was built ("9b3a482d -
     2026-10-19"), the tag build-<commit>, the notes the Unreleased section
     of release/CHANGELOG.md and the packages attached, with any packages
     of the same commit for the other system already in build/ (on
     Windows, run tools/build_linux.py first to publish the Linux ones too);
  4. publishes it to the origin repository with the GitHub CLI (gh), which
     must be signed in;
  5. files the Unreleased notes under the release's name, leaving Unreleased
     empty.

When the commit already has a release, made from the other system, it only
adds this system's packages to it, leaving the notes and the changelog alone.

release/ is not committed: the changelog is only where the notes are
written. The script creates it the first time it runs.

The launcher's News tab shows the published releases.

Options:
  --build DIR     the PC build folder (default build/pc)
  --dry-run       do everything except publishing and editing the changelog
  --prerelease    mark the release as a pre-release, for trying a build out;
                  News shows it marked as one
  --allow-dirty   with --dry-run, go on with uncommitted changes
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import package_pc  # noqa: E402
import package_server  # noqa: E402
from package_pc import PackageError  # noqa: E402

CHANGELOG = os.path.join(ROOT, "release", "CHANGELOG.md")
UNRELEASED = "## Unreleased"
TEMPLATE = ("# Changelog\n\n"
            "What changed for players in each PC release. Add a line under Unreleased\n"
            "for every change a player will notice; tools/release_pc.py publishes that\n"
            "section as the release notes on GitHub, which the launcher's News tab shows,\n"
            "and then files it under the release's name. This folder is not committed.\n\n"
            "## Unreleased\n")


def read_changelog():
    if not os.path.isfile(CHANGELOG):
        os.makedirs(os.path.dirname(CHANGELOG), exist_ok=True)
        with open(CHANGELOG, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(TEMPLATE)
        raise PackageError("Created %s. Write the release notes under Unreleased, then run this again."
                           % os.path.relpath(CHANGELOG, ROOT))
    return package_pc.read(CHANGELOG)


def run(command, what):
    try:
        result = subprocess.run(command, cwd=ROOT)
    except OSError as error:
        raise PackageError("%s: could not run %s (%s)" % (what, command[0], error))
    if result.returncode != 0:
        raise PackageError("%s failed" % what)


def build(folder):
    print("Building %s" % os.path.relpath(folder, ROOT))
    what = "The build (run this from a Visual Studio x64 developer prompt)" if os.name == "nt" else "The build"
    run(["cmake", "--build", folder], what)


def build_info(folder):
    path = os.path.join(folder, "launcher", "BuildInfo.hpp")
    text = package_pc.read(path)
    values = dict(re.findall(r"#define LCE_BUILD_(\w+) \"?([^\"\n]*)\"?", text))
    for key in ("COMMIT", "DIRTY", "DATE"):
        if key not in values:
            raise PackageError("%s has no LCE_BUILD_%s" % (os.path.relpath(path, ROOT), key))
    return values


def unreleased_notes(text):
    start = text.find(UNRELEASED + "\n")
    if start < 0:
        raise PackageError("release/CHANGELOG.md has no '%s' section" % UNRELEASED)
    body_start = start + len(UNRELEASED) + 1
    following = text.find("\n## ", body_start)
    body_end = len(text) if following < 0 else following + 1
    notes = text[body_start:body_end].strip()
    if not re.search(r"^- ", notes, re.M):
        raise PackageError("release/CHANGELOG.md's Unreleased section has no entries. "
                           "Add what changed for players first.")
    return notes, body_start, body_end


def unwrap(notes):
    lines = []
    for line in notes.splitlines():
        continues = lines and lines[-1].strip() and line.strip() and not re.match(r"\s*(- |#)", line) \
                    and not lines[-1].startswith("#")
        if continues:
            lines[-1] += " " + line.strip()
        else:
            lines.append(line.rstrip())
    return "\n".join(lines)


def quote_mentions(notes):
    # GitHub turns @name into a mention and lists that account as a contributor, so @p or @e[type=cow] go in code.
    mention = re.compile(r"(?<![\w@])@[A-Za-z0-9][A-Za-z0-9-]*(?:\[[^\]\s`]*\])?")
    parts = re.split(r"(`[^`]*`)", notes)
    return "".join(part if index % 2 else mention.sub(lambda match: "`%s`" % match.group(0), part)
                   for index, part in enumerate(parts))


def release_notes(notes):
    return quote_mentions(unwrap(notes))


def file_notes(text, body_start, body_end, title, notes):
    return text[:body_start] + "\n## %s\n\n%s\n\n" % (title, notes) + text[body_end:].lstrip("\n")


def tag_exists(tag, repository):
    result = subprocess.run(["gh", "release", "view", tag, "--repo", repository], cwd=ROOT, capture_output=True,
                            text=True)
    if result.returncode == 0:
        return True
    if "release not found" in result.stderr:
        return False
    raise PackageError("gh could not look up %s: %s" % (tag, result.stderr.strip()))


def other_packages(commit, outputs):
    """Packages of the same commit for the other system, such as tools/build_linux.py's, to publish too."""
    pattern = os.path.join(package_pc.OUTPUT_DIR, "LCEReborn-*-%s.*" % commit[:12])
    made = set(os.path.abspath(path) for path in outputs)
    return sorted(path for path in glob.glob(pattern) if os.path.abspath(path) not in made)


def add_to_release(tag, repository, outputs, dry_run):
    print("\nRelease  %s exists already\nRepo     %s\nAdding   %s\n"
          % (tag, repository, "\n         ".join(os.path.relpath(path, ROOT) for path in outputs)))
    if dry_run:
        print("Dry run: nothing was published.")
        return 0
    if input("Add these packages to the release on GitHub? [y/N] ").strip().lower() != "y":
        print("Not published.")
        return 1
    run(["gh", "release", "upload", tag] + outputs + ["--repo", repository], "Adding the packages to the release")
    print("Added %s to %s." % (", ".join(os.path.basename(path) for path in outputs), tag))
    return 0


def main():
    parser = argparse.ArgumentParser(description="Build, package and publish a PC release on GitHub.")
    parser.add_argument("--build", default=package_pc.DEFAULT_BUILD, help="the PC build folder (default build/pc)")
    parser.add_argument("--dry-run", action="store_true", help="do everything except publishing")
    parser.add_argument("--prerelease", action="store_true", help="mark the release as a pre-release")
    parser.add_argument("--allow-dirty", action="store_true", help="with --dry-run, allow uncommitted changes")
    args = parser.parse_args()
    if args.allow_dirty and not args.dry_run:
        parser.error("--allow-dirty only goes with --dry-run")
    folder = os.path.abspath(args.build)

    try:
        package_pc.source_info(args.allow_dirty)

        build(folder)
        info = build_info(folder)
        outputs, source = package_pc.package(folder, args.allow_dirty)
        server_outputs, _ = package_server.package(folder, args.allow_dirty, source=source)
        outputs.extend(server_outputs)
        commit = source["commit"]
        if info["COMMIT"] != commit[:len(info["COMMIT"])]:
            raise PackageError("The build is of %s, not of HEAD %s" % (info["COMMIT"], commit[:12]))
        if info["DIRTY"] != "0" and not args.allow_dirty:
            raise PackageError("The build has uncommitted changes in it")
        if not args.dry_run and not package_pc.git("branch", "-r", "--contains", commit):
            raise PackageError("%s is not pushed. Push it first, so the release's source is public." % commit[:12])

        short = info["COMMIT"]
        title = "%s - %s" % (short, info["DATE"])
        tag = "build-%s" % short
        repository = source["repository"][len("https://github.com/"):]
        if tag_exists(tag, repository):
            return add_to_release(tag, repository, outputs, args.dry_run)
        changelog = read_changelog()
        notes, body_start, body_end = unreleased_notes(changelog)
        packages = outputs + other_packages(commit, outputs)
        command = ["gh", "release", "create", tag] + packages + ["--repo", repository, "--target", commit,
                                                                 "--title", title]
        if args.prerelease:
            command.append("--prerelease")
        command.append("--notes-file")

        print("\nRelease  %s%s\nRepo     %s\nTag      %s\nCommit   %s\nPackages %s\n\nNotes:\n\n%s\n"
              % (title, " (pre-release)" if args.prerelease else "", repository, tag, commit,
                 "\n         ".join(os.path.relpath(path, ROOT) for path in packages), release_notes(notes)))
        if args.dry_run:
            print("Dry run: nothing was published and the changelog is unchanged.")
            return 0
        if input("Publish this release on GitHub? [y/N] ").strip().lower() != "y":
            print("Not published.")
            return 1

        with tempfile.NamedTemporaryFile("w", suffix=".md", delete=False, encoding="utf-8") as handle:
            handle.write(release_notes(notes) + "\n")
            notes_file = handle.name
        try:
            run(command + [notes_file], "Publishing the release")
        finally:
            os.remove(notes_file)

        with open(CHANGELOG, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(file_notes(changelog, body_start, body_end, title, notes))
    except PackageError as error:
        print("error: %s" % error, file=sys.stderr)
        return 1

    print("Published %s; release/CHANGELOG.md now files its notes under it." % title)
    return 0


if __name__ == "__main__":
    sys.exit(main())
