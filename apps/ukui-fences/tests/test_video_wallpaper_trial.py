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
    def test_only_explicit_hide_rewinds_on_resume(self):
        self.assertTrue(trial.should_rewind(True,True,False,True))
        self.assertFalse(trial.should_rewind(False,True,False,True))
        self.assertFalse(trial.should_rewind(True,True,True,True))
        self.assertFalse(trial.should_rewind(True,False,False,True))
        self.assertFalse(trial.should_rewind(True,True,False,False))

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


class PlaybackPolicyTest(unittest.TestCase):
    def test_first_start_is_immediate(self):
        self.assertEqual(trial.playback_action(True, False, False, started=False), 'play')

    def test_resume_waits_then_plays_when_due(self):
        self.assertEqual(trial.playback_action(True, False, False, started=True), 'wait')
        self.assertEqual(trial.playback_action(True, False, True, started=True), 'none')
        self.assertEqual(trial.playback_action(True, False, True, started=True, due=True), 'play')

    def test_cover_pauses_at_once_and_cancels_pending_resume(self):
        self.assertEqual(trial.playback_action(False, True, False, started=True), 'pause')
        self.assertEqual(trial.playback_action(False, False, True, started=True), 'pause')
        self.assertEqual(trial.playback_action(False, False, False, started=True), 'none')

    def test_playing_stays_playing(self):
        self.assertEqual(trial.playback_action(True, True, False, started=True), 'none')

    def test_state_reports_are_throttled(self):
        self.assertFalse(trial.should_report(False, 0.34, 0.32))
        self.assertTrue(trial.should_report(False, 0.40, 0.34))
        self.assertTrue(trial.should_report(True, 0.34, 0.34))


class ShadowFrameTest(unittest.TestCase):
    def test_client_side_shadow_is_not_an_occluder(self):
        self.assertEqual(trial.visible_frame(100, 50, 848, 663, [24, 24, 15, 48]), (124, 65, 800, 600))

    def test_plain_window_unchanged(self):
        self.assertEqual(trial.visible_frame(0, 0, 640, 480, []), (0, 0, 640, 480))

    def test_degenerate_extents_drop_window(self):
        self.assertIsNone(trial.visible_frame(0, 0, 40, 40, [30, 30, 0, 0]))

    def test_shadow_margin_restores_exposed_desktop(self):
        frame = trial.visible_frame(0, 0, 148, 163, [24, 24, 15, 48])
        self.assertAlmostEqual(trial.exposed_fraction([(0, 0, 200, 200)], [frame]), 1 - 100*100/40000)


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
