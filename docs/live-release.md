# LCEReborn Live releases

The `Windows and Linux build and release` workflow builds both clients from the
tagged source commit and packages the Live bridge with its checksum-verified
Node.js runtime. Linux gets an AppImage and a portable `.tar.gz`; Windows gets a
per-user installer and portable ZIP. Pushes to `main` and `live-integration`
and pull requests to `main` build both platforms and publish workflow artifacts
without creating a GitHub Release.

To publish a release, merge the client changes, create and push a version tag
such as `v0.1.0`, then wait for the workflow to finish. The GitHub Release gets
`LCEReborn-Live-Installer-0.1.0.exe`,
`LCEReborn-Live-Portable-0.1.0.zip`,
`LCEReborn-Live-Linux-0.1.0.AppImage`, and
`LCEReborn-Live-Linux-Portable-0.1.0.tar.gz`.

The packages contain the executables, launcher resources, open-source runtime
dependencies, and Live networking scripts. They do not contain game content,
player profiles, saves, account tokens, or cosmetics. On first launch, players
must import their own base game and Update 688 dumps through the launcher.

The client keeps its Live API endpoint in the launcher source. The workflow does
not replace it or require API credentials to compile the client.

The API endpoint is `https://api.lcereborn.xyz`; browser sign-in opens
`https://lcereborn.xyz/authorize`. The same saved account tokens remain valid.
Existing releases using `api.kerneldev.qzz.io` continue to work through the
legacy server alias. Relay addresses come from the API rather than being
hardcoded in the client.

## Browser joins

The Windows installer registers `lcereborn://join/<owner-id>` for the current
user. Linux users can select **Enable browser joins** in Friends to register a
per-user desktop handler; an AppImage registers its stable AppImage path, while
a portable archive registers the current launcher path. Moving a portable
folder requires registering again. Windows portable users can use the same
button.

Join links contain only a positive account ID. The launcher checks the world
directory using its own saved session, then the existing bridge obtains the
usual direct/relay join ticket. A website sign-in does not transfer credentials
to the launcher or grant access to private worlds. Existing running launcher
instances receive the request through a local socket restricted to the user;
the per-installation lock prevents a duplicate launcher instance.

If the game is closed, the selected profile starts before the join command is
queued. If a world is already open, the player must return to the main menu;
the existing in-game invitation handler consumes the command there.

Configure with `-DLCE_BUILD_LIVE_JOIN_TESTS=ON` to build
`LCERebornLiveJoinUrlTest` (strict link validation) and
`LCERebornLiveJoinIpcTest` (local forwarding). The IPC test takes the path to a
launcher copied into an isolated test folder with its Qt dependencies; do not
point it at an in-use player installation. In headless runs set
`QT_QPA_PLATFORM=offscreen` and include Qt's offscreen platform plugin.

On Linux, the Live bridge reads and writes the same session files through the
POSIX file bridge. The release packages include the Node.js 22 runtime and its
license, so players do not need a separate Node installation. The Linux
launcher opens AppImage updates in the browser; users of a portable archive
download and replace it manually.
