"""Home-folder installation and GUI support; uses Armada's existing KDE tools."""
import contextlib
import errno
import fcntl
import html
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


class UserError(Exception):
    pass


class Cancelled(Exception):
    pass


def config_path():
    return Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "fallout2-armada/launcher.json"


def state_dir():
    return Path(os.environ.get("XDG_STATE_HOME", str(Path.home() / ".local/state"))) / "fallout2-armada"


def install_dir():
    return Path.home() / "Applications/Fallout2-Armada"


def application_entry():
    return Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "applications/fallout2-armada.desktop"


def atomic_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=".fallout2-", dir=path.parent)
    try:
        with os.fdopen(fd, "w") as stream:
            stream.write(text)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def saved_game_dir():
    try:
        return Path(json.loads(config_path().read_text())["game_dir"])
    except (OSError, ValueError, KeyError, TypeError):
        return None


def save_game_dir(game):
    atomic_text(config_path(), json.dumps({"game_dir": str(game)}, indent=2) + "\n")


def validate_game_dir(game):
    if not game.is_dir():
        raise UserError(f"Game folder not found: {game}\nChoose the folder containing your Fallout 2 installation.")
    names = {p.name.lower() for p in game.iterdir()}
    missing = [name for name in ("master.dat", "critter.dat") if name not in names]
    if missing:
        raise UserError(f"missing {', '.join(missing)} in {game}.\nCopy your complete Fallout 2 installation to the Thor, then choose that folder.")
    if not os.access(game, os.W_OK):
        raise UserError("This game folder is read-only. Copy the complete folder into Games in your Home folder, then choose the copy.")


@contextlib.contextmanager
def installation_lock(exclusive=False):
    folder = state_dir()
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / "installation.lock").open("a") as lock:
        try:
            fcntl.flock(lock, (fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH) | fcntl.LOCK_NB)
        except BlockingIOError:
            raise UserError("Close Fallout 2 and any other Fallout 2 setup window, then try again.")
        yield


class Dialogs:
    title = "Fallout 2 (Armada)"

    def __init__(self):
        if not shutil.which("kdialog"):
            raise UserError("The Armada desktop dialog tool is unavailable. Run setup in Armada's KDE Desktop Mode.")

    def _run(self, *args):
        return subprocess.run(["kdialog", "--title", self.title, *args], text=True, capture_output=True)

    def info(self, message):
        self._run("--msgbox", message)

    def error(self, message):
        self._run("--error", message)

    def confirm(self, message, yes="Continue", no="Cancel"):
        return self._run("--yes-label", yes, "--no-label", no, "--yesno", message).returncode == 0

    def folder(self, initial):
        result = self._run("--getexistingdirectory", str(initial))
        if result.returncode != 0 or not result.stdout.strip():
            raise Cancelled()
        return Path(result.stdout.rstrip("\n")).expanduser().resolve()


def choose_game(dialogs, initial=None, forbidden=()):
    initial = initial or saved_game_dir() or Path.home() / "Games"
    while True:
        game = dialogs.folder(initial)
        try:
            validate_game_dir(game)
            if any(game == p or p in game.parents for p in forbidden):
                raise UserError("Keep your game files outside the downloaded or installed application folder. Move the complete game folder into Games in Home, then choose it again.")
            return game
        except (OSError, UserError) as exc:
            dialogs.error(html.escape(str(exc)))
            initial = game


def prepare_assets(game, assets):
    ce = game / "ce.dat"
    if ce.is_symlink() and not ce.exists():
        raise UserError("The extra game files link is broken. Run Install Fallout 2 again to repair it.")
    if ce.exists():
        return
    try:
        ce.symlink_to(assets, target_is_directory=True)
    except OSError as exc:
        if exc.errno not in (errno.EPERM, errno.EOPNOTSUPP, errno.ENOSYS, errno.EACCES):
            raise
        # FAT/exFAT game folders cannot store symlinks. Copy only our extra
        # assets; original game files and saves are never touched.
        staging = Path(tempfile.mkdtemp(prefix=".fallout2-assets-", dir=game))
        try:
            shutil.copytree(assets, staging / "ce.dat")
            (staging / "ce.dat/.fallout2-armada-assets").write_text("1\n")
            (staging / "ce.dat").rename(ce)
        finally:
            shutil.rmtree(staging)


def desktop_argument(value):
    # Desktop Entry Exec has two escaping layers: string escapes followed by
    # argument quoting. Percent signs must not become field codes.
    value = str(value).replace("%", "%%")
    for original, replacement in (("\\", "\\\\"), ('"', '\\"'), ("`", "\\`"), ("$", "\\$")):
        value = value.replace(original, replacement)
    return ('"' + value + '"').replace("\\", "\\\\")


def write_shortcut(destination):
    launch = desktop_argument(destination / "bin/fallout2-armada")
    setup = desktop_argument(destination / "Install Fallout 2")
    atomic_text(application_entry(), f'''[Desktop Entry]
Type=Application
Name=Fallout 2 (Armada)
Comment=Touch-first Fallout 2 with a dual-screen companion
Exec={launch} --gui
Icon=applications-games
Categories=Game;RolePlaying;
Terminal=false
Actions=ChooseGame;Uninstall;

[Desktop Action ChooseGame]
Name=Choose game folder
Exec={launch} --gui --choose-game

[Desktop Action Uninstall]
Name=Uninstall application (keep saves)
Exec={setup} --uninstall
''')
    if shutil.which("update-desktop-database"):
        subprocess.run(["update-desktop-database", str(application_entry().parent)], capture_output=True, check=False)


def install_bundle(source, game):
    """Caller holds the installation lock and validated the selected game."""
    destination = install_dir()
    required = ("bin/fallout2-ce", "bin/fallout2-companion", "bin/fallout2-armada",
                "bin/fallout2-companion-launch", "share/fallout2-armada/ce.dat",
                "share/fallout2-armada/armada_desktop.py",
                "share/fallout2-armada/armada_setup.py", "Install Fallout 2")
    if not all((source / name).exists() for name in required):
        raise UserError("The download has not been completely extracted. Extract the whole archive, then open Install Fallout 2 inside the extracted folder.")
    validate_game_dir(game)
    if game == destination or destination in game.parents or game == source or source in game.parents:
        raise UserError("Move the game folder outside the application folder before installing.")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and source.resolve() != destination.resolve():
        if not (destination / ".armada-installation").is_file():
            raise UserError("Home → Applications → Fallout2-Armada already exists and was not created by this installer. Rename that folder in Files, then run setup again.")
    if source.resolve() != destination.resolve():
        previous_game = saved_game_dir()
        if previous_game and (previous_game.resolve() == destination.resolve()
                              or destination.resolve() in previous_game.resolve().parents):
            raise UserError("Your previously selected game folder is inside the application folder. Move it elsewhere with Files before updating, so saves are preserved.")
        staging = Path(tempfile.mkdtemp(prefix=".fallout2-install-", dir=destination.parent))
        backup = staging / "previous"
        try:
            application = staging / "application"
            application.mkdir()
            for folder in ("bin", "share"):
                shutil.copytree(source / folder, application / folder)
            shutil.copy2(source / "Install Fallout 2", application / "Install Fallout 2")
            if (source / "START HERE.html").is_file():
                shutil.copy2(source / "START HERE.html", application / "START HERE.html")
            (application / ".armada-installation").write_text("1\n")
            if destination.exists():
                destination.rename(backup)
            try:
                application.rename(destination)
            except OSError:
                if backup.exists():
                    backup.rename(destination)
                raise
        finally:
            # Keep the recovery copy if a filesystem error prevented rollback.
            if not backup.exists() or destination.exists():
                shutil.rmtree(staging)
    elif not (destination / ".armada-installation").exists():
        (destination / ".armada-installation").write_text("1\n")
    # A previous install from a removed download can leave only a dangling
    # symlink. Setup repairs that link; real asset files remain untouched.
    ce = game / "ce.dat"
    if ce.is_symlink() and (not ce.exists() or str(ce.readlink()).endswith("/share/fallout2-armada/ce.dat")):
        ce.unlink()
    # Refresh only copied assets explicitly marked as ours (FAT/exFAT).
    if not ce.is_symlink() and (ce / ".fallout2-armada-assets").is_file():
        staging = Path(tempfile.mkdtemp(prefix=".fallout2-assets-", dir=game))
        try:
            shutil.copytree(destination / "share/fallout2-armada/ce.dat", staging / "new")
            (staging / "new/.fallout2-armada-assets").write_text("1\n")
            ce.rename(staging / "previous")
            try:
                (staging / "new").rename(ce)
            except OSError:
                (staging / "previous").rename(ce)
                raise
        finally:
            if ce.exists() or not (staging / "previous").exists():
                shutil.rmtree(staging)
    prepare_assets(game, destination / "share/fallout2-armada/ce.dat")
    save_game_dir(game)
    write_shortcut(destination)
    return destination


def uninstall_bundle():
    destination = install_dir()
    if destination.exists():
        if not (destination / ".armada-installation").is_file():
            raise UserError("This folder was not created by the installer, so it has not been removed.")
        # The marker only authorizes removal of the application tree. Never
        # remove the configured game directory even if it was moved inside it.
        game = saved_game_dir()
        if game and (game.resolve() == destination.resolve() or destination.resolve() in game.resolve().parents):
            raise UserError("Your game folder is inside the application folder. Move it elsewhere with Files before uninstalling, so saves are preserved.")
        shutil.rmtree(destination)
    for path in (application_entry(), config_path()):
        if path.exists():
            path.unlink()
