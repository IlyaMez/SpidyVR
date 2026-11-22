"""The game's texts read from its archives, and Spidy's VR wording table (tools/game_texts.py); no game needed."""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import game_texts


def dat1(sections):
    """A DAT1 with the given {tag: bytes} sections, as the game lays one out."""
    head = 16 + 12*len(sections)
    table, body = b'', b''
    for tag, data in sections.items():
        table += struct.pack('<3I', tag, head + len(body), len(data))
        body += data
    return struct.pack('<4I', 0x44415431, 0, head + len(body), len(sections)) + table + body


class ArchiveTests(unittest.TestCase):
    def test_an_assets_id_is_the_crc64_of_its_path(self):
        # The ids the toc lists them by (docs/REFERENCE.md), whatever the path's case or slashes.
        self.assertEqual(game_texts.asset_id('localization/localization_all.localization'), 0xBE55D94F171BF8DE)
        self.assertEqual(game_texts.asset_id('configs\\system\\system_TutorialPrompt.config'), 0x904043FBE0214EF3)

    def test_an_lz4_block_with_literals_and_an_overlapping_match(self):
        # "abc", then 9 bytes copied from 3 back (overlapping), then the last literals "xy".
        block = bytes([0x35]) + b'abc' + bytes([3, 0]) + bytes([0x20]) + b'xy'
        self.assertEqual(game_texts.lz4_block(block, 14), b'abcabcabcabcxy')

    def test_the_texts_come_with_tags_and_hashes(self):
        tags, texts = b'JUMP\0WEB\0', b'Press [BTN_A] to jump.\0Hold a web.\0'
        data = dat1({game_texts.LOC_COUNT: struct.pack('<I', 2),
                     game_texts.LOC_HASHES: struct.pack('<2I', 0x5BEA1D26, 0x1234),
                     game_texts.LOC_TAGS: tags, game_texts.LOC_TAG_AT: struct.pack('<2I', 0, 5),
                     game_texts.LOC_TEXTS: texts, game_texts.LOC_TEXT_AT: struct.pack('<2I', 0, 23)})
        self.assertEqual(game_texts.parse_localization(data), [
            dict(hash=0x5BEA1D26, tag='JUMP', text='Press [BTN_A] to jump.'),
            dict(hash=0x1234, tag='WEB', text='Hold a web.')])


class TableTests(unittest.TestCase):
    def test_emphasis_is_the_games_span(self):
        self.assertEqual(game_texts.markup('Pull to *zip* now'), "Pull to <span class='emphasis'>zip</span> now")
        with self.assertRaises(ValueError):
            game_texts.markup('an *unpaired star')

    def test_a_table_reads_back_as_written(self):
        rows = [dict(hash=0x20, tag='B_TAG', text='Say “B”, a "quote" and a \\ back' + ' long'*40),
                dict(hash=0x10, tag='A_TAG', text='Press [BTN_LT].')]
        text, entries = game_texts.table(rows, [('B_TAG', 'Pull a "trigger"'),
                                                ('A_TAG', game_texts.markup('Hold *X*'))])
        self.assertEqual([e[0] for e in entries], [0x10, 0x20])
        self.assertTrue(all(len(line) <= 110 for line in text.splitlines()))
        path = pathlib.Path(self.id().replace('.', '_') + '.inc')
        try:
            path.write_text(text, 'utf-8')
            self.assertEqual(game_texts.parse_table(path), [
                (0x10, 'A_TAG', 'Press [BTN_LT].', "Hold <span class='emphasis'>X</span>"),
                (0x20, 'B_TAG', rows[0]['text'], 'Pull a "trigger"')])
        finally:
            path.unlink(missing_ok=True)

    def test_a_tag_the_game_lacks_is_refused(self):
        with self.assertRaises(ValueError):
            game_texts.table([dict(hash=1, tag='A', text='a')], [('NOT_THERE', 'x')])

    def test_the_generated_table_is_the_tsv(self):
        # src/vr_texts_table.inc is written from src/vr_texts.tsv: an edit there wants `game_texts.py table`.
        source = game_texts.read_source()
        generated = game_texts.parse_table()
        self.assertGreaterEqual(len(generated), 200)
        self.assertEqual({t: vr for _, t, _, vr in generated}, dict(source))
        hashes = [h for h, _, _, _ in generated]
        self.assertEqual(hashes, sorted(set(hashes)))
        for _, tag, english, vr in generated:
            self.assertNotEqual(english, vr, tag)
            self.assertTrue(vr.isascii(), tag)


if __name__ == '__main__':
    unittest.main()
