# LCEReborn Live Windows releases

The `Windows build and release` workflow builds the game and launcher from the
tagged source commit, packages the Live bridge and its Node.js runtime, then
creates a per-user Windows installer and a portable ZIP. It also runs on pushes
to `main` and `live-integration` and on pull requests to `main`; those runs
publish downloadable build artifacts without creating a GitHub Release.

To publish a release, merge the client changes, create and push a version tag
such as `v0.1.0`, then wait for the workflow to finish. The GitHub Release gets
`LCEReborn-Live-Installer-0.1.0.exe` and
`LCEReborn-Live-Portable-0.1.0.zip`.

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
user. Portable users can select **Enable browser joins** in Friends to register
their current launcher path. Moving that folder requires registering again.
Uninstalling an installed copy removes the handler only when it still points to
that installation.

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
