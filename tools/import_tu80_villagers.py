"""Prepare villager textures from a local TU80 res folder (requires Pillow)."""

import argparse
from pathlib import Path

from PIL import Image

BIOMES = ("plains", "desert", "jungle", "savanna", "snow", "swamp", "taiga")
PROFESSIONS = (
    "unskilled", "farmer", "fisherman", "shepherd", "fletcher", "librarian",
    "cartographer", "cleric", "armorer", "weaponsmith", "toolsmith", "butcher",
    "leatherworker", "nitwit", "stonemason",
)


def load_layer(path):
    with Image.open(path) as image:
        if image.size != (64, 64):
            raise ValueError(f"Unexpected villager texture size: {path}")
        return image.convert("RGBA")


def import_villagers(source, assets):
    source = source / "mob/villager"
    output = assets / "Common/res/TitleUpdate/res/mob/villager/tu80"
    base = load_layer(source / "villagerBase1.png")
    biomes = {name: load_layer(source / f"biomes/biome_{name}.png") for name in BIOMES}
    jobs = {name: load_layer(source / f"professions/{name}.tga") for name in PROFESSIONS}
    output.mkdir(parents=True, exist_ok=True)
    for biome, clothes in biomes.items():
        outfit = Image.alpha_composite(base, clothes)
        for job, overlay in jobs.items():
            path = output / f"{biome}_{job}.png"
            temporary = path.with_suffix(".tmp")
            Image.alpha_composite(outfit, overlay).save(temporary, format="PNG")
            temporary.replace(path)
    print(f"Prepared {len(biomes) * len(jobs)} villager textures in {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="TU80 TitleUpdate/res folder")
    parser.add_argument("--assets", type=Path, required=True, help="Prepared game assets folder")
    args = parser.parse_args()
    import_villagers(args.source, args.assets)
