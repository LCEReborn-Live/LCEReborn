"""Compile the engine's checked village export into native C++ geometry.

The export is produced by tools/tests/export_native_villages.hpp using the
engine's block registry. Original NBT and intermediate files are build inputs
only. Generated C++ contains typed states, geometry, connectors and data
factories for the existing entity/block-entity APIs, never serialized NBT.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
from zipfile import ZipFile
from village_native_data import read, plain


def literal(value):
    return 'L' + json.dumps(value, ensure_ascii=True)


def data_expression(tag):
    kind, value = tag
    classes = {1: 'ByteTag', 2: 'ShortTag', 3: 'IntTag', 4: 'LongTag',
               5: 'FloatTag', 6: 'DoubleTag', 8: 'StringTag'}
    if kind in classes:
        if kind == 8:
            value = literal(value)
        elif kind == 4:
            value = str(value) + 'LL'
        elif kind == 1:
            value = str(value & 255)
        else:
            value = repr(value)
        return f'new {classes[kind]}({value})'
    if kind == 10:
        rows = ['[] { auto* t = new CompoundTag();']
        for key, item in sorted(value.items()):
            rows.append(f't->put({literal(key)}, {data_expression(item)});')
        return ' '.join(rows) + ' return t; }()'
    if kind == 9:
        rows = [f'[] {{ auto* t = new ListTag({value[0][0] if value else 0});']
        rows.extend(f't->add({data_expression(item)});' for item in value)
        return ' '.join(rows) + ' return t; }()'
    if kind in (7, 11):
        scalar = 'unsigned char' if kind == 7 else 'int'
        cls = 'ByteArrayTag' if kind == 7 else 'IntArrayTag'
        rows = [f'[] {{ arrayWithLength<{scalar}> a({len(value)}, true);']
        rows += [f'a.data[{i}] = {v & 255 if kind == 7 else v};' for i, v in enumerate(value)]
        return ' '.join(rows) + f' return new {cls}(a{", true" if kind == 7 else ""}); }}()'
    raise ValueError(f'Unsupported native tag type {kind}')


def weight_after(lines, index):
    for i in range(index + 1, len(lines)):
        if 'java/lang/Integer.valueOf:' in lines[i]:
            instruction = lines[i - 1].split(':', 1)[1].strip()
            if instruction.startswith('iconst_'):
                return int(instruction[len('iconst_'):])
            if instruction.startswith(('bipush', 'sipush')):
                return int(instruction.split()[1])
            raise ValueError('Unsupported weight instruction: ' + instruction)
    raise ValueError('Missing pool weight')


def read_pools(folder):
    pools = {}
    feature_names = {'cbn.A': 'oak_tree', 'cbn.G': 'plain_flower', 'cbn.K': 'taiga_grass',
                     'cbn.N': 'cactus', 'cbn.aM': 'sweet_berry_bush', 'cbn.ag': 'hay_pile',
                     'cbn.ah': 'snow_pile', 'cbn.ai': 'ice_pile', 'cbn.aj': 'melon_pile',
                     'cbn.ak': 'pumpkin_pile', 'cbn.v': 'pine_tree', 'cbn.x': 'acacia_tree',
                     'cbn.y': 'spruce_tree'}
    for file in sorted(folder.glob('*.class.txt')):
        lines = file.read_text().splitlines()
        active = None
        for line in lines:
            if re.search(r'// class cft$', line):
                active = []
            if active is not None:
                active.append(line)
            if active is None or 'Method cft."<init>"' not in line:
                continue
            strings = [re.search(r'// String (.*)', row).group(1) for row in active if '// String ' in row]
            if len(strings) < 2:
                raise ValueError('Pool lacks its name or fallback')
            pool = {'id': 'minecraft:' + strings[0], 'fallback': 'minecraft:' + strings[1],
                    'terrain': any('Field cft$a.a:' in row for row in active), 'entries': []}
            for i, row in enumerate(active):
                if '// String village/' in row and i > 10:
                    path = row.split('// String ', 1)[1]
                    # The first two strings are pool names, not piece entries.
                    if not any('class cfq' in r for r in active[max(0, i-3):i]):
                        continue
                    constructor = next(j for j in range(i + 1, len(active)) if 'Method cfq."<init>"' in active[j])
                    pool['entries'].append({'piece': 'NativeVillage/v1/' + path.removeprefix('village/'),
                                            'weight': weight_after(active, constructor)})
                elif 'Field cfl.a:' in row:
                    pool['entries'].append({'piece': '', 'weight': weight_after(active, i)})
                elif '// class cfm' in row:
                    feature = next(r.split('// Field ', 1)[1].split(':')[0]
                                   for r in active[i:] if '// Field cbn.' in r)
                    pool['entries'].append({'piece': 'NativeFeature/v1/' + feature_names[feature],
                                            'weight': weight_after(active, i)})
            if pool['id'] in pools and pools[pool['id']] != pool:
                raise ValueError('Conflicting pool registrations: ' + pool['id'])
            pools[pool['id']] = pool
            active = None
    return list(pools.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-export', required=True, type=Path)
    parser.add_argument('--registry-bytecode', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--jar', required=True, type=Path, help='Local 1.14.4 client, for native loot tables')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    files = sorted(args.native_export.rglob('*.nbt'))
    if len(files) != 482:
        raise ValueError(f'Expected all 482 converted templates, found {len(files)}')
    args.output.mkdir(parents=True, exist_ok=True)
    groups, manifest, ids = {}, [], set()
    for index, file in enumerate(files):
        relative = file.relative_to(args.native_export)
        source = args.source / relative
        typed = read(file)
        data = plain(typed)
        group = relative.parts[0]
        rows, names = groups.setdefault(group, ([], []))
        name = 'structure_' + str(index)
        piece_id = 'NativeVillage/v1/' + relative.with_suffix('').as_posix()
        ids.add(piece_id)
        factories = {}

        def factory(tag):
            if tag is None:
                return 'nullptr'
            key = repr(tag)
            if key not in factories:
                function = name + '_data_' + str(len(factories))
                factories[key] = function
                rows.append(f'static CompoundTag* {function}() {{ return {data_expression(tag)}; }}')
            return factories[key]

        palette = list(dict.fromkeys((b['id'], b['data'], b['extraId'], b['extraData']) for b in data['blocks']))
        palette_ids = {value: i for i, value in enumerate(palette)}
        block_rows = []
        for block in typed[1]['blocks'][1]:
            b = plain(block)
            block_rows.append('{' + ','.join(map(str, b['pos'] + [palette_ids[b['id'], b['data'], b['extraId'], b['extraData']]]))
                              + ',' + factory(block[1].get('nbt')) + '}')
        entity_rows = []
        for entity in typed[1]['entities'][1]:
            e = plain(entity)
            entity_rows.append('{' + ','.join(map(repr, e['pos'] + e['blockPos'])) + ',' + factory(entity[1]['nbt']) + '}')
        if palette:
            rows.append(f'static const State {name}_palette[] = {{' + ','.join('{%d,%d,%d,%d}' % p for p in palette) + '};')
        if block_rows:
            rows.append(f'static const Block {name}_blocks[] = {{\n' + ',\n'.join(block_rows) + '\n};')
        connectors = data['connectors']
        if connectors:
            rows.append(f'static const Connector {name}_connectors[] = {{')
            for c in connectors:
                rows.append('{' + ','.join(map(str, c['pos'])) + ','
                            + ','.join(literal(c[k]) for k in ('facing', 'name', 'target', 'pool')) + '},')
            rows.append('};')
        if entity_rows:
            rows.append(f'static const Entity {name}_entities[] = {{' + ','.join(entity_rows) + '};')
        values = [literal(piece_id), *map(str, data['size']), name + '_palette' if palette else 'nullptr']
        for suffix, count in [('blocks', len(block_rows)), ('connectors', len(connectors)), ('entities', len(entity_rows))]:
            values += [name + '_' + suffix if count else 'nullptr', str(count)]
        rows.append(f'static const Definition {name} = {{' + ','.join(values) + '};')
        names.append('&' + name)
        manifest.append({'id': piece_id, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                         'blocks': len(block_rows), 'connectors': len(connectors), 'entities': len(entity_rows)})
    includes = ['CompoundTag', 'ListTag', 'ByteTag', 'ShortTag', 'IntTag', 'LongTag', 'FloatTag', 'DoubleTag',
                'StringTag', 'ByteArrayTag', 'IntArrayTag']
    preamble = '// Generated by tools/compile_native_villages.py. Native geometry, not serialized NBT.\n'
    preamble += '#include "game/world/generation/NativeVillageRegistry.hpp"\n'
    preamble += ''.join(f'#include "game/data/{name}.hpp"\n' for name in includes)
    for group, (rows, names) in groups.items():
        text = preamble + 'namespace NativeVillageRegistry {\n' + '\n'.join(rows)
        text += f'\nconst std::vector<const Definition*>& {group}Definitions() {{\n'
        text += 'static const std::vector<const Definition*> values = {' + ','.join(names) + '}; return values; }\n}\n'
        (args.output / (group + '.cpp')).write_text(text, encoding='utf-8')
    pools = read_pools(args.registry_bytecode)
    for pool in pools:
        for entry in pool['entries']:
            if entry['piece'] and not entry['piece'].startswith('NativeFeature/') and entry['piece'] not in ids:
                raise ValueError('Pool references missing template: ' + entry['piece'])
    rows = [preamble, 'namespace NativeVillageRegistry {']
    for group in groups:
        rows.append(f'const std::vector<const Definition*>& {group}Definitions();')
    rows.append('const std::vector<const Definition*>& definitions() { static const auto result = [] {')
    rows.append('std::vector<const Definition*> values;')
    for group in groups:
        rows.append(f'for (auto* value : {group}Definitions()) values.push_back(value);')
    rows.append('return values; }(); return result; }')
    for index, pool in enumerate(pools):
        entries = ','.join('{' + literal(e['piece']) + ',' + str(e['weight']) + '}' for e in pool['entries'])
        rows.append(f'static const PoolEntry pool_{index}_entries[] = {{{entries}}};')
        rows.append(f'static const Pool pool_{index} = {{' + literal(pool['id']) + ',' + literal(pool['fallback'])
                    + ',' + str(pool['terrain']).lower() + f',pool_{index}_entries,{len(pool["entries"])}}};')
    rows.append('const std::vector<const Pool*>& pools() { static const std::vector<const Pool*> result = {'
                + ','.join('&pool_' + str(i) for i in range(len(pools))) + '}; return result; }\n}')
    (args.output / 'Registry.cpp').write_text('\n'.join(rows), encoding='utf-8')
    compile_loot(args.jar, args.output)
    (args.output / 'provenance.json').write_text(json.dumps({'templates': manifest, 'pools': pools,
        'registry_source_sha256': hashlib.sha256(args.jar.read_bytes()).hexdigest()}, indent=2) + '\n')
    print(f'Compiled {len(files)} native definitions, {len(pools)} pools, {sum(m["blocks"] for m in manifest)} blocks')
    print('Feature references:', sorted({e['piece'] for p in pools for e in p['entries'] if e['piece'].startswith('NativeFeature/')}))


def compile_loot(jar, output):
    aliases = {'cod': ('fish', 0), 'salmon': ('fish', 1), 'green_dye': ('dye', 2),
               'yellow_dye': ('dye', 11), 'lapis_lazuli': ('dye', 4),
               'oak_sapling': ('sapling', 0), 'spruce_sapling': ('sapling', 1), 'acacia_sapling': ('sapling', 4),
               'white_wool': ('wool', 0), 'gray_wool': ('wool', 7), 'light_gray_wool': ('wool', 8),
               'black_wool': ('wool', 15), 'brown_wool': ('wool', 12), 'spruce_log': ('log', 1),
               'dandelion': ('yellow_flower', 0), 'poppy': ('red_flower', 0), 'dead_bush': ('deadbush', 0),
               'grass': ('tallgrass', 1), 'fern': ('tallgrass', 2), 'tall_grass': ('double_plant', 2),
               'large_fern': ('double_plant', 3), 'snow_block': ('snow', 0), 'stone_bricks': ('stonebrick', 0)}
    rows = ['// Generated native loot entries from the local 1.14.4 client.',
            '#include "game/world/generation/NativeVillageRegistry.hpp"',
            '#include "game/world/generation/NativeVillageLoot.hpp"',
            'namespace NativeVillageRegistry {']
    tables = []
    with ZipFile(jar) as archive:
        for filename in sorted(archive.namelist()):
            if not filename.startswith('data/minecraft/loot_tables/chests/village/') or not filename.endswith('.json'):
                continue
            data = json.loads(archive.read(filename))
            if len(data['pools']) != 1: raise ValueError('Multiple loot pools need explicit conversion')
            pool = data['pools'][0]
            if pool.get('conditions'): raise ValueError('Unsupported loot condition')
            entries = []
            for entry in pool['entries']:
                if entry['type'] != 'minecraft:item' or entry.get('conditions'): raise ValueError('Unsupported loot entry')
                name, variant = aliases.get(entry['name'].removeprefix('minecraft:'),
                                             (entry['name'].removeprefix('minecraft:'), 0))
                count = 1
                for function in entry.get('functions', []):
                    if function['function'] != 'minecraft:set_count': raise ValueError('Unsupported loot function')
                    count = function['count']
                low, high = (count['min'], count['max']) if isinstance(count, dict) else (count, count)
                entries.append('{' + literal('minecraft:' + name) + f',{variant},{entry.get("weight", 1)},{int(low)},{int(high)}}}')
            name = 'loot_' + str(len(tables))
            rows.append(f'static const LootEntry {name}[] = {{' + ','.join(entries) + '};')
            rolls = pool['rolls']
            low, high = (rolls['min'], rolls['max']) if isinstance(rolls, dict) else (rolls, rolls)
            location = 'minecraft:' + filename.removeprefix('data/minecraft/loot_tables/').removesuffix('.json')
            tables.append('{' + literal(location) + f',{int(low)},{int(high)},{name},{len(entries)}}}')
    rows.append('const std::vector<LootDefinition>& lootDefinitions() { static const std::vector<LootDefinition> result = {'
                + ','.join(tables) + '}; return result; }\n}')
    (output / 'Loot.cpp').write_text('\n'.join(rows), encoding='utf-8')


if __name__ == '__main__':
    main()
