"""Launcher file preservation, quoting, locking, and diagnostics."""
from pathlib import Path
import fcntl
import os
import shutil
import runpy
import json
from types import SimpleNamespace
from unittest.mock import patch
import subprocess
import sys
import tempfile
import unittest

LAUNCHER = Path(__file__).resolve().parents[1] / "os/linux/fallout2-armada"


class LauncherTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.bin = self.root / "package/bin"
        self.bin.mkdir(parents=True)
        shutil.copy(LAUNCHER, self.bin)
        (self.bin / "fallout2-ce").write_text('#!/usr/bin/env python3\nimport sys, os, json\nfrom pathlib import Path\nPath("argv.txt").write_text(repr(sys.argv[1:]))\nPath("keyboard-env.json").write_text(json.dumps({k: os.environ.get(k) for k in ("SDL_ENABLE_SCREEN_KEYBOARD", "SDL_ENABLE_STEAM_SCREEN_KEYBOARD")}))\nprint("engine diagnostic")\n')
        (self.bin / "fallout2-ce").chmod(0o755)
        self.assets = self.root / "package/share/fallout2-armada/ce.dat"
        self.assets.mkdir(parents=True)
        shutil.copy(LAUNCHER.with_name("armada_desktop.py"), self.assets.parent)
        self.env_config = self.root / "config"
        self.game = self.root / "game with spaces"
        self.game.mkdir()
        for name in ("MASTER.DAT", "critter.dat", "fallout2.cfg", "SAVEGAME"):
            (self.game / name).write_text(name)
        self.env = dict(os.environ, XDG_STATE_HOME=str(self.root / "state"), XDG_CONFIG_HOME=str(self.env_config))

    def tearDown(self):
        self.temp.cleanup()

    def run_launcher(self, *args):
        return subprocess.run([sys.executable, str(self.bin / "fallout2-armada"), "--game-dir", str(self.game),
                               "--mode", "desktop", *args], env=self.env, text=True, capture_output=True)

    def test_paths_arguments_and_preservation(self):
        result = self.run_launcher("--", "literal $(do-not-run)", "with spaces")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.game / "argv.txt").read_text(), "['literal $(do-not-run)', 'with spaces']")
        self.assertEqual((self.game / "ce.dat").resolve(), self.assets)
        for name in ("MASTER.DAT", "critter.dat", "fallout2.cfg", "SAVEGAME"):
            self.assertEqual((self.game / name).read_text(), name)
        self.assertIn("engine diagnostic", (self.root / "state/fallout2-armada/launch.log").read_text())

    def test_kde_connector_identity_scaling_and_override(self):
        resolve = runpy.run_path(str(LAUNCHER))["desktop_outputs"]
        def output(name, width, height, rotation=1, scale=1, y=0):
            return {"name": name, "enabled": True, "currentModeId": "1", "pos": {"x": 0, "y": y},
                    "modes": [{"id": "1", "size": {"width": width, "height": height}}],
                    "rotation": rotation, "scale": scale}
        raw = json.dumps({"outputs": [output("HDMI-A-1", 3840, 2160), output("DSI-1", 1080, 1240, 8, 2, 1080), output("DSI-2", 1080, 1920, 8)]}) + "\nextra output"
        env = {"XDG_CURRENT_DESKTOP": "KDE"}
        with patch("subprocess.run", return_value=SimpleNamespace(stdout=raw)):
            resolve(env)
        self.assertEqual(env["FALLOUT_TOP_OUTPUT"], "DSI-2")
        self.assertEqual(env["FALLOUT_TOP_BOUNDS"], "bounds:0,0,1920,1080")
        self.assertEqual(env["FALLOUT_BOTTOM_OUTPUT"], "DSI-1")
        self.assertEqual(env["FALLOUT_BOTTOM_BOUNDS"], "bounds:0,1080,620,540")
        self.assertEqual(env["FALLOUT_BOTTOM_PIXEL_BOUNDS"], "bounds:0,1080,1240,1080")
        env = {"XDG_CURRENT_DESKTOP": "KDE", "FALLOUT_BOTTOM_OUTPUT": "custom-output"}
        with patch("subprocess.run", return_value=SimpleNamespace(stdout=raw)):
            resolve(env)
        self.assertEqual(env["FALLOUT_BOTTOM_OUTPUT"], "custom-output")
        self.assertNotIn("FALLOUT_BOTTOM_BOUNDS", env)

    def test_session_keyboard_selection(self):
        self.assertEqual(self.run_launcher().returncode, 0)
        env = json.loads((self.game / "keyboard-env.json").read_text())
        self.assertEqual(env, {"SDL_ENABLE_SCREEN_KEYBOARD": "1", "SDL_ENABLE_STEAM_SCREEN_KEYBOARD": "0"})
        self.assertEqual(self.run_launcher("--mode", "game", "--single-screen").returncode, 0)
        env = json.loads((self.game / "keyboard-env.json").read_text())
        self.assertEqual(env, {"SDL_ENABLE_SCREEN_KEYBOARD": "1", "SDL_ENABLE_STEAM_SCREEN_KEYBOARD": "1"})

    def test_missing_data(self):
        (self.game / "MASTER.DAT").unlink()
        result = self.run_launcher()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing master.dat", result.stderr)
        self.assertFalse((self.game / "ce.dat").exists())

    def test_existing_assets_are_preserved(self):
        (self.game / "ce.dat").mkdir()
        (self.game / "ce.dat/custom").write_text("custom")
        self.assertEqual(self.run_launcher().returncode, 0)
        self.assertEqual((self.game / "ce.dat/custom").read_text(), "custom")

    def test_simultaneous_launch_is_rejected(self):
        with (self.game / ".fallout2-armada.lock").open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            result = self.run_launcher()
            self.assertIn("already running", result.stderr)
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
