#!/usr/bin/env python3
"""Route this user's native UKUI menu startup/activation to Liquid V2."""
import argparse
import datetime
import fcntl
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile


def validate_target(target):
    # Inspect the directory entry itself, including dangling links. Replacing
    # a symlink is supported; neither backup nor publication follows its target.
    if os.path.lexists(str(target)):
        mode = target.lstat().st_mode
        if not (stat.S_ISREG(mode) or stat.S_ISLNK(mode)):
            raise ValueError('Target is not a regular file or symbolic link: ' + str(target))
    parent = target.parent
    while not parent.exists():
        parent = parent.parent
    if not parent.is_dir() or not os.access(str(parent), os.W_OK | os.X_OK):
        raise PermissionError('Target parent is not writable: ' + str(parent))


def stage_bytes(target, contents):
    fd, name = tempfile.mkstemp(prefix='.' + target.name + '-', suffix='.new', dir=str(target.parent))
    staged = Path(name)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(contents.encode())
            stream.flush()
            os.fsync(stream.fileno())
        staged.chmod(0o644)
        return staged
    except BaseException:
        staged.unlink()
        raise


def restore_backup(backup, target):
    fd, name = tempfile.mkstemp(prefix='.' + target.name + '-rollback-', dir=str(target.parent))
    os.close(fd)
    staged = Path(name)
    try:
        if backup.is_symlink():
            staged.unlink()
        shutil.copy2(backup, staged, follow_symlinks=False)
        os.replace(str(staged), str(target))
    finally:
        if os.path.lexists(str(staged)):
            staged.unlink()


def install(plan, backup_root):
    """Back up and stage ALL files before publishing; undo in reverse on failure."""
    plan = [(Path(p), text) for p, text in plan]
    for target, _ in plan:
        validate_target(target)
    backup_root.mkdir(parents=True, exist_ok=True)
    with (backup_root / '.install.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for target, _ in plan:
            validate_target(target)
        backup = Path(tempfile.mkdtemp(prefix=datetime.datetime.now().strftime('%Y%m%d-%H%M%S-'),
                                       dir=str(backup_root)))
        originals, staged, committed = {}, {}, []
        try:
            for index, (target, _) in enumerate(plan):
                saved = backup / (str(index) + '-' + target.name)
                originals[target] = None
                if os.path.lexists(str(target)):
                    shutil.copy2(target, saved, follow_symlinks=False)
                    originals[target] = saved
                (backup / (str(index) + '-target.txt')).write_text(str(target) + '\n')
            (backup / 'before.json').write_text(json.dumps(
                {str(p): str(b) if b is not None else None for p, b in originals.items()}, indent=2))
            for target, contents in plan:
                target.parent.mkdir(parents=True, exist_ok=True)
                staged[target] = stage_bytes(target, contents)
            for target, _ in plan:
                os.replace(str(staged[target]), str(target))
                committed.append(target)
            (backup / 'installed.json').write_text(json.dumps([str(p) for p in committed], indent=2))
        except BaseException as error:
            errors = []
            for target in reversed(committed):
                try:
                    saved = originals[target]
                    if saved is None:
                        target.unlink()
                    else:
                        restore_backup(saved, target)
                except BaseException as rollback_error:
                    errors.append(str(target) + ': ' + str(rollback_error))
            if errors:
                raise RuntimeError('Installation failed; rollback incomplete; backup: ' + str(backup)
                                   + '\n' + '\n'.join(errors)) from error
            raise RuntimeError('Installation failed; original files restored; backup: ' + str(backup)) from error
        finally:
            for temporary in staged.values():
                if os.path.lexists(str(temporary)):
                    temporary.unlink()
        return backup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=Path.home() / '.local')
    args = parser.parse_args()
    launcher = args.prefix.resolve() / 'bin/ukui-kaishicaidan-launcher'
    if not launcher.is_file() or not os.access(str(launcher), os.X_OK):
        parser.error('Install the Liquid launcher before configuring menu routing')
    config = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
    data = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    source = Path(__file__).resolve().parent
    plan = [
        (config / 'autostart/org.ukui.menu.desktop', (source / 'org.ukui.menu.desktop').read_text()),
        (data / 'dbus-1/services/org.ukui.menu.service',
         (source / 'org.ukui.menu.service.in').read_text().replace(
             '@LAUNCHER@', '"' + str(launcher).replace('\\', '\\\\').replace('"', '\\"') + '"')),
    ]
    backup = install(plan, data / 'ukui-kaishicaidan/menu-routing-backups')
    try:
        subprocess.run(['gdbus', 'call', '--session', '--timeout', '5',
                        '--dest', 'org.freedesktop.DBus', '--object-path', '/org/freedesktop/DBus',
                        '--method', 'org.freedesktop.DBus.ReloadConfig'],
                       check=True, timeout=6, stdout=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        print('Could not reload the session bus. Log out/in to refresh D-Bus activation routing.')
    print('Menu routing installed. Backup/target paths:', backup)
    print('Stop session module org.ukui.menu.desktop, then restart Liquid V2 to claim org.ukui.menu.')


if __name__ == '__main__':
    main()
