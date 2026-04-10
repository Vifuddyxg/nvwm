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

Minimal install:

```bash
git clone https://github.com/Vifuddyxg/nvwm
cd nvwm
make
sudo make install
mkdir -p ~/.config/nvwm
cp config.conf ~/.config/nvwm/config.conf
```

If you also want transparency, blur, shadows, or animations, install a compositor such as `picom`.

`picom` support is optional and experimental.
It is not required for NVWM and is not part of the default setup.
Depending on your hardware, driver, GPU, and `picom` build, blur or animations may behave differently.

If you want the lightest NVWM setup, do not install `picom` at all.
NVWM works fine without a compositor.

### Choosing a `picom` build

If you want transparency, blur, shadows, or animations, install a `picom` build that matches the effect level you want:

- No compositor: lightest setup, no transparency, no blur, no animations.
- Plain `picom`: transparency, blur, rounded corners, shadows, fades, but no extra animation fork requirements.
- `FT-Labs-picom`: more advanced animation-focused setup for users who want stronger animation effects.
- Custom fork: you can use your own preferred `picom` fork as long as you point `autostart` at the correct binary.

Project links:

- Plain `picom`: https://github.com/yshui/picom
- `FT-Labs-picom`: https://github.com/HcGys/FT-Labs-picom

Important:

- Different `picom` builds do not support the same flags or the same `animation-*` config keys.
- Do not assume `/usr/local/bin/picom` exists on every system.
- If your distro package provides standard `picom`, the safest default is:


### Option 1: No `picom`

Use this if you want the lightest NVWM setup:

- Do not copy `picom.conf`
- Do not add any `autostart = picom ...` line

### Option 2: Plain `picom`

Use this if you want transparency, blur, shadows, and rounded corners, without custom animation forks.

Install:

- Arch Linux / Artix Linux: `sudo pacman -S picom`
- Void Linux: `sudo xbps-install -S picom`
- Gentoo: `emerge x11-misc/picom`
- Debian / Ubuntu: `sudo apt install picom`
- Fedora: `sudo dnf install picom`
- openSUSE: `sudo zypper install picom`
- Alpine: `sudo apk add picom`

Autostart:

```conf
autostart = picom --config ~/.config/nvwm/picom.conf
```

If you use plain `picom`, keep the config focused on blur, transparency, shadows, rounded corners, and fades.
If the compositor fails to start, remove unsupported `animation-*` keys.

### Build dependencies for `picom` forks

If you want to try a custom `picom` fork, this is the point where you switch from "install a package from the distro" to "build a compositor yourself".
That is useful if you want more advanced animation behavior or a fork with features that the standard distro package does not provide.

Custom forks usually need the normal build toolchain, plus X11, XCB, config, event-loop, and rendering development libraries.
If you only want plain `picom` from your distro for blur, transparency, shadows, and fades, skip this section entirely.

If you want to build a custom `picom` fork such as `FT-Labs-picom`, install the build dependencies first:

- Arch Linux / Artix Linux:

```bash
sudo pacman -S --needed base-devel git meson ninja pkgconf libx11 libxext libxcb xcb-util xcb-util-image xcb-util-renderutil libconfig libev pcre2 pixman dbus uthash
```

- Debian / Ubuntu:

```bash
sudo apt install git meson ninja-build pkg-config uthash-dev libconfig-dev libdbus-1-dev libev-dev libpcre2-dev libpixman-1-dev libx11-xcb-dev libxcb1-dev libxcb-composite0-dev libxcb-damage0-dev libxcb-image0-dev libxcb-present-dev libxcb-randr0-dev libxcb-render0-dev libxcb-render-util0-dev libxcb-shape0-dev libxcb-util-dev libxcb-xfixes0-dev libxext-dev
```

- Gentoo:

```bash
emerge dev-libs/uthash dev-util/meson dev-util/ninja dev-util/pkgconf x11-libs/libX11 x11-libs/libXext x11-libs/libxcb x11-libs/xcb-util x11-libs/xcb-util-image x11-libs/xcb-util-renderutil dev-libs/libconfig dev-libs/libev dev-libs/libpcre2 x11-libs/pixman sys-apps/dbus
```

- Fedora:

```bash
sudo dnf install gcc git meson ninja-build pkgconf-pkg-config uthash-devel libconfig-devel dbus-devel libev-devel pcre2-devel pixman-devel libX11-devel libX11-xcb libXext-devel libxcb-devel xcb-util-image-devel xcb-util-renderutil-devel
```

### Option 3: `FT-Labs-picom`

Use this if you want more advanced animation behavior and a more animation-heavy compositor setup.

GitHub:

- https://github.com/HcGys/FT-Labs-picom

One simple local install example:

```bash
mkdir -p ~/src ~/opt
git clone https://github.com/HcGys/FT-Labs-picom ~/src/FT-Labs-picom
cd ~/src/FT-Labs-picom
meson setup --buildtype=release build --prefix="$HOME/opt/picom-ftlabs"
ninja -C build
ninja -C build install
```

This fork installs launcher symlinks such as `~/opt/picom-ftlabs/bin/compton` and `~/opt/picom-ftlabs/bin/compton-trans`.
Use the `compton` launcher from that prefix in your NVWM autostart line.

### Option 4: Your own fork

If you want to test a custom build separately before making it your default compositor, install it to a separate prefix and point `autostart` at that exact binary:

```conf
autostart = ~/opt/picom-custom/bin/compton --config ~/.config/nvwm/picom.conf
```

This makes it easy to compare multiple `picom` builds without replacing the system package or overwriting another fork you already use.

### After installing your chosen `picom`

Once you have installed the `picom` build you want, copy the sample config:

```bash
<<<<<<< HEAD
cd nvwm
=======
cd ~/nvwm
>>>>>>> 608d015 (Fix window removal and update picom setup docs)
mkdir -p ~/.config/nvwm
cp picom.conf ~/.config/nvwm/picom.conf
```

Then enable it in `~/.config/nvwm/config.conf`.

Plain `picom`:

```conf
autostart = picom --config ~/.config/nvwm/picom.conf
```

`FT-Labs-picom`:

```conf
autostart = ~/opt/picom-ftlabs/bin/compton --config ~/.config/nvwm/picom.conf
```

Your own fork:

```conf
autostart = ~/opt/picom-custom/bin/compton --config ~/.config/nvwm/picom.conf
```

Minimal `~/.xinitrc`:

```sh
exec nvwm
```

Recommended `~/.xinitrc` for a full session:

```sh
exec dbus-run-session sh -lc '
export XDG_CURRENT_DESKTOP=nvwm
export XDG_SESSION_DESKTOP=nvwm
export DESKTOP_SESSION=nvwm
export XCURSOR_THEME=Adwaita
export XCURSOR_SIZE=24
pipewire &
wireplumber &
exec nvwm
'
```

The minimal form may work on some systems, but the `dbus-run-session` variant is the safer default if you want audio/session services to behave normally.


## Greeter / XSession Example

If you want to add NVWM to your greeter, one simple setup looks like this:

Use a session wrapper script. Do not point the greeter directly at `nvwm`, or you may end up with a broken login session.

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

```sh
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

If you want transparency, blur, shadows, rounded corners, fades, or animations, use a compositor instead of building those effects into the window manager.

A sample `picom.conf` is included in the repo and can be copied after you install the `picom` build you want.
Use the installation and activation steps from the `Choosing a picom build` section above, then adjust `~/.config/nvwm/picom.conf` for blur, transparency, rounded corners, shadows, fades, and any fork-specific animation settings you want.

`nvwm` works without `picom`. The compositor is optional and not part of the default setup.

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
