import importlib.util
from pathlib import Path
import tempfile
import unittest
spec = importlib.util.spec_from_file_location('policy', Path(__file__).with_name('ftg340-responsiveness.py'))
p = importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
class PolicyTests(unittest.TestCase):
    def test_ac_and_battery(self):
        freqs=[200000,400000,600000,800000]
        self.assertEqual(p.desired_floor(200000,600000,800000,freqs,True),600000)
        self.assertEqual(p.desired_floor(200000,600000,800000,freqs,False),200000)
        self.assertEqual(p.desired_floor(200000,600000,400000,freqs,True),400000)
    def test_lifecycle_and_power_events(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);node=root/'gpu';node.mkdir();(node/'device').mkdir();(node/'device/driver').symlink_to(root/'ftg340')
            for key,value in {'min_freq':'200000','max_freq':'800000','available_frequencies':'200000 400000 600000 800000','governor':'simple_ondemand'}.items():
                (node/key).write_text(value)
            ac=root/'power/AC';ac.mkdir(parents=True);(ac/'type').write_text('Mains');(ac/'online').write_text('1')
            p.NODE=node;p.POWER=root/'power';p.CONFIG=root/'config';p.CONFIG.write_text('{"ac_min_freq":600000}')
            p.STATE=root/'state/original.json';p.MARKER=root/'active'
            p.run('start');self.assertEqual((node/'min_freq').read_text().strip(),'600000')
            (ac/'online').write_text('0');p.run('apply');self.assertEqual((node/'min_freq').read_text().strip(),'200000')
            (ac/'online').write_text('1');p.run('apply');self.assertEqual((node/'min_freq').read_text().strip(),'600000')
            p.run('stop');self.assertEqual((node/'min_freq').read_text().strip(),'200000')
            p.run('apply');self.assertEqual((node/'min_freq').read_text().strip(),'200000')
            p.run('start');p.run('stop');self.assertEqual((node/'min_freq').read_text().strip(),'200000')
if __name__=='__main__':unittest.main()
