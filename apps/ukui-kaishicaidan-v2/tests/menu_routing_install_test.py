"""User routing installation is isolated from the real desktop session."""
import os
import importlib.util
from unittest.mock import patch
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'packaging/install-menu-routing.py'


spec = importlib.util.spec_from_file_location('menu_installer', SCRIPT)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class RoutingTransactionTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.targets = [self.root / 'config/entry.desktop', self.root / 'data/menu.service']
        for target in self.targets:
            target.parent.mkdir(parents=True)
            target.write_text('original ' + target.name)
            target.chmod(0o640)
        self.plan = [(p, 'new ' + p.name) for p in self.targets]
        self.backups = self.root / 'backups'

    def install(self):
        return installer.install(self.plan, self.backups)

    def assert_original(self):
        for target in self.targets:
            self.assertEqual(target.read_text(), 'original ' + target.name)
            self.assertEqual(target.stat().st_mode & 0o777, 0o640)
        self.assertFalse(list(self.root.rglob('*.new')))
        self.assertFalse(list(self.root.rglob('*-rollback-*')))

    def test_directory_preflight_preserves_first_target(self):
        self.targets[1].unlink()
        self.targets[1].mkdir()
        with self.assertRaises(ValueError):
            self.install()
        self.assertEqual(self.targets[0].read_text(), 'original entry.desktop')
        self.assertFalse(self.backups.exists())

    def test_unwritable_parent_preserves_both(self):
        real = installer.os.access
        with patch.object(installer.os, 'access', side_effect=lambda p, flags: False if p == str(self.targets[1].parent) else real(p, flags)):
            with self.assertRaises(PermissionError):
                self.install()
        self.assert_original()

    @unittest.skipIf(os.geteuid() == 0, 'root bypasses directory permission checks')
    def test_real_readonly_directory_preflight(self):
        parent = self.targets[1].parent
        parent.chmod(0o500)
        try:
            with self.assertRaises(PermissionError):
                self.install()
            self.assert_original()
        finally:
            parent.chmod(0o700)

    def test_staging_failure_does_not_publish(self):
        real = installer.stage_bytes
        def fail(target, text):
            if target == self.targets[1]:
                raise OSError('stage denied')
            return real(target, text)
        with patch.object(installer, 'stage_bytes', side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, 'original files restored'):
                self.install()
        self.assert_original()

    def test_second_replace_failure_rolls_back_first(self):
        real = installer.os.replace
        def fail(source, target):
            if target == str(self.targets[1]):
                raise OSError('second publication denied')
            return real(source, target)
        with patch.object(installer.os, 'replace', side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, 'original files restored'):
                self.install()
        self.assert_original()

    def test_failed_install_removes_new_target(self):
        self.targets[0].unlink()
        real = installer.os.replace
        def fail(source, target):
            if target == str(self.targets[1]):
                raise OSError('second publication denied')
            return real(source, target)
        with patch.object(installer.os, 'replace', side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, 'original files restored'):
                self.install()
        self.assertFalse(self.targets[0].exists())

    def test_rollback_failure_reports_target_and_backup(self):
        real = installer.os.replace
        def fail(source, target):
            if target == str(self.targets[1]) or '-rollback-' in source:
                raise OSError('injected failure')
            return real(source, target)
        with patch.object(installer.os, 'replace', side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, 'rollback incomplete') as caught:
                self.install()
        self.assertIn(str(self.targets[0]), str(caught.exception))
        self.assertIn(str(self.backups), str(caught.exception))
        saved = next(self.backups.glob('*/0-entry.desktop'))
        self.assertEqual(saved.read_text(), 'original entry.desktop')

    def test_regular_and_dangling_links_are_preserved(self):
        for dangling in (False, True):
            with self.subTest(dangling=dangling):
                target = self.targets[0]
                target.unlink()
                sentinel = self.root / ('missing' if dangling else 'sentinel')
                if not dangling:
                    sentinel.write_text('external untouched')
                target.symlink_to(sentinel.name)
                # A relative link must resolve relative to target.parent.
                external = target.parent / sentinel.name
                if not dangling:
                    external.write_text('external untouched')
                backup = self.install()
                self.assertFalse(target.is_symlink())
                saved = backup / '0-entry.desktop'
                self.assertTrue(saved.is_symlink())
                self.assertEqual(os.readlink(saved), sentinel.name)
                if dangling:
                    self.assertFalse(external.exists())
                else:
                    self.assertEqual(external.read_text(), 'external untouched')
                installer.restore_backup(saved, target)
                self.assertTrue(target.is_symlink())
                self.assertEqual(os.readlink(target), sentinel.name)

    def test_link_restored_after_second_publication_fails(self):
        self.targets[0].unlink()
        self.targets[0].symlink_to('missing-original')
        real = installer.os.replace
        def fail(source, target):
            if target == str(self.targets[1]):
                raise OSError('second publication denied')
            return real(source, target)
        with patch.object(installer.os, 'replace', side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, 'original files restored'):
                self.install()
        self.assertTrue(self.targets[0].is_symlink())
        self.assertEqual(os.readlink(self.targets[0]), 'missing-original')


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
            backup = next(p for p in (data / 'ukui-kaishicaidan/menu-routing-backups').iterdir() if p.is_dir())
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
