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
    def test_drawable_matches_display_including_portrait(self):
        for width,height in [(2880,1800),(1920,1080),(2560,1080),(1080,1920)]:
            self.assertEqual(trial.fill_geometry(width,height,16/9),(0,0,width,height))


class VisibilityTest(unittest.TestCase):
    def test_two_windows_cover_desktop_together(self):
        self.assertEqual(trial.exposed_fraction([(0,0,100,100)],[(0,0,55,100),(45,0,55,100)]),0)

    def test_overlapping_windows_are_not_double_counted(self):
        self.assertEqual(trial.exposed_fraction([(0,0,100,100)],[(0,0,60,100)]*2),0.4)

    def test_offscreen_and_multiple_video_regions(self):
        self.assertEqual(trial.exposed_fraction([(0,0,40,100),(60,0,40,100)], [(-100,0,120,100)]),0.75)

    def test_hysteresis(self):
        self.assertTrue(trial.should_pause(0.09,False))
        self.assertFalse(trial.should_pause(0.11,False))
        self.assertTrue(trial.should_pause(0.11,True))
        self.assertFalse(trial.should_pause(0.16,True))

    def test_empty_region(self):
        self.assertEqual(trial.exposed_fraction([],[]),0)


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
