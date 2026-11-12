# Addoner

Install World of Warcraft addons from the zips you download, plus your own
addons from a local folder. A small Qt Quick app for Omarchy.

- **Downloads**: every addon zip in `~/Downloads` (CurseForge, WoWInterface,
  GitHub's *Download ZIP*), newest first, with whether it's new, installed,
  an update, or older than what you have. Zips without an addon in them are
  left out. Install or update with one key; folders the previous version
  installed are replaced. New downloads show up on their own.
- **Installed**: what Addoner installed, with its version and folders, and
  Update all when a newer zip is in Downloads. Folders it didn't install are
  listed but never touched.
- **Web check**: give an installed addon its web page (**Page…**, or it's
  taken from the `.toc`'s `X-Website`, `X-WoWI-ID` or `X-Curse-Project-ID`)
  and Addoner reads that page at startup, or when you press **Check web**, to
  see if a newer version is out. If so, the addon is marked *new on web* and
  **Open page** takes you there to download it. Nothing is downloaded for
  you. WoWInterface pages and GitHub repositories (latest release, or else
  the newest tag) can be checked. CurseForge puts a bot check in front of its
  site, so its pages can't be read; those addons just get **Open page**.
- **Local**: folders in `local-addons/` get *symlinked* into AddOns, so your
  edits show up after `/reload` in game. **New addon** writes a starter `.toc`
  and `.lua` with a slash command and links it in.
- **Settings**: the AddOns folder, the local addons folder, and the
  downloads folder.

Versions come from each addon's `.toc` (`## Version:`). GitHub archives
(`MyAddon-main/MyAddon.toc`) are unwrapped and installed as `MyAddon`.

Settings live in `~/.config/addoner/config.json`, and the list of installed
addons is in `~/.local/share/addoner/installed.json`.

## Keys

| Key | Action |
| --- | --- |
| `1`–`4` | Downloads, Installed, Local, Settings |
| `↑` `↓` / `J` `K` | Move the selection |
| `Space` | Install, update, open page, link or unlink |
| `Delete` / `X` | Remove or unlink |
| `O` | Open this tab's folder |
| `R` | Look in Downloads again |
| `U` | Update all |
| `C` | Check the web for newer versions |
| `P` | Set the selected addon's web page |
| `W` | Open the selected addon's web page |
| `N` | New local addon |
| `Esc` | Clear the message |
| `?` | Show all shortcuts |
| `Q` | Quit |

## Command line

```sh
addoner downloads             # addon zips in Downloads and their status
addoner install Bagnon-12.1.9.zip   # a file in Downloads, or any path
addoner remove Bagnon         # by main folder name
addoner list                  # installed, and which have an update zip
addoner update                # install every update zip
addoner page Bagnon https://www.wowinterface.com/downloads/info4459-Bagnon.html
addoner check                 # read each addon's page for a newer version
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
