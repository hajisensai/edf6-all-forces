from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import dsgo
import vehicle_optics as optics


class OpticsModelReferenceTests(unittest.TestCase):
    def test_direct_and_wrapped_dsgo_primary_model_round_trip(self) -> None:
        spec = next(s for s in optics.MODELS if s.stem == 'V515_RETROBALAM')
        original = 'app:/object/v515_retrobalam.mrab'
        for depth in (0, 1, 2):
            with self.subTest(depth=depth), patch.object(optics, 'ready', return_value=True):
                ref = dsgo.Node([original, 'v515_retrobalam.mdb'])
                for _ in range(depth):
                    ref = dsgo.Node([ref])
                animation = dsgo.Node([ref, 'keep.cas', dsgo.Blob(b'keep animation')])
                source = dsgo.write(dsgo.Document(dsgo.Node([animation, 123.0], {0: 'animation_model', 1: 'keep'}), []))
                changed, needs = optics.redirect(source)
                self.assertEqual(optics.model_path(changed), 'app:/object/' + optics.output(spec).lower())
                self.assertEqual(needs, ('OBJECT/' + optics.output(spec),))
                doc = dsgo.parse(changed)
                self.assertEqual(doc.root.get('keep'), 123.0)
                self.assertEqual(doc.root.get('animation_model').items[1:], ['keep.cas', dsgo.Blob(b'keep animation')])
                restored = optics.restore_path(changed, original)
                self.assertEqual(optics.model_path(restored), original)
                self.assertEqual(dsgo.parse(restored).root, dsgo.parse(source).root)
                redirected, repeated_needs = optics.redirect(restored)
                self.assertEqual(optics.model_path(redirected), optics.model_path(changed))
                self.assertEqual(repeated_needs, needs)

    def test_non_model_structure_is_preserved(self) -> None:
        animation = dsgo.Node([dsgo.Node([dsgo.Blob(b'not a path')])])
        source = dsgo.write(dsgo.Document(dsgo.Node([animation], {0: 'animation_model'}), []))
        self.assertEqual(optics.redirect(source), (source, ()))
        self.assertIsNone(optics.model_path(source))


if __name__ == '__main__':
    unittest.main()
