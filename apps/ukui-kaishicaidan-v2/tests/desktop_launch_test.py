"""Exercise the real launch helper after its UI-owned pipes are closed."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

BIN = os.environ['UKUI_KAISHICAIDAN_V2_BIN']


class DesktopLaunchTest(unittest.TestCase):
    def test_app_survives_helper_pipe_closure(self):
        with tempfile.TemporaryDirectory(prefix='desktop-launch-') as tmp:
            root = Path(tmp)
            script = root / 'app with spaces.py'
            marker = root / 'survived'
            script.write_text(
                'import os, signal, time\n'
                'signal.signal(signal.SIGPIPE, signal.SIG_DFL)\n'
                'time.sleep(.4)\n'
                'os.write(1, b"delayed application stdout\\n")\n'
                'os.write(2, b"delayed application stderr\\n")\n'
                'open(' + repr(str(marker)) + ', "w").write("alive")\n')
            desktop = root / 'fixture.desktop'
            desktop.write_text('[Desktop Entry]\nType=Application\nName=Fixture\n'
                               'Exec="' + sys.executable + '" "' + str(script) + '" %U\n'
                               'Terminal=false\n')
            helper = subprocess.Popen([BIN, '--launch-desktop', str(desktop)],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                self.assertEqual(helper.wait(timeout=5), 0)
            finally:
                helper.stdout.close()
                helper.stderr.close()
                if helper.poll() is None:
                    helper.kill()
                    helper.wait()
            deadline = time.monotonic() + 4
            while not marker.exists() and time.monotonic() < deadline:
                time.sleep(.05)
            self.assertTrue(marker.exists(), 'application died after helper output pipes closed')

    def test_missing_desktop_reports_failure(self):
        result = subprocess.run([BIN, '--launch-desktop', '/nonexistent/ukui-launch-test.desktop'],
                                capture_output=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(result.stderr)


if __name__ == '__main__':
    unittest.main()
