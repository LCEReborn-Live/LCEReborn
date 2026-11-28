"""Prepare PC cat skins using the collar mask in the local TU80 textures."""

import argparse
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="TU80 mob/cat texture directory")
    args = parser.parse_args()
    for template in sorted(args.directory.glob("*_tame.tga")):
        base = template.with_name(template.stem.removesuffix("_tame") + ".png")
        with Image.open(base) as source, Image.open(template) as mask:
            skin = source.convert("RGBA")
            if skin.size != mask.size:
                raise ValueError(f"Different texture sizes for {base} and {template}")
            pixels = skin.load()
            collar = mask.convert("RGBA").load()
            for y in range(skin.height):
                for x in range(skin.width):
                    shade, _, _, material = collar[x, y]
                    if material == 255:
                        pixels[x, y] = (min(255, shade + 20), shade // 5, shade // 5, 255)
            skin.save(template.with_suffix(".png"), optimize=True)


if __name__ == "__main__":
    main()
