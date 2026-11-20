"""The N55 tile layout, artwork and plugin agree with the metric table in src/n55_metrics.c."""
import importlib.util
import re
import subprocess
import sys
import tempfile
import unittest
from io import BytesIO
from pathlib import Path
from xml.etree import ElementTree
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import n55_table as table  # noqa: E402
import make_n55_layout as layout  # noqa: E402

DUMP = ROOT / 'build' / 'n55-dump'
PLUGIN = (ROOT / 'src' / 'plugin.cpp').read_text()


def plugin_constant(name):
    return int(re.search(r'\b%s = (\d+)u' % name, PLUGIN).group(1))


class TilesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rows = subprocess.run([str(DUMP)], capture_output=True, text=True, check=True).stdout.splitlines()
        cls.native = [row.split('\t') for row in rows]
        cls.metrics = table.metrics()
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'BoostDeck.xml'
            layout.main(str(ROOT / 'plugin' / 'BoostDeck.xml'), str(out), '--tiles')
            cls.root = ElementTree.parse(out).getroot()

    def test_python_reads_the_same_table(self):
        self.assertEqual([r[0] for r in self.native], [m[0] for m in self.metrics])
        self.assertEqual([r[1] for r in self.native], [m[1] for m in self.metrics])
        self.assertEqual([r[2] for r in self.native], [table.display_name(m[1]) for m in self.metrics])
        sets = table.zero_sets()
        for row, (_, _, bar, lo, hi) in zip(self.native, self.metrics):
            if bar == 'NB_ZERO':
                self.assertEqual(int(row[3]), sets.index(table.zero_step(lo, hi)))
                self.assertEqual(int(row[4]), table.zero_step(lo, hi))
            else:
                self.assertEqual(int(row[3]), -1)

    def test_plugin_ids_match_the_tools(self):
        for name, value in (('kCaptionModel', layout.CAPTION_MODEL), ('kButtonModel', layout.BUTTON_MODEL),
                            ('kHeaderModel', layout.HEADER_MODEL), ('kTilesHeaderModel', layout.TILES_HEADER_MODEL),
                            ('kResetAction', layout.RESET_ACTION), ('kPickFirst', table.PICK_FIRST),
                            ('kPickBlank', table.PICK_BLANK), ('kCaptionFirst', table.CAPTION_FIRST),
                            ('kCaptionBlank', table.CAPTION_BLANK), ('kZeroFirst', table.ZERO_FIRST),
                            ('kZeroSet', table.ZERO_SET), ('kTilesBackground', 56302)):
            self.assertEqual(plugin_constant(name), value, name)

    def test_every_position_lists_every_metric_and_blank(self):
        actions = {int(a.get('id')) for a in self.root.iter('raAction')}
        for p, name in enumerate(table.POSITIONS):
            state = self.root.find(".//hmiState[@id='%d']" % (layout.POSITION_STATE + p))
            self.assertEqual(state.get('text'), name)
            picks = [int(b.get('action')) for b in state.iter('button')]
            expected = [table.PICK_FIRST + p * 64 + m for m in range(len(self.metrics))] + \
                       [table.PICK_FIRST + p * 64 + table.PICK_BLANK]
            self.assertEqual(sorted(picks), sorted(expected))
            self.assertTrue(set(picks) <= actions)
            texts = [b.get('text') for b in state.iter('button')]
            self.assertEqual(texts[-1], '(blank)')
            self.assertEqual(texts[:-1], [table.display_name(self.metrics[[m[0] for m in self.metrics].index(k)][1])
                                          for k in table.MENU])

    def test_ids_are_unique(self):
        ids = [e.get('id') for e in self.root.iter() if e.get('id') and e.tag != 'property']
        self.assertEqual(len(ids), len(set(ids)))

    @unittest.skipUnless(importlib.util.find_spec('PIL'), 'needs Pillow')
    def test_art_has_every_image_the_plugin_can_ask_for(self):
        from PIL import Image
        import make_n55_tiles_art as art
        source = Image.new('RGBA', (1172, 400), art.TILE)
        files = art.build(source, art.DEFAULT_FONT)
        names = set(files)
        for m in range(len(self.metrics)):
            self.assertIn('%d.png' % (table.CAPTION_FIRST + m), names)
        for z in range(len(table.zero_sets())):
            for palette in range(table.PALETTES):
                for step in range(table.STEPS + 1):
                    self.assertIn('%d.png' % (table.ZERO_FIRST + z * table.ZERO_SET + palette * 33 + step), names)
        self.assertEqual(Image.open(BytesIO(files['56302.png'])).size, (1172, 400))


if __name__ == '__main__':
    unittest.main()
