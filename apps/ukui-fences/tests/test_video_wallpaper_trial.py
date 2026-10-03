import importlib.util
from pathlib import Path
import unittest
import json
import tempfile
from unittest.mock import patch
from fractions import Fraction

spec = importlib.util.spec_from_file_location('video_trial', Path(__file__).resolve().parents[1] / 'scripts/video_wallpaper_trial.py')
trial = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trial)


class FillGeometryTest(unittest.TestCase):
    def test_native_16_by_10_crop(self):
        self.assertEqual(trial.fill_geometry(2880, 1800, 16 / 9), (-160, 0, 3200, 1800))

    def test_wide_display(self):
        x, y, width, height = trial.fill_geometry(2560, 1080, 16 / 9)
        self.assertEqual((x, width), (0, 2560))
        self.assertLess(y, 0)
        self.assertGreater(height, 1080)

    def test_equal_aspect(self):
        self.assertEqual(trial.fill_geometry(1920, 1080, 16 / 9), (0, 0, 1920, 1080))

    def test_portrait(self):
        x, y, width, height = trial.fill_geometry(1080, 1920, 16 / 9)
        self.assertLess(x, 0)
        self.assertEqual((y, height), (0, 1920))
        self.assertGreater(width, 1080)


class NativeRateMediaTest(unittest.TestCase):
    def metadata(self, **changes):
        stream = dict(width=3840, height=2160, avg_frame_rate='60/1', codec_name='h264')
        stream.update(changes)
        return json.dumps({'streams': [stream], 'format': {'duration': '4.684'}}).encode()

    def inspect(self, metadata):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / '原片 clip.mp4'
            path.write_bytes(b'unchanged source')
            with patch.object(trial.subprocess, 'check_output', return_value=metadata) as probe:
                result = trial.inspect_media(path)
            self.assertEqual(path.read_bytes(), b'unchanged source')
            self.assertEqual(probe.call_args.args[0][-1], str(path))
            self.assertEqual(list(Path(directory).iterdir()), [path])
            return result

    def test_original_4k60_is_not_transcoded(self):
        result = self.inspect(self.metadata())
        self.assertEqual(result[1:4], (3840, 2160, Fraction(60)))

    def test_fractional_native_rate_is_preserved(self):
        self.assertEqual(self.inspect(self.metadata(avg_frame_rate='60000/1001'))[3],
                         Fraction(60000, 1001))

    def test_unknown_rate_rejected_without_rate_substitution(self):
        with self.assertRaises((ValueError, RuntimeError)):
            self.inspect(self.metadata(avg_frame_rate='0/1'))

    def test_unsupported_codec_is_not_silently_software_decoded(self):
        with self.assertRaises(RuntimeError):
            self.inspect(self.metadata(codec_name='av1'))
