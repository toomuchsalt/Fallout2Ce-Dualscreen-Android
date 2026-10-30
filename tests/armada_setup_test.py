"""Installation safety checks, isolated from the user's home and game files."""
import errno
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'os/linux'))
import armada_desktop as setup


class InstallationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / 'home with spaces'
        self.home.mkdir()
        self.enterContext(patch.object(Path, 'home', return_value=self.home))
        self.enterContext(patch.dict(os.environ, {
            'XDG_CONFIG_HOME': str(self.home / '.config'),
            'XDG_DATA_HOME': str(self.home / '.local/share'),
            'XDG_STATE_HOME': str(self.home / '.local/state'),
        }))
        self.enterContext(patch.object(setup.subprocess, 'run'))
        self.source = self.root / 'download'
        for name in ('bin/fallout2-ce', 'bin/fallout2-companion', 'bin/fallout2-armada',
                     'bin/fallout2-companion-launch', 'share/fallout2-armada/armada_desktop.py',
                     'share/fallout2-armada/armada_setup.py', 'share/fallout2-armada/ce.dat/asset',
                     'Install Fallout 2'):
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('version one')
        self.game = self.home / 'Games/Fallout 2'
        self.game.mkdir(parents=True)
        for name in ('master.dat', 'critter.dat', 'SAVE.DAT', 'fallout2.cfg'):
            (self.game / name).write_text('original')

    def test_install_update_uninstall_preserves_game(self):
        destination = setup.install_bundle(self.source, self.game)
        self.assertEqual(setup.saved_game_dir(), self.game)
        entry = setup.application_entry().read_text()
        self.assertIn(str(destination / 'bin/fallout2-armada'), entry)
        self.assertIn('Terminal=false', entry)
        (self.source / 'bin/fallout2-ce').write_text('version two')
        setup.install_bundle(self.source, self.game)
        self.assertEqual((destination / 'bin/fallout2-ce').read_text(), 'version two')
        setup.shutil.rmtree(self.source)
        self.assertEqual((self.game / 'ce.dat/asset').read_text(), 'version one')
        setup.uninstall_bundle()
        self.assertFalse(destination.exists())
        self.assertFalse(setup.application_entry().exists())
        for name in ('master.dat', 'critter.dat', 'SAVE.DAT', 'fallout2.cfg'):
            self.assertEqual((self.game / name).read_text(), 'original')

    def test_unowned_destination_and_invalid_game_are_preserved(self):
        destination = setup.install_dir()
        destination.mkdir(parents=True)
        (destination / 'personal').write_text('keep')
        with self.assertRaises(setup.UserError):
            setup.install_bundle(self.source, self.game)
        self.assertEqual((destination / 'personal').read_text(), 'keep')
        with self.assertRaises(setup.UserError):
            setup.install_bundle(self.source, self.home)
        self.assertFalse(setup.config_path().exists())

    def test_running_game_blocks_update_and_uninstall(self):
        with setup.installation_lock():
            with self.assertRaises(setup.UserError):
                with setup.installation_lock(exclusive=True):
                    self.fail('exclusive lock acquired')

    def test_broken_link_repaired_and_custom_assets_preserved(self):
        (self.game / 'ce.dat').symlink_to(self.root / 'removed')
        setup.install_bundle(self.source, self.game)
        self.assertTrue((self.game / 'ce.dat/asset').is_file())
        (self.game / 'ce.dat').unlink()
        (self.game / 'ce.dat').mkdir()
        (self.game / 'ce.dat/custom').write_text('keep')
        setup.install_bundle(self.source, self.game)
        self.assertEqual((self.game / 'ce.dat/custom').read_text(), 'keep')

    def test_filesystem_without_symlinks(self):
        with patch.object(Path, 'symlink_to', side_effect=OSError(errno.EOPNOTSUPP, 'unsupported')):
            setup.install_bundle(self.source, self.game)
        self.assertFalse((self.game / 'ce.dat').is_symlink())
        self.assertEqual((self.game / 'ce.dat/asset').read_text(), 'version one')
        (self.source / 'share/fallout2-armada/ce.dat/asset').write_text('updated')
        setup.install_bundle(self.source, self.game)
        self.assertEqual((self.game / 'ce.dat/asset').read_text(), 'updated')

    def test_failed_update_restores_previous_application(self):
        destination = setup.install_bundle(self.source, self.game)
        original = Path.rename
        def fail_new_application(path, target):
            if path.name == 'application':
                raise OSError('simulated filesystem failure')
            return original(path, target)
        with patch.object(Path, 'rename', fail_new_application):
            with self.assertRaises(OSError):
                setup.install_bundle(self.source, self.game)
        self.assertEqual((destination / 'bin/fallout2-ce').read_text(), 'version one')
        self.assertEqual((self.game / 'SAVE.DAT').read_text(), 'original')

    def test_update_refuses_previous_game_inside_application(self):
        destination = setup.install_bundle(self.source, self.game)
        moved = destination / 'old game'
        self.game.rename(moved)
        setup.save_game_dir(moved)
        other = self.home / 'Games/Other'
        other.mkdir()
        for name in ('master.dat', 'critter.dat'):
            (other / name).write_text('other')
        (self.source / 'bin/fallout2-ce').write_text('update')
        with self.assertRaises(setup.UserError):
            setup.install_bundle(self.source, other)
        self.assertEqual((moved / 'SAVE.DAT').read_text(), 'original')
        self.assertEqual((destination / 'bin/fallout2-ce').read_text(), 'version one')
        self.assertEqual(setup.saved_game_dir(), moved)
        # Moving the original game back out makes updating safe again.
        moved.rename(self.game)
        setup.save_game_dir(self.game)
        setup.install_bundle(self.source, other)
        self.assertEqual((self.game / 'SAVE.DAT').read_text(), 'original')
        self.assertEqual((destination / 'bin/fallout2-ce').read_text(), 'update')

    def test_uninstall_refuses_game_moved_inside_application(self):
        destination = setup.install_bundle(self.source, self.game)
        moved = destination / 'game'
        self.game.rename(moved)
        setup.save_game_dir(moved)
        with self.assertRaises(setup.UserError):
            setup.uninstall_bundle()
        self.assertEqual((moved / 'SAVE.DAT').read_text(), 'original')


if __name__ == '__main__':
    unittest.main()
