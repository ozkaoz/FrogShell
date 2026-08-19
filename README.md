# FrogShell

FrogShell is TreeFrogUI's offline VitaShell-style file manager. It manages the
SD card only and has no networking code or network features.

Controls:

- A: enter a folder or open the action menu for a file
- B: go up / cancel
- Y: mark or unmark an item for multi-select
- X: open actions for the selected item
- SELECT: paste the clipboard into the current folder
- START: create a folder
- L/R: page through entries

The action menu supports copy, cut, paste, rename, delete, new folder, and file
information. Copy/cut operations work recursively for folders.

FrogShell reads the shared TreeFrogUI `skin/skin.txt` colors and the selected
`font=` entry from `frogui/settings.txt`. It searches the same `cubegm/fonts/`
and `frogui/fonts/` directories, with a built-in 8x8 fallback.
