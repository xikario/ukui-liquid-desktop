"""Installer transactions against an isolated home and fake system files."""
import importlib.util
import os
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('panel_install', Path(__file__).resolve().parents[1] / 'scripts/install.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root, self.home = self.base / 'repo', self.base / 'home with space'
        self.inputs = ['build/plugins/styles/libukuiliquidpanel.so', 'scripts/ukui-panel-liquid',
                       'scripts/session-start.py', 'scripts/restore.py']
        for name in self.inputs:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('new ' + name).encode())
        self.desktop, self.binary = self.base / 'panel.desktop', self.base / 'panel'
        self.desktop.write_text('[Desktop Entry]\nType=Application\nExec=ukui-panel\nName=Panel\n')
        self.binary.write_bytes(b'packaged binary')
        self.targets = installer.destinations(self.home)

    def run_install(self):
        return installer.install(self.root, self.home, self.desktop, self.binary)

    def seed(self):
        for i, path in enumerate(self.targets):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('old target ' + str(i)).encode())
            path.chmod(0o600 + i)
        self.targets[-1].unlink()
        self.targets[-1].symlink_to('previous-restore')

    def snapshot(self):
        return [(os.readlink(str(p)) if p.is_symlink() else p.read_bytes(), p.lstat().st_mode)
                if os.path.lexists(str(p)) else None for p in self.targets]

    def assert_clean(self):
        self.assertFalse(list(self.home.rglob('*.new')) if self.home.exists() else [])
        self.assertFalse(list(self.home.rglob('*-rollback-*')) if self.home.exists() else [])

    def test_all_missing_inputs_fail_before_user_changes(self):
        self.seed()
        before = self.snapshot()
        for source in [self.root / name for name in self.inputs] + [self.desktop, self.binary]:
            with self.subTest(source=source):
                saved = source.read_bytes()
                source.unlink()
                with self.assertRaises(FileNotFoundError):
                    self.run_install()
                self.assertEqual(self.snapshot(), before)
                self.assertFalse((self.root / 'releases').exists())
                source.write_bytes(saved)
        self.assert_clean()

    def test_staging_failure_does_not_publish_any_target(self):
        self.seed()
        before = self.snapshot()
        real = installer.stage_bytes
        count = 0
        def fail(destination, *args):
            nonlocal count
            count += 1
            if count == 4:
                raise OSError('injected staging error')
            return real(destination, *args)
        with patch.object(installer, 'stage_bytes', side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, 'original files restored'):
                self.run_install()
        self.assertEqual(self.snapshot(), before)
        self.assert_clean()

    def test_each_publish_failure_restores_contents_modes_and_symlink(self):
        self.seed()
        before = self.snapshot()
        real = os.replace
        for fail_at in range(1, 7):
            with self.subTest(fail_at=fail_at):
                count = 0
                def fail(source, destination):
                    nonlocal count
                    count += 1
                    if count == fail_at:
                        raise OSError('injected publish error')
                    return real(source, destination)
                with patch.object(installer.os, 'replace', side_effect=fail):
                    with self.assertRaisesRegex(RuntimeError, 'original files restored'):
                        self.run_install()
                self.assertEqual(self.snapshot(), before)
                self.assert_clean()
                self.assertFalse(list((self.root / 'releases').glob('*/installed.json')))

    def test_first_install_rollback_removes_new_files_and_directories(self):
        real = os.replace
        count = 0
        def fail(source, destination):
            nonlocal count
            count += 1
            if count == 5:
                raise OSError('injected publish error')
            return real(source, destination)
        with patch.object(installer.os, 'replace', side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, 'original files restored'):
                self.run_install()
        self.assertEqual(self.snapshot(), [None] * 6)
        self.assertFalse(self.home.exists())
        self.assert_clean()

    def test_manifest_failure_rolls_back_all_six_targets(self):
        self.seed()
        before = self.snapshot()
        real = Path.write_text
        def fail(path, *args, **kwargs):
            if path.name == 'installed.json':
                raise OSError('injected manifest error')
            return real(path, *args, **kwargs)
        with patch.object(Path, 'write_text', fail):
            with self.assertRaisesRegex(RuntimeError, 'original files restored'):
                self.run_install()
        self.assertEqual(self.snapshot(), before)
        self.assert_clean()

    def test_success_tracks_all_files_without_touching_packaged_binary(self):
        self.seed()
        manifest = self.run_install()
        self.assertEqual(manifest['installed'], [str(p) for p in self.targets])
        self.assertEqual(self.targets[2].read_bytes(), b'new ' + self.inputs[0].encode())
        self.assertIn('Exec="' + str(self.targets[1]) + '"', self.targets[0].read_text())
        self.assertIn('Exec="' + str(self.targets[4]) + '"', self.targets[3].read_text())
        self.assertEqual(self.targets[-1].stat().st_mode & 0o777, 0o755)
        before = json.loads((Path(manifest['backup']) / 'before.json').read_text())
        self.assertTrue(Path(before[str(self.targets[-1])]).is_symlink())
        self.assertTrue((Path(manifest['backup']) / 'installed.json').exists())
        self.assertEqual(self.binary.read_bytes(), b'packaged binary')
        self.assert_clean()

    def test_backup_failure_leaves_targets_untouched(self):
        self.seed()
        before = self.snapshot()
        real = installer.shutil.copy2
        count = 0
        def fail(*args, **kwargs):
            nonlocal count
            count += 1
            if count == 4:
                raise OSError('injected backup error')
            return real(*args, **kwargs)
        with patch.object(installer.shutil, 'copy2', side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, 'original files restored'):
                self.run_install()
        self.assertEqual(self.snapshot(), before)
        self.assert_clean()

    def test_rollback_failure_reports_incomplete_state_and_retains_backup(self):
        self.seed()
        real = os.replace
        count = 0
        def fail(source, destination):
            nonlocal count
            count += 1
            if count == 2:
                raise OSError('injected publish error')
            return real(source, destination)
        with patch.object(installer.os, 'replace', side_effect=fail), \
                patch.object(installer, 'restore_backup', side_effect=OSError('storage unavailable')):
            with self.assertRaisesRegex(RuntimeError, 'rollback incomplete') as error:
                self.run_install()
        self.assertIn(str(self.targets[0]), str(error.exception))
        backups = list((self.root / 'releases').glob('*/managed/00-ukui-panel.desktop'))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), b'old target 0')
        self.assert_clean()


    def test_rollback_error_reporting_without_missing_ok_support(self):
        self.seed()
        before = self.snapshot()
        real_replace, real_unlink = os.replace, Path.unlink
        count = 0
        def fail(source, destination):
            nonlocal count
            count += 1
            if count == 2:
                raise OSError('injected publish error')
            return real_replace(source, destination)
        # Python 3.7's unlink capability: deliberately accepts no missing_ok.
        def unlink_37(path):
            return real_unlink(path)
        with patch.object(installer.os, 'replace', side_effect=fail), patch.object(Path, 'unlink', unlink_37):
            with self.assertRaisesRegex(RuntimeError, 'original files restored') as error:
                self.run_install()
        self.assertIsInstance(error.exception.__cause__, OSError)
        self.assertEqual(self.snapshot(), before)
        self.assert_clean()

    def test_same_basename_targets_have_distinct_recovery_backups(self):
        self.seed()
        before = self.snapshot()
        extra = self.home / '.local/share/ukui-panel-liquid'
        extra.parent.mkdir(parents=True)
        extra.write_bytes(b'distinct old extra')
        plan, digest = installer.prepare(self.root, self.home, self.desktop, self.binary)
        plan.append((extra, b'new extra', 0o755))
        real = os.replace
        count = 0
        def fail(source, destination):
            nonlocal count
            count += 1
            if count == 7:
                raise OSError('injected publish error')
            return real(source, destination)
        with patch.object(installer, 'prepare', return_value=(plan, digest)), patch.object(installer.os, 'replace', side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, 'original files restored'):
                self.run_install()
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(extra.read_bytes(), b'distinct old extra')
        maps = list((self.root / 'releases').glob('*/before.json'))
        mapping = json.loads(maps[0].read_text())
        self.assertNotEqual(mapping[str(extra)], mapping[str(self.targets[1])])
        self.assertEqual(Path(mapping[str(extra)]).read_bytes(), b'distinct old extra')
        self.assertEqual(Path(mapping[str(self.targets[1])]).read_bytes(), b'old target 1')
        self.assert_clean()


if __name__ == '__main__':
    unittest.main()
