# LCEReborn Live Windows releases

The `Windows build and release` workflow builds the game and launcher from the
tagged source commit, packages the Live bridge and its Node.js runtime, then
creates a per-user Windows installer and a portable ZIP. It also runs on pushes
to `main` and `live-integration` and on pull requests to `main`; those runs
publish downloadable build artifacts without creating a GitHub Release.

To publish a release, merge the client changes, create and push a version tag
such as `v0.1.0`, then wait for the workflow to finish. The GitHub Release gets
`LCEReborn-Live-Setup-0.1.0.exe` and the portable ZIP.

The packages contain the executables, launcher resources, open-source runtime
dependencies, and Live networking scripts. They do not contain game content,
player profiles, saves, account tokens, or cosmetics. On first launch, players
must import their own base game and Update 688 dumps through the launcher.

The client keeps its Live API endpoint in the launcher source. The workflow does
not replace it or require API credentials to compile the client.
