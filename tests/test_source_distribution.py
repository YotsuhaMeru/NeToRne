"""Offline distribution checks; no proprietary assets or hardware required."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


patcher = load("patcher", "firmware/tools/apply-patch.py")
packager = load("packager", "scripts/package-source.py")


class PatchTests(unittest.TestCase):
    def setUp(self):
        self.source = b"original fixture"
        self.result = b"Original fixturE"
        self.patch = {
            "format": "torne-xor-v1", "size": len(self.source),
            "input_sha256": hashlib.sha256(self.source).hexdigest(),
            "output_sha256": hashlib.sha256(self.result).hexdigest(),
            "segments": [{"offset": 0, "xor": "20"}, {"offset": 15, "xor": "20"}],
        }

    def test_reconstruction_and_corrupt_input_rejection(self):
        self.assertEqual(patcher.apply_delta(self.source, self.patch), self.result)
        with self.assertRaises(ValueError):
            patcher.apply_delta(b"wrong input", self.patch)
        with self.assertRaises(ValueError):
            patcher.apply_delta(b"x" + self.source[1:], self.patch)

    def test_invalid_ranges_and_output_digest_rejected(self):
        for segments in ([{"offset": -1, "xor": "20"}],
                         [{"offset": 16, "xor": "20"}],
                         [{"offset": 0, "xor": "20"}, {"offset": 0, "xor": "20"}],
                         [{"offset": 0, "xor": "00"}]):
            patch = copy.deepcopy(self.patch)
            patch["segments"] = segments
            with self.assertRaises(ValueError):
                patcher.apply_delta(self.source, patch)

    def test_input_and_existing_output_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "source.bin"
            source.write_bytes(self.source)
            with self.assertRaises(ValueError):
                patcher.write_result(source, self.result, source)
            output = Path(temporary) / "out.bin"
            output.write_bytes(b"keep")
            with self.assertRaises(ValueError):
                patcher.write_result(output, self.result, source)
            self.assertEqual(source.read_bytes(), self.source)
            self.assertEqual(output.read_bytes(), b"keep")
            output = Path(temporary) / "new.bin"
            patcher.write_result(output, self.result, source)
            patcher.write_result(output, self.result, source)
            self.assertEqual(output.read_bytes(), self.result)

    def test_shipped_patch_structure(self):
        paths = sorted((ROOT / "firmware/patches").glob("*.json"))
        self.assertEqual(len(paths), 4)
        for path in paths:
            patch = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(patch["format"], "torne-xor-v1")
            self.assertEqual(patch["input_sha256"],
                             "653f2377c838a4e7276b447fd992084d5e6e84a90ac4d389851f82309b662a04")
            previous_end = 0
            changed = 0
            for segment in patch["segments"]:
                delta = bytes.fromhex(segment["xor"])
                self.assertTrue(delta)
                self.assertGreaterEqual(segment["offset"], previous_end)
                previous_end = segment["offset"] + len(delta)
                self.assertLessEqual(previous_end, patch["size"])
                changed += len(delta)
            self.assertLess(changed, patch["size"] // 4)


class DistributionTests(unittest.TestCase):
    def test_private_and_binary_paths_rejected(self):
        for name in ("archive/README.md", "rel/bondriver/README.md", "firmware/build/file.c",
                     "firmware/original.bin", "bondriver/Torne.dll", "linux/receiver/target/a.rs",
                     "../README.md", "diagnostics/trace.json", "linux/report.json"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                packager.validate_name(name)

    def test_public_files_and_document_links(self):
        files = packager.source_files()
        names = {name for name, _ in files}
        self.assertIn("linux/receiver/Cargo.lock", names)
        self.assertIn("bondriver/THIRD_PARTY_LICENSES.txt", names)
        self.assertIn("firmware/tools/apply-patch.py", names)
        for name, data in files:
            if not name.endswith(".md"):
                continue
            for target in re.findall(r"\]\(([^)]+)\)", data.decode("utf-8")):
                if "://" in target or target.startswith("#"):
                    continue
                target = target.split("#", 1)[0]
                linked = ((ROOT / name).parent / target).resolve().relative_to(ROOT).as_posix()
                self.assertIn(linked, names, f"Broken public link in {name}: {target}")


if __name__ == "__main__":
    unittest.main()
