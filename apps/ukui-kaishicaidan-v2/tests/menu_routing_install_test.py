"""User routing installation is isolated from the real desktop session."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'packaging/install-menu-routing.py'


class RoutingInstallTest(unittest.TestCase):
    def test_backup_and_paths_with_spaces(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            prefix = root / 'prefix with space'
            config, data = root / 'config', root / 'data'
            launcher = prefix / 'bin/ukui-kaishicaidan-launcher'
            launcher.parent.mkdir(parents=True)
            launcher.write_text('#!/bin/sh\nexit 0\n')
            launcher.chmod(0o755)
            # Never reload the real session bus from this filesystem test.
            fake = root / 'tools/gdbus'
            fake.parent.mkdir()
            fake.write_text('#!/bin/sh\nexit 0\n')
            fake.chmod(0o755)
            env = dict(os.environ, XDG_CONFIG_HOME=str(config), XDG_DATA_HOME=str(data),
                       PATH=str(fake.parent) + os.pathsep + os.environ['PATH'])
            targets = [config / 'autostart/org.ukui.menu.desktop',
                       data / 'dbus-1/services/org.ukui.menu.service']
            for target in targets:
                target.parent.mkdir(parents=True)
                target.write_text('old ' + target.name)
            subprocess.run(['python3', str(SCRIPT), '--prefix', str(prefix)],
                           env=env, check=True, capture_output=True, text=True)
            self.assertIn('Hidden=true', targets[0].read_text())
            self.assertIn('Exec="' + str(launcher) + '" --autostart', targets[1].read_text())
            backup = next((data / 'ukui-kaishicaidan/menu-routing-backups').iterdir())
            for index, target in enumerate(targets):
                self.assertEqual((backup / (str(index) + '-' + target.name)).read_text(),
                                 'old ' + target.name)
                self.assertEqual((backup / (str(index) + '-target.txt')).read_text().strip(), str(target))

    def test_missing_launcher_does_not_write_configuration(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'),
                       XDG_DATA_HOME=str(root / 'data'))
            result = subprocess.run(['python3', str(SCRIPT), '--prefix', str(root / 'missing')],
                                    env=env, capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertFalse((root / 'config').exists())
            self.assertFalse((root / 'data').exists())


if __name__ == '__main__':
    unittest.main()
