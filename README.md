# Addoner

Browse CurseForge and install World of Warcraft addons, plus your own addons
from a local folder. A small Qt Quick app for Omarchy.

- **Browse**: search CurseForge (most popular when the search is empty) and
  install with one key. Required libraries are installed along with an addon.
- **Installed**: everything Addoner put in AddOns, with update checks (on
  launch and with `R`) and Update all. Folders it didn't install are listed
  but never touched.
- **Local**: folders in `local-addons/` get *symlinked* into AddOns, so your
  edits show up after `/reload` in game. **New addon** writes a starter `.toc`
  and `.lua` with a slash command and links it in.
- **Settings**: your CurseForge API key (free from
  [console.curseforge.com](https://console.curseforge.com), or set
  `CURSEFORGE_API_KEY`), the AddOns folder, and the local addons folder.

Settings live in `~/.config/addoner/config.json` (readable only by you), and
the list of installed addons is in `~/.local/share/addoner/installed.json`.

## Keys

| Key | Action |
| --- | --- |
| `1`–`4` | Browse, Installed, Local, Settings |
| `/` | Search CurseForge |
| `↑` `↓` / `J` `K` | Move the selection |
| `Space` | Install, update, link or unlink |
| `Delete` / `X` | Remove or unlink |
| `O` | Open the addon's CurseForge page, or the local folder |
| `R` | Check for updates |
| `U` | Update all |
| `N` | New local addon |
| `Esc` | Leave the search field |
| `?` | Show all shortcuts |
| `Q` | Quit |

## Command line

```sh
addoner search details        # search CurseForge
addoner install 61284         # install or update (with required libraries)
addoner remove 61284
addoner list                  # installed, after checking for updates
addoner update                # update everything
addoner local                 # local addons and whether they're linked
addoner link MyAddon / unlink MyAddon
addoner new MyAddon --title "My Addon" [--interface 16001]
```

Add `--json` to any of them for machine-readable output.

## Build

```sh
bin/build      # build/addoner
bin/test       # tests, offscreen
make install   # install for your user in ~/.local (no sudo), shows up in the launcher
make uninstall
bin/install    # or build and install a system package with makepkg
```
