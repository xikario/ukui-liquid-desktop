import importlib.util
from pathlib import Path
import tempfile
import sys
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('trial', Path(__file__).with_name('trial-frequency-floor.py'))
trial = importlib.util.module_from_spec(spec);spec.loader.exec_module(trial)

class TemporaryFloorTest(unittest.TestCase):
    def exercise(self, action):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);node=root/'gpu';node.mkdir();(node/'device').mkdir()
            (node/'device/driver').symlink_to(root/'ftg340')
            for key,value in {'min_freq':'600000','max_freq':'800000','available_frequencies':'200000 400000 600000 800000','governor':'simple_ondemand'}.items():
                (node/key).write_text(value)
            state=root/'state.json';state.write_text('{"min_freq":200000}')
            def during_test(_):
                self.assertEqual((node/'min_freq').read_text().strip(),'200000')
                action(node)
            with patch.object(trial,'NODE',node),patch.object(trial,'STATE',state),patch.object(trial.os,'geteuid',return_value=0),patch.object(sys,'argv',['trial','--seconds','30']),patch.object(trial.signal,'signal'),patch.object(trial.time,'sleep',side_effect=during_test):
                try:trial.main()
                except KeyboardInterrupt:pass
            return (node/'min_freq').read_text().strip()

    def test_interrupt_restores_original_floor(self):
        def interrupt(node):raise KeyboardInterrupt
        self.assertEqual(self.exercise(interrupt),'600000')

    def test_concurrent_policy_change_is_not_overwritten(self):
        self.assertEqual(self.exercise(lambda node:(node/'min_freq').write_text('400000')),'400000')

if __name__=='__main__':unittest.main()
