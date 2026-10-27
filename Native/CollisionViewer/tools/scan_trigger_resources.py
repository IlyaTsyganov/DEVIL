"""Read-only inventory of Eternal .resources; write metadata only to workspace.

Container layout reference: brunoanc/EternalResourceExtractor, extract.cpp.
No game files are modified. No archive-provided path is used for output writes.
"""
import argparse
import collections
import ctypes
import json
import mmap
import pathlib
import re
import struct

def u32(data, offset): return struct.unpack_from('<I', data, offset)[0]
def u64(data, offset): return struct.unpack_from('<Q', data, offset)[0]

def entries(path):
    with path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        version = u32(data, 4)
        start = 36 if version >= 13 else 32
        count, dummy_count = u32(data, start), u32(data, start + 8)
        names_at, info_at, dummy_at = (u64(data, start + offset) for offset in (32, 48, 64))
        dummy_at += dummy_count * 4
        name_count = u64(data, names_at)
        if count > 2_000_000 or name_count > 4_000_000 or info_at + count * 144 > len(data):
            raise ValueError('Invalid resource table bounds: ' + str(path))
        string_base = names_at + 8 + name_count * 8
        names = []
        for index in range(name_count):
            at = string_base + u64(data, names_at + 8 + index * 8)
            end = data.find(b'\0', at, min(len(data), at + 2048))
            if end < at: raise ValueError('Unterminated resource name')
            names.append(data[at:end].decode('utf-8', errors='replace'))
        for index in range(count):
            row = info_at + index * 144
            ids_at = dummy_at + u64(data, row + 32) * 8
            kind, name = u64(data, ids_at), u64(data, ids_at + 8)
            offset, compressed, size = struct.unpack_from('<3Q', data, row + 56)
            if kind >= len(names) or name >= len(names) or offset + compressed > len(data):
                raise ValueError(f'Invalid file entry {path.name} index={index} names={len(names)} ids={kind},{name} offset={offset} z={compressed} size={size} total={len(data)} header={version},{count},{names_at},{info_at},{dummy_at}')
            yield {'container': str(path), 'name': names[name], 'kind': names[kind],
                   'offset': offset, 'compressed': compressed, 'size': size, 'flags': u64(data, row + 112)}

class Decoder:
    def __init__(self, game):
        self.library = ctypes.WinDLL(str(game / 'oo2core_8_win64.dll'))
        self.decode = self.library.OodleLZ_Decompress
        self.decode.restype = ctypes.c_ssize_t
        self.decode.argtypes = [ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_ssize_t,
                               ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_ssize_t,
                               ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_int]
    def read(self, row):
        if not 0 <= row['size'] <= 128_000_000: raise ValueError('Oversized decl')
        with open(row['container'], 'rb') as source:
            source.seek(row['offset']); data = source.read(row['compressed'])
        if row['size'] == row['compressed']: return data
        if row['flags'] & 4: data = data[12:]
        output = ctypes.create_string_buffer(row['size'] + 64)
        result = self.decode(data, len(data), output, row['size'], 1, 0, 0, None, 0, None, None, None, 0, 3)
        if result != row['size']: raise ValueError('Could not decompress ' + row['name'])
        return output.raw[:result]
    def map_text(self, row):
        data = self.read(row)
        if data.startswith(b'Version '): return data.decode('utf-8')
        if len(data) < 16: raise ValueError('Short map file')
        size, compressed = struct.unpack_from('<QQ', data)
        if size > 256_000_000 or compressed != len(data) - 16: raise ValueError('Invalid compfile bounds')
        output = ctypes.create_string_buffer(size + 64)
        result = self.decode(data[16:], compressed, output, size, 1, 0, 0, None, 0, None, None, None, 0, 3)
        if result != size: raise ValueError('Map decompression failed')
        return output.raw[:result].decode('utf-8')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('game', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--samples', action='store_true')
    parser.add_argument('--catalogue', action='store_true')
    parser.add_argument('--maps', action='store_true')
    options = parser.parse_args()
    options.output.mkdir(parents=True, exist_ok=True)
    selected = []
    containers = sorted((options.game / 'base').rglob('*.resources'))
    kinds = collections.Counter()
    for container in containers:
        for row in entries(container):
            kinds[row['kind']] += 1
            name = row['name'].lower()
            if 'entitydef' in row['kind'].lower() or '/entitydef/' in name or name.endswith('.entities'):
                selected.append(row)
    (options.output / 'resource-index.json').write_text(json.dumps(selected, ensure_ascii=False), encoding='utf-8')
    definitions = [row for row in selected if not row['name'].endswith('.entities')]
    candidates = sorted({row['name'] for row in definitions if '/trigger/' in row['name'].lower()})
    print(json.dumps({'containers': len(containers), 'entitydef_entries': len(definitions),
                      'unique_entitydefs': len({row['name'] for row in definitions}),
                      'trigger_named_definitions': len(candidates), 'sample': candidates[:12],
                      'kinds': kinds.most_common(8)}, ensure_ascii=False))
    if options.samples:
        decoder = Decoder(options.game)
        for target in ('trigger/props/pickup.decl', 'trigger/interact/interact.decl', 'trigger/ai/archvile_immolate.decl', 'encounter/trigger/commit.decl'):
            row = next((row for row in definitions if row['name'].endswith('/' + target)), None)
            if row: print(row['name'], decoder.read(row).decode('utf-8', errors='replace')[:1800])
    if options.catalogue:
        decoder = Decoder(options.game)
        declarations = collections.defaultdict(set)
        sources = collections.defaultdict(set)
        missing = []
        for row in definitions:
            body = decoder.read(row).decode('utf-8', errors='replace')
            match = re.search(r'(?m)^\s*class\s*=\s*"([^"\r\n]+)"', body)
            parent = re.search(r'(?m)^\s*inherit\s*=\s*"([^"\r\n]+)"', body)
            if not match: missing.append(row['name'])
            name = row['name'].replace('\\', '/').lower()
            marker = 'generated/decls/entitydef/'
            if not name.startswith(marker) or not name.endswith('.decl'): continue
            definition = name[len(marker):-5]
            declarations[definition].add((match.group(1) if match else None, parent.group(1).lower() if parent else None))
            sources[definition].add(str(pathlib.Path(row['container']).relative_to(options.game / 'base')).replace('\\', '/'))
        resolved = {}
        def resolve(name, visiting=()):
            if name in resolved: return resolved[name]
            if name in visiting or len(visiting) > 64: return set()
            values = set()
            for cls, parent in declarations.get(name, ()):
                if cls: values.add(cls)
                elif parent: values.update(resolve(parent, visiting + (name,)))
            resolved[name] = values
            return values
        classes = {name: {cls for cls in resolve(name) if re.fullmatch(r'id(?:Trigger(?:_\w+)?|EncounterTrigger_\w+)', cls)} for name in declarations}
        rows = [{'definition': name, 'classes': sorted(values), 'containers': sorted(sources[name])} for name, values in sorted(classes.items()) if values]
        (options.output / 'trigger-definitions.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
        groups = sorted({(cls, row['definition'].rsplit('/', 1)[0] + '/') for row in rows if '/' in row['definition'] for cls in row['classes']})
        map_count = map_triggers = 0
        map_groups = set()
        if options.maps:
            maps = [row for row in selected if row['name'].endswith('.entities')]
            for row in maps:
                text = decoder.map_text(row)
                matches = list(re.finditer(r'(?m)^\s*entityDef\s+("(?:\\.|[^"\\])*"|[^\s{}]+)\s*\{', text))
                for index, match in enumerate(matches):
                    name = match.group(1).strip('"').replace('\\', '/').lower()
                    end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
                    header = text[match.end():min(end, match.end() + 8192)].split('edit =', 1)[0]
                    cls = re.search(r'(?m)^\s*class\s*=\s*"([^"\r\n]+)"', header)
                    parent = re.search(r'(?m)^\s*inherit\s*=\s*"([^"\r\n]+)"', header)
                    values = {cls.group(1)} if cls else resolve(parent.group(1).lower()) if parent else set()
                    values = {cls for cls in values if re.fullmatch(r'id(?:Trigger(?:_\w+)?|EncounterTrigger_\w+)', cls)}
                    if values: map_triggers += 1
                    if '/' in name:
                        for cls in values: map_groups.add((cls, name.rsplit('/', 1)[0] + '/'))
                map_count += 1
            groups = sorted(set(groups) | map_groups)
        (options.output / 'trigger-paths.tsv').write_text('#DEVIL_TRIGGER_PATHS_V1\n' + ''.join(cls + '\t' + folder + '\n' for cls, folder in groups), encoding='utf-8')
        report = {'containers': len(containers), 'entitydef_entries': len(definitions), 'unique_entitydefs': len({row['name'] for row in definitions}),
                  'definitions_without_class': len(set(missing)), 'unresolved_definitions': sum(not resolve(name) for name in declarations), 'trigger_definitions': len(rows), 'classes': sorted({cls for row in rows for cls in row['classes']}),
                  'folder_groups': len(groups), 'groups': groups, 'ambiguous_class_definitions': [row['definition'] for row in rows if len(row['classes']) > 1]}
        report.update(map_files=map_count, map_trigger_instances=map_triggers, map_folder_groups=sorted(map_groups))
        (options.output / 'summary.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps(report, ensure_ascii=False))

if __name__ == '__main__': main()
