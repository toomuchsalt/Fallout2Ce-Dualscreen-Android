# Install Fallout 2 on an AYN Thor running Armada

Everything below uses the touchscreen, file manager, and Steam menus. You do not need a terminal, administrator password, or system packages. The application installs into your Home folder and works with Armada's immutable system.

## 1. Open Desktop Mode

In Steam, open **Steam → Power → Switch to Desktop**. Open **Files** (Dolphin) from the taskbar or application menu. Use the Thor's touchpad controls or a mouse if that is easier.

## 2. Copy your game files

You need your own complete Fallout 2 installation; this download does not include the commercial game.

In Files, open **Home**, create a folder named **Games**, and copy your complete Fallout 2 folder into it. A USB drive or microSD card can be used to transfer it from another computer. Open the copied folder and check that **master.dat** and **critter.dat** are directly inside it. Keep all the other game files and folders too. Do not place your game files inside the downloaded application folder.

## 3. Download and extract the Armada application

On the project's GitHub **Releases** page, expand **Assets** and download **Fallout2-Dualscreen-Armada-aarch64.tar.gz**. Choose the Armada download, rather than the Android APK or GitHub's source-code download. The accompanying `.sha256` file is an optional checksum, not the application.

Open **Downloads** in Files. Right-click the downloaded archive and select **Extract → Extract archive here**. Open the extracted **Fallout2-Armada** folder. Keep its contents together. **START HERE.html** is an offline copy of the installation guide.

If you downloaded a GitHub Actions artifact instead, extract its outer ZIP first, then extract the `.tar.gz` inside it as above.

## 4. Run graphical setup

Double-click **Install Fallout 2** inside the extracted folder. If Files asks whether to execute it, choose **Run**. If it does not run, right-click it, open **Properties → Permissions**, enable **Is executable**, and try again.

Follow the setup window. In the folder picker, select your copied **Fallout 2 folder**, the one containing master.dat and critter.dat, and confirm. Select the folder itself rather than either file.

Setup copies the application to **Home → Applications → Fallout2-Armada**, remembers your game folder, and adds **Fallout 2 (Armada)** to the application menu under **Games**. Your game files and saves stay in your chosen game folder. Once setup finishes successfully, you can delete the extracted download and archive.

## 5. Add it to Steam

While still in Desktop Mode, open Steam. Select **Games → Add a Non-Steam Game to My Library**, tick **Fallout 2 (Armada)**, then select **Add Selected Programs**.

Open the new entry's **Properties → Compatibility** and leave **Force the use of a specific Steam Play compatibility tool** unchecked. This is a native Armada application and uses no Proton setting or launch options.

Return to Game Mode using the desktop shortcut. Open **Library → Non-Steam → Fallout 2 (Armada)** and select **Play**. Turn on the bottom screen using Armada's screen controls; enable the game's dual-screen mode in its settings if needed.

You can also launch the game directly from the desktop application menu.

## Updates, changing folders, and removal

**Update:** close the game, download and extract the new Armada archive, and run its **Install Fallout 2**. Choose the same game folder. Setup replaces the application and preserves your game files and saves. Your existing Steam entry continues to work.

**Move or change game files:** close the game and move the complete game folder with Files. Right-click **Fallout 2 (Armada)** in the desktop application menu and choose **Choose game folder**. Alternatively, run **Install Fallout 2** again and select the new folder.

**Uninstall:** close the game, right-click its desktop application-menu entry, and select **Uninstall application (keep saves)**. Confirm the dialog. Your separate game folder and saves are kept. Remove the Non-Steam shortcut separately through Steam's **Manage → Remove non-Steam game from your library**.

## If something goes wrong

- **Missing master.dat or critter.dat:** choose the folder immediately containing both files. Copy the complete installed game, rather than its installer or a shortcut.
- **Read-only folder:** copy the game into **Home → Games** and select the copy. The game must be able to write its settings and saves.
- **Incomplete download:** extract the whole archive; do not run setup from inside the archive viewer or move only the installer out of its folder.
- **Close the game first:** exit Fallout 2 and other setup windows, then retry the update or removal.
- **Game folder moved or extra-files link broken:** rerun setup and select the current game folder.
- **Second screen blank:** turn on the bottom screen in Armada and check the game's dual-screen setting. Try exiting and relaunching after both screens are awake.

The desktop and Non-Steam-game steps follow [Valve's Desktop Mode guide](https://help.steampowered.com/en/faqs/view/671A-4453-E8D2-323C/). Menu wording may vary slightly between Armada releases.
