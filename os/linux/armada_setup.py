"""Graphical setup entrypoint; all application writes stay in the user's home."""
import html
import sys
from pathlib import Path

from armada_desktop import (Cancelled, Dialogs, UserError, choose_game, install_bundle,
                            install_dir, installation_lock, uninstall_bundle)


def main():
    dialogs = Dialogs()
    try:
        if "--uninstall" in sys.argv[1:]:
            if not dialogs.confirm("Remove Fallout 2 (Armada) from the application menu? Your original game folder and saved games will be kept.", "Uninstall"):
                return 0
            with installation_lock(exclusive=True):
                uninstall_bundle()
            dialogs.info("The application has been removed. Your game folder and saved games have been kept.")
            return 0
        if not dialogs.confirm("<p>Install <b>Fallout 2 (Armada)</b> for your account.</p><p>Have your complete Fallout 2 game folder ready on this device. The next window lets you choose it.</p><p>No terminal, administrator password, or system package installation is needed.</p>", "Choose game folder"):
            return 0
        source = Path(__file__).resolve().parents[2]
        game = choose_game(dialogs, forbidden=(source, install_dir()))
        with installation_lock(exclusive=True):
            install_bundle(source, game)
        cleanup_note = "You can now delete the downloaded archive and its extracted folder." if source != install_dir().resolve() else "Keep this application folder in place. You can delete the downloaded archive."
        dialogs.info("<p><b>Setup complete.</b></p><p>Open the desktop application menu, choose <b>Games → Fallout 2 (Armada)</b>.</p><p>For Game Mode, open Steam and choose <b>Games → Add a Non-Steam Game to My Library</b>, then tick <b>Fallout 2 (Armada)</b>.</p><p>" + cleanup_note + " Your game folder and saves stay where you chose them.</p>")
        return 0
    except Cancelled:
        return 0
    except (OSError, UserError) as exc:
        dialogs.error(html.escape(str(exc)))
        return 1


if __name__ == "__main__":
    sys.exit(main())
