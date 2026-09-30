#!/usr/bin/env python3
"""Install an opt-in user session entry with rollback; never replace packaged binaries."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import shutil
import tempfile
import fcntl


def destinations(home):
    return [
        home / '.config/autostart/ukui-panel.desktop',
        home / '.local/bin/ukui-panel-liquid',
        home / '.local/lib/ukui-panel-liquid/plugins/styles/libukuiliquidpanel.so',
        home / '.config/autostart/ukui-panel-liquid-session.desktop',
        home / '.local/bin/ukui-panel-liquid-session',
        home / '.local/bin/ukui-panel-liquid-restore',
    ]


def desktop_exec(path):
    value = str(path).replace('%', '%%')
    for char in ('\\', '"', '`', '$'):
        value = value.replace(char, '\\' + char)
    # Desktop entry string unescaping precedes Exec argument unquoting.
    value = value.replace('\\', '\\\\')
    return '"' + value + '"'


def prepare(root, home, system_desktop, system_binary):
    """Read every required input before creating a backup or touching user files."""
    paths = destinations(home)
    plugin = (root / 'build/plugins/styles/libukuiliquidpanel.so').read_bytes()
    wrapper = (root / 'scripts/ukui-panel-liquid').read_bytes()
    session = (root / 'scripts/session-start.py').read_bytes()
    restore = (root / 'scripts/restore.py').read_bytes()
    template = system_desktop.read_text()
    binary_hash = hashlib.sha256(system_binary.read_bytes()).hexdigest()
    lines = template.splitlines()
    section = ''
    found = False
    for index, line in enumerate(lines):
        if line.startswith('['):
            section = line
        if section == '[Desktop Entry]' and line.startswith('Exec='):
            lines[index] = 'Exec=' + desktop_exec(paths[1])
            found = True
    if not found:
        raise ValueError('system panel desktop entry has no Exec field')
    # Insert in the main group, before any optional desktop action group.
    lines = [line for line in lines if not line.startswith('X-LiquidPanel-Managed=')]
    next_group = next((i for i, line in enumerate(lines) if i and line.startswith('[')), len(lines))
    lines.insert(next_group, 'X-LiquidPanel-Managed=true')
    panel_entry = ('\n'.join(lines) + '\n').encode()
    session_entry = (
        '[Desktop Entry]\nType=Application\nName=液态面板登录恢复\n'
        'Exec=' + desktop_exec(paths[4]) + '\nTerminal=false\nOnlyShowIn=UKUI;\nNoDisplay=true\n'
        'X-UKUI-Autostart-Phase=Application\nX-UKUI-AutoRestart=false\nX-LiquidPanel-Managed=true\n'
    ).encode()
    return list(zip(paths, [panel_entry, wrapper, plugin, session_entry, session, restore],
                    [0o644, 0o755, 0o755, 0o644, 0o755, 0o755])), binary_hash


def mkdir_tracked(directory, created):
    missing = []
    current = directory
    while not current.exists():
        missing.append(current)
        current = current.parent
    for path in reversed(missing):
        path.mkdir()
        created.append(path)


def stage_bytes(destination, data, mode):
    fd, name = tempfile.mkstemp(prefix='.' + destination.name + '-', suffix='.new', dir=str(destination.parent))
    staged = Path(name)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        staged.chmod(mode)
        return staged
    except BaseException:
        staged.unlink()
        raise


def restore_backup(backup, destination):
    fd, name = tempfile.mkstemp(prefix='.' + destination.name + '-rollback-', dir=str(destination.parent))
    os.close(fd)
    staged = Path(name)
    try:
        if backup.is_symlink():
            staged.unlink()
        shutil.copy2(backup, staged, follow_symlinks=False)
        os.replace(str(staged), str(destination))
    finally:
        if os.path.lexists(str(staged)):
            staged.unlink()


def remove_if_exists(path):
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def install(root, home, system_desktop=Path('/etc/xdg/autostart/ukui-panel.desktop'),
            system_binary=Path('/usr/bin/ukui-panel')):
    root, home = Path(root), Path(home)
    plan, binary_hash = prepare(root, home, Path(system_desktop), Path(system_binary))
    releases = root / 'releases'
    releases.mkdir(parents=True, exist_ok=True)
    with (releases / '.install.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        release = Path(tempfile.mkdtemp(prefix=datetime.datetime.now().strftime('%Y%m%d-%H%M%S-'), dir=str(releases)))
        created, staged, committed, backups = [], {}, [], {}
        manifest = {'backup': str(release), 'installed': [str(p) for p, _, _ in plan],
                    'system_binary_sha256': binary_hash}
        try:
            # Back up every managed target, including the recovery helper.
            for index, (destination, _, _) in enumerate(plan):
                if os.path.lexists(str(destination)):
                    if destination.is_dir():
                        raise IsADirectoryError(str(destination))
                    backup = release / 'managed' / ('%02d-' % index + destination.name)
                    backup.parent.mkdir(exist_ok=True)
                    shutil.copy2(destination, backup, follow_symlinks=False)
                    backups[destination] = backup
                else:
                    backups[destination] = None
            for name in ['panel.conf', 'panel-commission.ini', 'liquid-panel.ini']:
                source = home / '.config/ukui' / name
                if source.is_file():
                    config_backup = release / 'config' / name
                    config_backup.parent.mkdir(exist_ok=True)
                    shutil.copy2(source, config_backup)
            (release / 'before.json').write_text(json.dumps(
                {str(p): str(b) if b else None for p, b in backups.items()}, indent=2))
            for destination, data, mode in plan:
                mkdir_tracked(destination.parent, created)
                staged[destination] = stage_bytes(destination, data, mode)
            for destination, _, _ in plan:
                os.replace(str(staged[destination]), str(destination))
                committed.append(destination)
            (release / 'installed.json').write_text(json.dumps(manifest, indent=2))
        except BaseException as error:
            rollback_errors = []
            for destination in reversed(committed):
                try:
                    backup = backups[destination]
                    if backup is None:
                        destination.unlink()
                    else:
                        restore_backup(backup, destination)
                except BaseException as rollback_error:
                    rollback_errors.append(str(destination) + ': ' + str(rollback_error))
            remove_if_exists(release / 'installed.json')
            if rollback_errors:
                raise RuntimeError('installation failed; rollback incomplete; backup: ' + str(release)
                                   + '\n' + '\n'.join(rollback_errors)) from error
            raise RuntimeError('installation failed; original files restored; backup: ' + str(release)) from error
        finally:
            for temporary in staged.values():
                if os.path.lexists(str(temporary)):
                    temporary.unlink()
            if len(committed) != len(plan) or not (release / 'installed.json').exists():
                for directory in reversed(created):
                    try:
                        directory.rmdir()
                    except OSError:
                        pass
        return manifest


def main():
    print(json.dumps(install(Path(__file__).resolve().parents[1], Path.home()), indent=2))


if __name__ == '__main__':
    main()
