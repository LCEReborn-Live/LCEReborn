# Bundled game data

Release packages include the complete prepared `assets/` directory: menus,
textures, sounds, DLC and local structure templates. Start the launcher and
play; no separate game-data download or import is needed. Windows installers
include this directory, and Linux tar.gz and AppImage packages use the assets
beside the launcher, keeping saves and profiles in the user's data directory.

Packaging verifies every file against `assets/.bundle-manifest.json` and fails
if content is missing or changed. After intentionally updating the assets, run
`python tools/bundled_assets.py --write-manifest` and commit the content and
manifest together. The separate 1.14 Texture Pack is no longer bundled.

## Recovery and source-only builds

The launcher offers **Download & install** alongside the existing local-folder
import. The automatic path downloads the base game and Update 688, checks each
archive, unpacks it, and passes both folders to the original `AssetPreparer`.
The game client, Live networking and save formats are unchanged.

Download sources live in `resources/pc/game-downloads.json`. For each archive,
configure an HTTPS URL, its exact compressed and uncompressed sizes, SHA-256,
title ID and title version. Replace those values together when replacing an
archive. Do not disable verification when changing hosts. Google Drive links
must permit anonymous downloading.

The bundled Node runtime handles streaming downloads and ZIP/ZIP64 extraction;
players do not need Python, WinRAR or another archiver. Stored and Deflate ZIP
entries are supported. Google Drive confirmation forms and HTTPS redirects are
handled without signing in. If Drive refuses access or reaches its download
quota, the launcher reports the error and keeps local import available.

Downloaded data stays under the launcher's data directory in
`downloads/game-files/<archive-sha256>/`. Cancellation preserves a partial ZIP
for a later Range request. If a server ignores Range, the downloader restarts
the archive. A complete ZIP is accepted only after SHA-256 verification; every
extracted file also receives a size and CRC-32 check. Extraction rejects unsafe
paths, symlinks, duplicate paths, encrypted files and unexpected expanded sizes.
The game metadata is checked before a prepared directory is exposed to the UI.

Downloaded archives and extracted dumps are cached for retries. They can be
removed from `downloads/game-files` after a successful import; the installed
`assets` directory and saves are separate. The downloader checks free space for
both extraction and the subsequent import.

## Village & Pillage resources

The PC packages already contain the prepared Village & Pillage resources.
To update them from another PS4 1.95 dump during development, import its
`Common` folder into a separate prepared data directory:

```sh
python tools/import_common_update.py --common /path/to/ps4-update/Common \
    --assets /path/to/prepared/assets --backup-dir /path/to/tu80-backup
```

The `tools/import_tu80_ui.py` and `tools/import_tu80_sounds.py` tools adapt
the PS4 menus and sound bank for PC. Use `--help` for their input paths.
The client and server can load optional local structure templates from
`assets/custom-villages`. See [local templates](custom-village-templates.md).

## Verification

Run `node --test tools/tests/game_download_test.mjs` on Windows or Linux. It
covers resume, ignored and invalid Range responses, cancellation, checksum and
CRC failures, HTML responses, ZIP64, unsafe paths and dump validation.

For a full check with the authorized game archives, configure CMake with
`-DLCE_BUILD_GAME_DOWNLOAD_TESTS=ON` and build `LCERebornGameFilesTest`. Put the
matching download cache under an isolated `build/game-download-check` directory
and pass that directory to the test. The check runs the real process adapter,
imports into a temporary directory, and compares every merged file by SHA-256.
It does not use the player's installed assets or saves.
