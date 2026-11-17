# Game data ZIP reader

The first-run downloader uses yauzl 3.4.0 (MIT) and pend 1.2.0 (MIT).
Runtime sources and the original license texts are vendored in `node_modules/`;
no npm installation or external archiver is needed by players.

Sources: https://github.com/thejoshwolfe/yauzl and https://github.com/andrewrk/node-pend

The original npm tarballs were verified before unpacking:

- yauzl 3.4.0: `sha512-jIH9yLR9wqr0wOS0TpBvo/g/2UgZH5qePVbjgRliiF0BYvOZyaBknKsF+x9Iht0O6sqgnB93rCICdOZFecJuDw==`
- pend 1.2.0: `sha512-F3asv42UuXchdzt+xXqfW1OGlVBe+mxa2mqI0pg5yAHZPvFmY3Y6drSf/GQ1A86WgWEN9Kzh/WrgKa6iGcHXLg==`

SHA-256 verifies each complete game archive before extraction. Extraction also
checks CRC-32 and sizes, rejects links and unsafe paths, and stays in a private
staging directory. The existing launcher import validates the dump again.
