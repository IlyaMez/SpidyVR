"""The game's own texts, read from its asset archives with no game running, and Spidy's VR wording for them.

    python tools/game_texts.py dump [--out reports/game-texts]   the English table: every text with its tag and the
                                                                 hash the game's text lookup takes; the tutorial,
                                                                 help and prompt texts as a TSV
    python tools/game_texts.py table                             src/vr_texts.tsv into src/vr_texts_table.inc, each
                                                                 tag's English text and hash taken from the game
    python tools/game_texts.py check                             the table against the installed game: every English
                                                                 text it replaces still the game's own

How the archives are laid out (docs/REFERENCE.md, "The game's texts"): asset_archive/toc is zlib-compressed DAT1, its
asset ids CRC-64 of the asset's path; the gNNsNNN archives are DSAR, 256 KB blocks of raw LZ4; the localization asset
is DAT1 with the tags, the texts and their hashes in sections of its own.
"""
import argparse
import csv
import json
import os
import pathlib
import re
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
GAME = pathlib.Path(r"C:\Program Files (x86)\Steam\steamapps\common\Marvel's Spider-Man Remastered")
SOURCE = ROOT/'src/vr_texts.tsv'
TABLE = ROOT/'src/vr_texts_table.inc'
LOCALIZATION = 'localization/localization_all.localization'
M64 = 0xffffffffffffffff

# DAT1 section tags.
TOC_IDS, TOC_SIZES, TOC_OFFSETS, TOC_ARCHIVES = 0x506D7B8A, 0x65BCF461, 0xDCD720B5, 0x398ABFF0
LOC_COUNT, LOC_HASHES, LOC_TAGS, LOC_TAG_AT, LOC_TEXTS, LOC_TEXT_AT = (
    0xD540A903, 0x06A58050, 0x4D73CEBD, 0xA4EA55B2, 0x70A382B8, 0xF80DEEB4)


def _crc64_table():
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0xC96C5795D7870F42 if c & 1 else c >> 1
        table.append(c)
    return table


CRC64 = _crc64_table()


def asset_id(path):
    """The id the toc lists an asset by: CRC-64/ECMA (reflected), seeded with its own polynomial, of the path in
    lower case with forward slashes, shifted right by two under the top bit."""
    c = 0xC96C5795D7870F42
    for byte in path.lower().replace('\\', '/').encode():
        c = CRC64[(c ^ byte) & 0xff] ^ (c >> 8)
    return (c >> 2) | 0x8000000000000000


def sections(data):
    """A DAT1's sections by tag: (offset, size)."""
    magic, _, _, count = struct.unpack_from('<4I', data)
    if magic != 0x44415431:
        raise ValueError('not DAT1')
    out = {}
    for i in range(count):
        tag, offset, size = struct.unpack_from('<3I', data, 16 + 12*i)
        out[tag] = (offset, size)
    return out


def lz4_block(src, size):
    """A raw LZ4 block, as DSAR keeps its blocks."""
    out = bytearray()
    i, n = 0, len(src)
    while i < n:
        token = src[i]
        i += 1
        run = token >> 4
        if run == 15:
            while True:
                b = src[i]
                i += 1
                run += b
                if b != 255:
                    break
        out += src[i:i + run]
        i += run
        if i >= n or len(out) >= size:
            break
        back = src[i] | (src[i + 1] << 8)
        i += 2
        length = token & 15
        if length == 15:
            while True:
                b = src[i]
                i += 1
                length += b
                if b != 255:
                    break
        length += 4
        start = len(out) - back
        if back >= length:
            out += out[start:start + length]
        else:
            for k in range(length):
                out.append(out[start + k])
    return bytes(out)


class Archives:
    def __init__(self, game=GAME):
        self.folder = pathlib.Path(game)/'asset_archive'
        raw = (self.folder/'toc').read_bytes()
        magic = struct.unpack_from('<I', raw)[0]
        if magic != 0x77AF12AF:
            raise ValueError(f'{self.folder/"toc"}: not the game\'s toc')
        self.toc = zlib.decompressobj().decompress(raw[8:])
        sec = sections(self.toc)
        o, s = sec[TOC_IDS]
        self.ids = struct.unpack_from(f'<{s//8}Q', self.toc, o)
        self.sizes = sec[TOC_SIZES][0]
        self.offsets = sec[TOC_OFFSETS][0]
        o, s = sec[TOC_ARCHIVES]
        names = self.toc[o:o + s]
        self.names = [names[m.start():m.start() + 16].split(b'\0')[0].decode()
                      for m in re.finditer(rb'[a-z]\d\ds\d\d\d', names)]
        self.blocks = {}

    def copies(self, path):
        """Every copy the toc lists of an asset (one a language for the texts): (archive, offset, size)."""
        want = asset_id(path)
        out = []
        for i, x in enumerate(self.ids):
            if x == want:
                size = struct.unpack_from('<I', self.toc, self.sizes + 12*i + 4)[0]
                archive, offset = struct.unpack_from('<2I', self.toc, self.offsets + 8*i)
                out.append((self.names[archive], offset, size))
        return out

    def read(self, archive, offset, size):
        path = self.folder/archive
        with open(path, 'rb') as f:
            if archive not in self.blocks:
                head = f.read(32)
                if head[:4] != b'DSAR':
                    raise ValueError(f'{path}: not DSAR')
                count = struct.unpack_from('<I', head, 8)[0]
                raw = f.read(32*count)
                self.blocks[archive] = [struct.unpack_from('<QQIIB', raw, 32*i) for i in range(count)]
            out = bytearray()
            for real, stored, real_size, stored_size, kind in self.blocks[archive]:
                if real + real_size <= offset or real >= offset + size:
                    continue
                f.seek(stored)
                data = f.read(stored_size)
                if kind == 3:
                    data = lz4_block(data, real_size)
                elif kind != 0:
                    raise ValueError(f'{path}: block kind {kind}')
                out += data[max(offset, real) - real:min(offset + size, real + real_size) - real]
        return bytes(out)


def texts(game=GAME):
    """The English table: a list of {hash, tag, text}, in the asset's own order."""
    archives = Archives(game)
    copies = archives.copies(LOCALIZATION)
    if not copies:
        raise ValueError(f'{LOCALIZATION} is not in the toc')
    for copy in copies:
        rows = parse_localization(archives.read(*copy)[36:])
        if any(r['tag'] == 'PROMPT_INTERACT' and r['text'] == '[BTN_Y] Interact' for r in rows):
            return rows
    raise ValueError('no English copy of the texts')


def parse_localization(data):
    sec = sections(data)
    count = struct.unpack_from('<I', data, sec[LOC_COUNT][0])[0]

    def words(tag):
        return struct.unpack_from(f'<{count}I', data, sec[tag][0])

    def blob(tag):
        o, s = sec[tag]
        return data[o:o + s]

    tags, values = blob(LOC_TAGS), blob(LOC_TEXTS)

    def at(b, i):
        return b[i:b.index(b'\0', i)].decode('utf-8')

    return [dict(hash=h, tag=at(tags, t), text=at(values, v))
            for h, t, v in zip(words(LOC_HASHES), words(LOC_TAG_AT), words(LOC_TEXT_AT))]


CONTROL = re.compile(r'\[(BTN|ACTION|STICK|DPAD|TUTORIAL_AIM|TECHBOX)[A-Z0-9_]*\]')
FAMILIES = ('TUT_', 'HELP_', 'HINT_', 'TIPS_', 'MOVES_', 'CTRL_', 'PROMPT_', 'HUD_MSG', 'UI_HELP', 'PS5_HELP',
            'SKILL_', 'CIRCUIT_HELP', 'SPECTOGRAPH_TUTORIAL', 'STAKEOUT_TUT', 'BUTTON_', 'CONTROLS_')


def dump(rows, out):
    out.mkdir(parents=True, exist_ok=True)
    (out/'localization-en.json').write_text(
        json.dumps([dict(r, hash=f'{r["hash"]:08x}') for r in rows], ensure_ascii=False, indent=0), 'utf-8')
    kept = 0
    with open(out/'tutorial-texts.tsv', 'w', encoding='utf-8', newline='') as f:
        f.write('hash\ttag\tfamily\tnames_control\tkind\ttext\n')
        for r in rows:
            family = next((x.rstrip('_') for x in FAMILIES if r['tag'].startswith(x)), 'other')
            names = bool(CONTROL.search(r['text']))
            if (family == 'other' and not names) or r['tag'].endswith('_MKB'):
                continue
            kind = 'card' if 'to continue' in r['text'] else ''
            text = r['text'].replace('\t', ' ').replace('\r', '').replace('\n', '\\n')
            f.write(f'{r["hash"]:08x}\t{r["tag"]}\t{family}\t{int(names)}\t{kind}\t{text}\n')
            kept += 1
    return kept


def read_source(path=SOURCE):
    """src/vr_texts.tsv: [(tag, VR text with the game's markup)], in the file's order."""
    out = []
    for n, line in enumerate(path.read_text('utf-8').splitlines(), 1):
        if not line.strip() or line.startswith('#'):
            continue
        tag, sep, words = line.partition('\t')
        if not sep or not tag or not words.strip() or '\t' in words:
            raise ValueError(f'{path.name}:{n}: want TAG<tab>text')
        out.append((tag, markup(words.strip())))
    tags = [t for t, _ in out]
    for t in set(tags):
        if tags.count(t) > 1:
            raise ValueError(f'{path.name}: {t} twice')
    return out


def markup(words):
    """*word* as the game's emphasis."""
    if words.count('*') % 2:
        raise ValueError(f'unpaired * in {words!r}')
    return re.sub(r'\*([^*]+)\*', r"<span class='emphasis'>\1</span>", words)


def literal(s, indent, width=110):
    """A C++ string literal, split over lines to stay within `width`; anything but printable ASCII as octal."""
    parts, cur = [], ''
    for ch in s:
        if ch in '"\\':
            piece = '\\' + ch
        elif ' ' <= ch <= '~':
            piece = ch
        else:
            piece = ''.join(f'\\{b:03o}' for b in ch.encode('utf-8'))
        if len(cur) + len(piece) > width - indent - 4 and cur:
            parts.append(cur)
            cur = ''
        cur += piece
    parts.append(cur)
    return ('\n' + ' '*indent).join(f'"{p}"' for p in parts)


def table(rows, source):
    by_tag = {r['tag']: r for r in rows}
    missing = [t for t, _ in source if t not in by_tag]
    if missing:
        raise ValueError(f'not in the game\'s texts: {", ".join(missing)}')
    entries = sorted(((by_tag[t]['hash'], t, by_tag[t]['text'], vr) for t, vr in source), key=lambda e: e[0])
    hashes = [e[0] for e in entries]
    if len(set(hashes)) != len(hashes):
        raise ValueError('two tags with one hash')
    lines = ['// Generated by tools/game_texts.py table from src/vr_texts.tsv and the game\'s own English texts:',
             '// edit the TSV, not this. {hash the game\'s lookup takes, tag, its English text, the VR wording}.']
    for h, tag, english, vr in entries:
        lines.append(f'{{0x{h:08x}, "{tag}",')
        lines.append(f' {literal(english, 1)},')
        lines.append(f' {literal(vr, 1)}}},')
    return '\n'.join(lines) + '\n', entries


def parse_table(path=TABLE):
    """The generated table back: [(hash, tag, english, vr)] (for checks with no game)."""
    text = path.read_text('utf-8')
    out = []
    for m in re.finditer(r'\{0x([0-9a-f]{8}), "([A-Z0-9_ ]+)",\n((?: "(?:[^"\\]|\\.)*"\n?)+),\n((?: "(?:[^"\\]|\\.)*"\n?)+)\},',
                         text):
        def value(block):
            raw = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', block))
            return re.sub(rb'\\([0-7]{3}|.)', lambda e: bytes([int(e.group(1), 8)]) if len(e.group(1)) == 3
                          else e.group(1), raw.encode('latin-1')).decode('utf-8')
        out.append((int(m.group(1), 16), m.group(2), value(m.group(3)), value(m.group(4))))
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('command', choices=['dump', 'table', 'check'])
    p.add_argument('--game', type=pathlib.Path, default=GAME, help='the game\'s folder (Steam\'s by default)')
    p.add_argument('--out', type=pathlib.Path, default=ROOT/'reports/game-texts')
    a = p.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    rows = texts(a.game)
    if a.command == 'dump':
        kept = dump(rows, a.out)
        print(f'{len(rows)} texts; {kept} tutorial, help and prompt texts in {a.out/"tutorial-texts.tsv"}')
        return 0
    if a.command == 'table':
        text, entries = table(rows, read_source())
        TABLE.write_text(text, 'utf-8', newline='\n')
        print(f'{len(entries)} VR texts in {TABLE.relative_to(ROOT)}')
        return 0
    by_hash = {r['hash']: r for r in rows}
    stale = [(t, e) for h, t, e, _ in parse_table() if by_hash.get(h, {}).get('text') != e]
    for tag, english in stale:
        print(f'{tag}: the game no longer says {english!r}')
    source = dict(read_source())
    generated = {t: vr for _, t, _, vr in parse_table()}
    if source != generated:
        print('src/vr_texts_table.inc is not src/vr_texts.tsv: run python tools/game_texts.py table')
        return 1
    print(f'{len(generated)} VR texts; {len(stale)} whose English the game changed')
    return 1 if stale else 0


if __name__ == '__main__':
    sys.exit(main())
