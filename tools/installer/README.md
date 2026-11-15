# Installer (for players)

A small window that sets the game up from your own Driver 2 and Driver 1.

1. Unzip the game, copy `Install_ReExtended.bat` and `Install_ReExtended.ps1` next to `REDRIVER2_dev.exe` (or ship them in the zip).
2. Double-click `Install_ReExtended.bat`.
3. Choose your Driver 2 (disc 1 drive, folder with `LEVELS\CHICAGO.LEV`, or `.iso`) and optionally your Driver 1 (folder with `LEVELS\MIAMI.LEV`, disc drive or `.iso`). Subfolders called `DRIVER2` / `DRIVER1` are found automatically.
4. Click **Install**. It copies the files into `DRIVER2\` and `DRIVER1\` (existing game files are never overwritten), converts the Driver 1 cities (about a minute) and shows a message when the game can be started. **Start game** launches it.

Why PowerShell + WinForms: both ship with every Windows, so there is no extra download, no compiler and no package; the script is readable and editable. UI language follows Windows (Dutch or English).

Nothing of Driver 1 or Driver 2 is part of this repo: the installer only reads your files and writes into the game folder. `.bin/.cue` images are not supported; mount them or copy the files out first.

Headless test: `powershell -File Install_ReExtended.ps1 -Auto -Driver2 <path> -Driver1 <path> -GameDir <copy of an install>`.
