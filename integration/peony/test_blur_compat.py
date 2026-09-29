import unittest
from blur_compat import matches
class MatchingTests(unittest.TestCase):
    def test_only_peony_normal_windows(self):
        self.assertTrue(matches('peony',[1],1))
        self.assertTrue(matches('peony',[],1))
        for name in ['peony-qt-desktop','ukui-fences','peony-helper','chrome']:
            self.assertFalse(matches(name,[1],1))
        self.assertFalse(matches('peony',[2],1)) # popup / desktop window
if __name__=='__main__':unittest.main()
