import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "prepare_runtime", Path(__file__).resolve().parents[1] / "scripts/prepare-runtime.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PrepareRuntimeTests(unittest.TestCase):
    def test_mixed_case_and_preserved_saves(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            data = root / "GaMe"
            data.mkdir()
            for name in ("title.bit", "iBm.Ch", "Runes.ch", "bRiT.oOl"):
                (data / name).write_bytes(b"original")
            (data / "init.gam").write_bytes(bytes(4192))
            (data / "sFx").mkdir()
            (data / "sFx/Title1.WAV").write_bytes(b"audio fixture")
            (data / "mUsIc").mkdir()
            (data / "mUsIc/02 - Britannic Lands.mp3").write_bytes(b"music fixture")
            (data / "sOuNd").mkdir()
            (data / "sOuNd/step0.wav").write_bytes(b"sound fixture")
            runtime = root / "RuNtImE"
            runtime.mkdir()
            (runtime / "sAvEgAmE").mkdir()
            save = runtime / "sAvEgAmE/Brit.Ool"
            save.write_bytes(b"existing save")
            result = module.prepare(root / "GAME", root / "runtime")
            self.assertEqual(result, runtime)
            self.assertEqual(save.read_bytes(), b"existing save")
            self.assertFalse((runtime / "SAVEGAME").exists())
            self.assertTrue((runtime / "SFX/Title1.WAV").exists())
            self.assertEqual((runtime / "Music/02 - Britannic Lands.mp3").read_bytes(), b"music fixture")
            self.assertEqual((runtime / "Sound/step0.wav").read_bytes(), b"sound fixture")
            self.assertTrue((runtime / "init.gam").exists())
            cursor = runtime / "textures/cursors/cursor-pointer.png"
            self.assertTrue(cursor.is_file())
            original = cursor.read_bytes()
            cursor.write_bytes(b"stale")
            (data / "sOuNd/step0.wav").write_bytes(b"updated sound")
            module.prepare(root / "game", root / "RUNTIME")
            self.assertEqual(cursor.read_bytes(), original)
            self.assertEqual(save.read_bytes(), b"existing save")
            self.assertEqual((runtime / "Sound/step0.wav").read_bytes(), b"updated sound")
            (data / "TITLE.BIT").write_bytes(b"conflict")
            with self.assertRaises(ValueError):
                module.prepare(data, runtime)


if __name__ == "__main__":
    unittest.main()
