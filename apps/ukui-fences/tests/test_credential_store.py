#!/usr/bin/env python3
"""Run under a private D-Bus session; never use the desktop keyring."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    helper = Path(__file__).resolve().parents[1] / "scripts/credential_store.py"
    with tempfile.TemporaryDirectory(prefix="fences-keyring-test-") as root:
        env = os.environ.copy()
        for name, subdir in (("XDG_DATA_HOME", "data"), ("XDG_CONFIG_HOME", "config"),
                             ("XDG_CACHE_HOME", "cache"), ("XDG_RUNTIME_DIR", "run")):
            env[name] = str(Path(root) / subdir)
            Path(env[name]).mkdir(mode=0o700)
        env.pop("GNOME_KEYRING_CONTROL", None)
        control = Path(root) / "control"
        control.mkdir(mode=0o700)
        daemon = subprocess.Popen(["gnome-keyring-daemon", "--foreground", "--unlock",
                                   "--components=secrets", "--control-directory", str(control)],
                                  env=env, stdin=subprocess.PIPE,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        daemon.stdin.write(b"isolated-test-password\n")
        daemon.stdin.close()
        config = str(Path(env["XDG_CONFIG_HOME"]) / "kylin/ukui-fences.ini")

        def call(operation, key=None, profile=config):
            result = subprocess.run([sys.executable, str(helper), operation, profile],
                                    input=json.dumps({"key": key}) if key is not None else "",
                                    text=True, capture_output=True, env=env, timeout=8)
            assert result.returncode == 0, "credential operation failed: " + operation
            return json.loads(result.stdout)["key"] if operation == "read" else None

        app = None
        try:
            for _ in range(50):
                probe = subprocess.run(["gdbus", "call", "--session", "--dest",
                                        "org.freedesktop.DBus", "--object-path",
                                        "/org/freedesktop/DBus", "--method",
                                        "org.freedesktop.DBus.NameHasOwner",
                                        "org.freedesktop.secrets"], env=env,
                                       capture_output=True, text=True, timeout=2)
                if "true" in probe.stdout:
                    break
                time.sleep(.1)
            assert call("read") == ""
            call("write", "synthetic-key-one")
            assert call("read") == "synthetic-key-one"
            assert call("read", profile=config + "-other") == ""
            call("write", "synthetic-key-two")
            assert call("read") == "synthetic-key-two"
            call("clear")
            assert call("read") == ""
            call("clear")  # idempotent deletion

            # Exercise the actual C++ migration, with a synthetic legacy key.
            if len(sys.argv) > 1:
                cfg = Path(config)
                cfg.parent.mkdir(parents=True)
                cfg.write_text("[systemMonitor]\nautoStart=true\napiKey=legacy-test-key\n")
                # Missing backend must leave the legacy key recoverable.
                unavailable = dict(env, PATH="/nonexistent")
                app = subprocess.Popen([sys.argv[1], "--autostart"], env=unavailable,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                time.sleep(1)
                assert "apiKey=legacy-test-key" in cfg.read_text()
                app.terminate()
                app.wait(timeout=5)
                app = subprocess.Popen([sys.argv[1], "--autostart"], env=env,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                for _ in range(100):
                    contents = cfg.read_text()
                    if "credentialStore=secret-service" in contents and "apiKey=" not in contents:
                        break
                    time.sleep(.1)
                else:
                    raise AssertionError("application did not migrate legacy key")
                assert call("read") == "legacy-test-key"
                assert cfg.stat().st_mode & 0o077 == 0
                app.terminate()
                app.wait(timeout=5)
                app = subprocess.Popen([sys.argv[1], "--autostart"],
                                       env=dict(env, DEEPSEEK_API_KEY="environment-only-test-key"),
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                time.sleep(1)
                subprocess.run(["gdbus", "call", "--session", "--dest", "org.ukui.fences",
                                "--object-path", "/ukuiFences", "--method",
                                "org.ukui.fences.quitApp"], env=env, check=True,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
                app.wait(timeout=5)
                app = None
                assert "apiKey=" not in cfg.read_text()
                assert "environment-only-test-key" not in cfg.read_text()
                assert call("read") == "legacy-test-key"
            print("PASS: keyring CRUD, profile isolation, failure preservation, migration, environment override")
        finally:
            if app:
                app.terminate()
                app.wait(timeout=5)
            daemon.terminate()
            daemon.wait(timeout=5)


if __name__ == "__main__":
    main()
