"""Read-only native ownership contract behind NPC action-owned look suppression."""
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib')]
import gamedir
GAME = gamedir.find()


class NativeNpcLook(unittest.TestCase):
    @unittest.skipUnless(GAME and (Path(GAME) / 'EDF.dll').is_file(), 'supported EDF.dll unavailable')
    def test_action_bit_blocks_integration_and_network_but_cas_reads_input(self):
        import pefile
        pe = pefile.PE(str(Path(GAME) / 'EDF.dll'), fast_load=True)
        self.addCleanup(pe.close)
        self.assertEqual(pe.FILE_HEADER.TimeDateStamp, 0x678CCB46)
        img = pe.get_memory_mapped_image()
        contracts = {
            0x573B9F: 'f686d005000002752e',  # test Human+5D0,2; jne past look integration
            0x59FE5F: '83e502',               # network sender uses the same bit
            0x59FF65: '85ed7517',             # bit set: send two zero floats instead of d60/d64
            0x5741E7: 'f30f10b6640d0000',     # CAS variable uses d64 even in the action-owned state
            0x574215: '0f28d68b9628140000488bcfe8',  # d64 -> CAS variable Human+1428
        }
        for address, text in contracts.items():
            with self.subTest(address=hex(address)):
                expected = bytes.fromhex(text)
                self.assertEqual(img[address:address + len(expected)], expected)
        source = (ROOT / 'src/npcai.cpp').read_text(encoding='utf-8')
        self.assertRegex(source, r'kActionLimits\s*=\s*0x5D0')
        self.assertIn('if(At<std::uint32_t>(h,kActionLimits)&2)return;', source)
        for address in (0x573B9F, 0x59FE5F, 0x5741E7):
            self.assertIn(f'{{0x{address:X},', source)


if __name__ == '__main__':
    unittest.main()
