# NVWM

Vim-inspired tiling window manager for X11 with modal controls, workspaces, floating support, a configurable bar, and optional compositor effects.

NVWM uses a modal workflow:

- `INSERT` for normal desktop input
- `NORMAL` for window-manager actions
- `COMMAND` for vim-style commands such as `:q!`

Windows are managed in a tiling tree. New windows are attached near the focused one. Floating windows stay above tiled ones and can be moved or resized with `Super+mouse`. Multi-monitor setups are handled through Xinerama.
## Dependencies

- `libX11`
- `libXinerama`
- C99 compiler

Examples:

- Arch Linux: `sudo pacman -S libx11 libxinerama`
- Artix Linux: `sudo pacman -S libx11 libxinerama`
- Void Linux: `sudo xbps-install libX11-devel libxinerama-devel`
- Gentoo: `emerge x11-libs/libX11 x11-libs/libXinerama`
- Debian / Ubuntu: `sudo apt install libx11-dev libxinerama-dev build-essential`
- Fedora: `sudo dnf install libX11-devel libXinerama-devel gcc make`
- openSUSE: `sudo zypper install libX11-devel libXinerama-devel gcc make`
- Alpine: `sudo apk add libx11-dev libxinerama-dev build-base`

## Full Install Example


```bash
git clone https://github.com/Vifuddyxg/nvwm
cd nvwm
make
sudo make install
mkdir -p ~/.config/nvwm
cp config.conf ~/.config/nvwm/config.conf
```

If you also want compositor effects:

```bash
mkdir -p ~/.config/nvwm
cp picom.conf ~/.config/nvwm/picom.conf
```

Then enable picom from `~/.config/nvwm/config.conf`:

```conf
autostart = picom --config ~/.config/nvwm/picom.conf
```

If you want the animation fork instead of upstream `picom`, install your preferred fork first and point the autostart line at that binary.

Add to `~/.xinitrc`:

```sh
exec nvwm
```

## Greeter / XSession Example

If you want to add NVWM to your greeter, one simple setup looks like this:

`/usr/share/xsessions/nvwm.desktop`

```ini
[Desktop Entry]
Name=NVWM
Comment=Neovim Window Manager
Exec=/usr/local/bin/nvwm-session
Type=XSession
DesktopNames=nvwm
```

`/usr/local/bin/nvwm-session`

```
#!/bin/sh
export XDG_CURRENT_DESKTOP=nvwm
export XDG_SESSION_DESKTOP=nvwm
export XDG_SESSION_TYPE=x11

exec dbus-run-session sh -lc '
  /usr/local/bin/ensure-audio-session
  exec nvwm
'
```

Make the session launcher executable:

```bash
sudo chmod +x /usr/local/bin/nvwm-session
```

If you use `greetd` with `tuigreet`, that is enough.
`tuigreet` will pick up `NVWM` automatically from `/usr/share/xsessions/nvwm.desktop`.

## Configuration

NVWM uses a single active config file:

- `~/.config/nvwm/config.conf`

If that file does not exist, it falls back to:

- `/etc/nvwm/config.conf`

No recompile needed for config changes.

Example:

```conf
gap            = 8
border         = 2
bar_height     = 24
bar_position   = bottom
border_focus   = 7aa2f7
border_normal  = 2f3549
terminal       = kitty

bind_insert = mod+q = spawn:kitty
bind_insert = mod+Space = spawn:rofi -show drun
bind_insert = mod+1 = wm:workspace:1
bind_normal = i = wm:mode:insert
command = :q! = wm:quit
```

Use `config.conf` for behavior, keybinds, rules, bar settings, colors, spacing, and autostart.

Supported modifiers:

- `mod` = Super
- `shift`
- `ctrl`
- `alt`

## Compositor

If you want transparency, blur, shadows, rounded corners, and smoother fades, use a compositor instead of building those effects into the window manager.

A sample `picom.conf` is included in the repo. One simple setup is:

```
mkdir -p ~/.config/nvwm
cp picom.conf ~/.config/nvwm/picom.conf
```

Then add this to `config.conf`:

```conf
autostart = picom --config ~/.config/nvwm/picom.conf
```

Use `~/.config/nvwm/picom.conf` to customize blur, transparency, rounded corners, shadows, and fade speed.

Recommended package examples:

- Arch Linux / Artix Linux: `sudo pacman -S picom`
- Void Linux: `sudo xbps-install -S picom`
- Gentoo: `emerge x11-misc/picom`
- Debian / Ubuntu: `sudo apt install picom`
- Fedora: `sudo dnf install picom`
- openSUSE: `sudo zypper install picom`
- Alpine: `sudo apk add picom`

`nvwm` works without `picom`. The compositor is optional.

## Bar

The bar is split into three sections:

- `bar_left`
- `bar_center`
- `bar_right`

Available elements:

- `command`
- `title`
- `workspaces`
- `battery`
- `clock`
- `mode`

Example:

```conf
bar_left   = command,title,workspaces
bar_center =
bar_right  = battery,clock
bar_padding_x = 10
bar_item_gap = 6
bar_text_padding = 8
bar_workspace_min_width = 20
```

## Modes

### INSERT

Applications receive keyboard input normally.

### NORMAL

Window-manager keys are handled directly.

Default pattern:

- `Super+Escape` enters `NORMAL`
- `i` returns to `INSERT`
- `:` enters `COMMAND`

### COMMAND

Commands are typed in the bar and executed with `Enter`.

Example:

```text
:q!
```

Command mode supports three forms:

- `:command` for internal NVWM commands
- `/command` to run a shell command in the background
- `:t command` to run a shell command inside the configured terminal

Examples:

```text
:q!
/librewolf
:t htop
```

You can also define your own command aliases in `config.conf`:

```conf
command = :lock = spawn:slock
command = :music = spawn:kitty -e ncmpcpp
command = :browser = spawn:librewolf
```

## Keybinds

The exact behavior depends on your config, but the default setup includes:

| Key | Action |
| --- | --- |
| `Super+q` | Open terminal in `INSERT` mode |
| `Super+Escape` | Enter `NORMAL` mode |
| `i` | Return to `INSERT` mode |
| `q` | Open terminal in `NORMAL` mode |
| `:` | Enter `COMMAND` mode |
| `/` | Enter shell command mode |
| `Super+1..9` | Switch workspace |
| `Super+Ctrl+1..9` | Move focused window to workspace |
| `Super+Left/Right/Up/Down` | Focus windows |
| `Super+Ctrl+Left/Right/Up/Down` | Swap windows |
| `Super+j / Super+k` | Focus down / up |
| `Print` | Screenshot the full screen |
| `Super+f` | WM fullscreen |
| `Super+v` | Toggle floating |
| `Super+Button1` | Move floating window |
| `Super+Button3` | Resize floating window |
| `:q!` | Quit NVWM |
| `:w!` | Reload config |

## Floating

Floating windows stay above tiled ones and do not reserve tiled space.

- toggle floating from `NORMAL` mode
- drag with `Super+Button1`
- resize with `Super+Button3`
- `Super+Button1` or `Super+Button3` on a tiled window will turn it into a floating window first

Dragging a floating window to another monitor moves it to that monitor.

Dialogs and transient windows are floated automatically.

## Workspaces

NVWM provides workspaces `1..9`.

- each monitor keeps its own tree per workspace
- switching workspace only shows windows from that workspace
- focused windows can be moved to another workspace from keybinds

Simple rules are supported in `config.conf`:

```conf
rule = class:Librewolf = workspace:2
rule = class:Pavucontrol = float
rule = title:Open File = float
```

Rules can match `class`, `instance`, or `title`, and can apply `float`, `tile`, or `workspace:n`.

## Multi-Monitor

Each monitor keeps its own independent layout tree.

- new windows open on the monitor under the pointer
- focus and tiling stay local to that monitor
- floating windows can move across monitor boundaries

## Notes

- `config.conf` is the only active configuration file
- after `sudo make install`, copy `config.conf` into `~/.config/nvwm/config.conf` if you want a per-user config
- media and brightness keys are optional
- `Super+f` uses the WM fullscreen mode, while applications can still request real fullscreen through EWMH
