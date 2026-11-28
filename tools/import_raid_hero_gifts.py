"""Translate the 1.14.4 gift loot tables to the native LCE loot-pool schema."""
import json
import re
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
output = ['// Generated from Mojang Java 1.14.4 hero_of_the_village loot tables.']
names = []
colors = 'white orange magenta light_blue yellow lime pink gray light_gray cyan purple blue brown green red black'.split()
aliases = {'lapis_lazuli': ('dye',4), 'cod': ('fish',0), 'salmon': ('fish',1)}
aliases.update({c+'_wool': ('wool',i) for i,c in enumerate(colors)})
with zipfile.ZipFile(root/'build/tu80-grindstone/java/1.14.4.jar') as jar:
    for path in sorted(jar.namelist()):
        if '/hero_of_the_village/' not in path or not path.endswith('.json'): continue
        name = Path(path).stem.removesuffix('_gift')
        names.append(name)
        pools = json.loads(jar.read(path))['pools']
        assert len(pools)==1 and pools[0]['rolls']==1
        output.append(f'static const GiftEntry {name}Gifts[] = {{')
        for entry in pools[0]['entries']:
            assert entry['type']=='minecraft:item'
            item=entry['name'].removeprefix('minecraft:')
            item,data=aliases.get(item,(item,0))
            low=high=1; potion='nullptr'
            for fn in entry.get('functions',[]):
                if fn['function']=='minecraft:set_count':
                    low=int(fn['count']['min']); high=int(fn['count']['max'])
                elif fn['function']=='minecraft:set_nbt':
                    match=re.fullmatch(r'\{Potion:"([^"]+)"\}',fn['tag']); assert match
                    potion='L"'+match[1]+'"'
                else: raise ValueError(fn)
            output.append(f'    {{L"minecraft:{item}", {data}, {entry.get("weight",1)}, {low}, {high}, {potion}}},')
        output.append('};')
output.append('static const GiftDefinition giftDefinitions[] = {')
for name in names: output.append(f'    {{L"{name}", {name}Gifts, sizeof({name}Gifts)/sizeof(GiftEntry)}},')
output.append('};')
(root/'source/game/world/generation/RaidHeroGiftData.inc').write_text('\n'.join(output)+'\n',encoding='utf-8')
print(f'Imported {len(names)} profession gift tables')
