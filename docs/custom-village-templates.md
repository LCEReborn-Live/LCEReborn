# Local village templates

LCEReborn on Windows scans user-created village templates at startup. Use `--village-templates-dir` to point either executable at a local directory. Relative paths are resolved from the working directory. The default is `custom-villages` under the content directory, normally `assets/custom-villages`. The host of a Live game or the dedicated server needs the files. Templates can stay outside the repository.

```powershell
.\LCEReborn-Live.exe --village-templates-dir "D:\My Village Templates"
.\LCERebornServer.exe --village-templates-dir "D:\My Village Templates"
```

## Folder-based pools

```text
assets/custom-villages/
  plains/
    center/hub.nbt
    roads/track.nbt
    homes/shelter.nbt
    decor/sculpture.nbt
```

The biome folders are `plains`, `desert`, `savanna`, `taiga`, and `snowy`. The first two directory levels give the biome and pool. Files in deeper subdirectories belong to the same pool. Each discovered template has a weight of 1, and villages start with a template from `center`.

## Configured pools and starts

Place an optional UTF-8 file named `village-pools.tsv` in the template directory. Separate fields with literal tabs. Lines beginning with `#` are comments.

```text
start	plains	squares
pool	plains	squares	centers/open-square.nbt	4	rigid
pool	plains	squares	centers/market.nbt	1	rigid
pool	plains	homes	buildings/cottage.nbt	3	rigid
pool	*	roads	paths/dirt-track.nbt	1	terrain
```

A `start` row has the biome and its start pool. A `pool` row has the biome, pool ID, relative template filename, weight and optional placement mode. Use `*` for a definition shared across biomes. Biome-specific definitions take precedence over shared definitions. An explicit pool replaces the folder-based entries for that pool. Template paths may be nested anywhere under the root and use forward slashes.

Relative weights can range from 1 to 1,000,000. A weight of 4 gives a piece four times the initial selection chance of a piece with weight 1 in the same pool. If a selected piece cannot fit or its connector does not match, generation tries the remaining candidates. Templates in `roads` default to `terrain` placement. Other pools default to `rigid`. Start structures are placed as a unit at the village's starting elevation.

## Template data

An outgoing Jigsaw with `pool = "lce:homes"` looks in the `homes` pool for the current biome. The outgoing `target` must match the incoming `name`. Both names use the `lce:` namespace. A connector with `pool = "lce:empty"` is available as an entrance or end point.

Give each Jigsaw a horizontal `facing` or `orientation` in its palette entry. In the block's NBT, include `name`, `pool`, and `target` for outgoing connections. Set `final_state` to the block that should occupy the connector position, such as `minecraft:gravel`, or leave it empty for air. Templates in the `roads` pool follow the terrain. Other pools use their supplied geometry and rotate as a unit.

Use raw or gzip-compressed big-endian NBT with one `palette`, a `size` list, and a `blocks` list. Template dimensions can be up to 64 blocks on each axis. The startup log reports loaded files and skips unreadable templates. Palette diagnostics include the source file, entry number, and the unsupported block or property value.

A local layout needs at least six pieces within the village bounds. Newly generated villages use the standard generation path when no local layout fits. Keep the template paths and contents stable for an existing world so partially generated villages can resume across saves. Changes to templates or pools take effect the next time the game or server starts.
