# Function Key Audit

This audit covers the launcher-visible app set from `cfg/apps_catalog.txt`
plus the launcher itself.

It reflects the current source after these consistency updates:

- `calcplus` help moved to `F8`
- `hexview` help moved to `F8`
- `cal26` help moved to `F8`
- `dizzy` help moved to `F8`
- `deminer` help moved to `F8`
- `clipboard` save/export moved to `F5`
- `clipboard` load/import moved to `F7`

Common global convention:

- `F2/F4`: app switching in most apps
- `CTRL+B`: return to launcher/home in most apps

## Function Key Matrix

| App | F1 | F3 | F5 | F6 | F7 | F8 | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| editor | copy | paste | save | select | open | help | dialog flows also reuse `F3` for drive toggle |
| quicknotes | copy | paste | save | select | open | help | dialog flows also reuse `F3` for drive toggle |
| calc plus | copy | paste | - | angle mode | calc mode | help | `F5` is free |
| hex viewer | - | - | - | PETSCII/screen mode | - | help | copy is `C`, not a function key |
| clipboard | - | - | save/export | - | load/import | - | now matches system save/load direction better |
| reu viewer | - | - | - | - | - | - | no local function keys |
| task list | copy | paste | save | save as | open | help | dialog flows also reuse `F3` for drive toggle |
| simple files | - | drive toggle | refresh | - | - | help | `F3` also selects drive in dialogs |
| simple cells | format menu | formula edit | save | - | open | help | copy/paste use `C`/`V` |
| 2048 game | - | - | - | - | - | - | no local function keys |
| calendar 26 | copy day item | paste to day | - | - | next view | help | `F5` is free |
| dizzy kanban | new card | edit card | - | single/double view | cycle focus column | help | `F5` is free |
| read.me | - | - | - | - | - | - | no local function keys |
| ready shell | - | - | - | - | - | - | uses typed `HELP` command instead |
| deminer | - | - | - | - | - | help | now aligned with `F8 = Help` |
| launcher | load all | load selected app | - | - | - | - | `F2/F4` used for next/prev app |

## Consistent Patterns

- `F8 = Help` is now consistent in:
  - editor
  - quicknotes
  - calc plus
  - hex viewer
  - task list
  - simple files
  - simple cells
  - calendar 26
  - dizzy kanban
  - deminer
- `F5` now trends toward save/refresh:
  - save in editor, quicknotes, task list, simple cells, clipboard
  - refresh in simple files
- `F7` now trends toward open/load/navigation:
  - open in editor, quicknotes, task list, simple cells
  - load/import in clipboard
  - next view in calendar 26
  - calc/view focus actions in calc plus and dizzy
- `F2/F4` and `CTRL+B` are strong global conventions across the system.

## Conflicts That Are Still Real

- `F1` is still heavily overloaded:
  - copy in editor, quicknotes, task list, calendar 26
  - new card in dizzy
  - format menu in simple cells
  - load all in launcher
- `F3` is still heavily overloaded:
  - paste in editor, quicknotes, task list, calc plus, calendar 26
  - edit card in dizzy
  - drive toggle / drive select in simple files and several file dialogs
  - formula edit in simple cells
  - load selected app in launcher
- `F6/F7` remain app-specific mode/action keys rather than system-wide semantic keys:
  - `F6` means select, angle mode, save as, mode toggle, or view toggle depending on app
  - `F7` means open, calc mode, next view, cycle focus, or clipboard load/import depending on app
- `ready shell` is structurally different:
  - it already has help via typed `HELP`
  - forcing a function-key help path would be possible, but it is not as naturally aligned with the shell model

## Other Hotkeys

These matter because a lot of the system’s real consistency is outside the function keys.

### System-Level Conventions

- `F2/F4`: app switching
- `CTRL+B`: return to launcher/home
- `RUN/STOP`: often quit, cancel, or close a popup
- `RETURN`: usually open, edit, launch, confirm, or close help

### Control-Key Conventions Already Worth Preserving

- `editor`:
  - `CTRL+A`: save as
  - `CTRL+E`: line end
  - `CTRL+F` / `CTRL+G`: find / find next
  - `CTRL+N` / `CTRL+P`: page down / page up
- `quicknotes`:
  - `CTRL+A`: save as
  - `CTRL+L` / `CTRL+O`: pane switch
  - `CTRL+N` / `CTRL+P`: page next / previous
  - `CTRL+W`: new note
  - `CTRL+R`: rename/title
  - `CTRL+D`: delete note
  - `CTRL+U` / `CTRL+K`: move note
  - `CTRL+F` / `CTRL+G`: find / find next
- `simple files`:
  - `CTRL+N` / `CTRL+P`: next / previous page

### Letter-Key Conventions

- `simple cells`:
  - `C` / `V`: copy / paste
- `simple files`:
  - `C`: copy
  - `N`: copy as
  - `D`: duplicate via other drive
  - `S`: swap panes
- `calendar 26`:
  - `L`: load
  - `R`: rebuild
  - `N`: new
  - `A`: edit
  - `D`: delete
  - `T`: set today
- `dizzy kanban`:
  - `L`: load
  - `R`: rebuild
  - `D/A/S`: toggle card flags
  - `/`: search
  - `C`: clear search
- `deminer`:
  - `B` / `I`: beginner / intermediate
  - `P`: pause
  - `R`: restart
  - `M`: menu
  - `J` / `RETURN`: reveal/open
  - `K` / `SPACE`: flag
- `ready shell`:
  - typed `HELP`, `VER`, and `CLEAR` commands

## Suggestions

Because there is not an established user base yet, this is the easiest stage to normalize conventions. The cost of cleanup is currently code churn, not user retraining.

- Highest-value convention to keep: `F8 = Help`
- Good next targets for help:
  - clipboard
  - reu viewer
  - read.me
  - 2048 game
- Good convention to keep reinforcing:
  - `F5` biased toward save/refresh
  - `F7` biased toward open/load/navigation
- Changes that are still harder even now:
  - a full semantic cleanup of `F1`, `F3`, `F6`, or `F7`
  - those keys already carry meaningful editing and navigation workflows in many apps

## Bottom Line

- `F8 = Help` is now consistent across every app in the shipped set that currently exposes a dedicated help hotkey.
- `clipboard` now aligns better with the system by putting save/export on `F5` and load/import on `F7`.
- The next easy consistency win is adding `F8` help to the apps that still have no dedicated help surface.
- The biggest remaining inconsistencies are not in help anymore; they are in `F1`, `F3`, and the app-specific meaning of `F6/F7`.
