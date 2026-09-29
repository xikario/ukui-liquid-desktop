import unittest
from blur_compat import AFFECTED_APPLICATIONS, matches
class MatchingTests(unittest.TestCase):
    def test_confirmed_system_applications(self):
        for name in AFFECTED_APPLICATIONS:
            with self.subTest(name=name):
                self.assertTrue(matches(name,[1],1))
                self.assertTrue(matches(name,[],1))
                self.assertFalse(matches(name,[2],1)) # popup / desktop window

    def test_unrelated_windows_keep_blur(self):
        for name in ['peony-qt-desktop','ukui-fences','ukui-panel',
                     'ukui-kaishicaidan-v2','peony-helper','chrome',
                     'kylin-software-center-helper','kylin-unverified-app']:
            with self.subTest(name=name):
                self.assertFalse(matches(name,[1],1))
if __name__=='__main__':unittest.main()
