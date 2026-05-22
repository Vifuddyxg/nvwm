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
- `make`
- `git` if you clone the repository with `git clone`

Examples:

- Arch Linux: `sudo pacman -S git libx11 libxinerama make`
- Artix Linux: `sudo pacman -S git libx11 libxinerama make`
- Void Linux: `sudo xbps-install git gcc make libX11-devel libxinerama-devel`
- Gentoo: `emerge dev-vcs/git x11-libs/libX11 x11-libs/libXinerama`
- Debian / Ubuntu: `sudo apt install git libx11-dev libxinerama-dev build-essential`
- Fedora: `sudo dnf install git libX11-devel libXinerama-devel gcc make`
- openSUSE: `sudo zypper install git libX11-devel libXinerama-devel gcc make`
- Alpine: `sudo apk add git libx11-dev libxinerama-dev build-base`
- NixOS / nix-shell: `nix-shell -p git gcc gnumake xorg.libX11 xorg.libXinerama`
- FreeBSD: `doas pkg install git libX11 libXinerama`
- OpenBSD: install the `xbase` and `xshare` sets; use `doas pkg_add git` if you clone with Git

Runtime tools used by the default config:

- `alacritty` for `terminal = alacritty` and `Super+q`
- `rofi` for the default `Super+Space` launcher
- one screenshot tool: `maim`, `scrot`, or ImageMagick's `import`
- optional media tools if you uncomment those keybinds: `pactl`, `playerctl`, and `brightnessctl`

You can avoid these runtime tools by changing the matching commands in `~/.config/nvwm/config.conf`.

Session tools:

- one X11 server: Xorg or XLibre
- either `startx`/`xinit` or a display manager/greeter that can start X sessions
- `dbus-run-session` if you use the recommended full-session examples below

Install exactly one X server:

- Xorg: use your distro's normal Xorg server package, usually named `xorg-server`, `xserver-xorg`, or part of the BSD X sets.
- XLibre: use your distro's XLibre package if one exists.

Do not install Xorg and XLibre just for NVWM. Pick one server, then run NVWM inside that X11 session.

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

### NixOS

For a temporary build shell:

```bash
nix-shell -p git gcc gnumake xorg.libX11 xorg.libXinerama
make
make install PREFIX="$HOME/.local" SYSCONFDIR="$HOME/.config"
```

For a declarative NixOS setup, add the build/runtime tools you want to your system or user packages, then build NVWM from this repo:

```nix
environment.systemPackages = with pkgs; [
  git
  gcc
  gnumake
  dbus
  xorg.libX11
  xorg.libXinerama
  xorg.xinit
  xorg.xset
  alacritty
  rofi
];
```

`sudo make install` writes to `/usr/local/bin` and `/etc/nvwm`.
That is normal on many Unix systems, but on NixOS a per-user install such as `~/.local/bin/nvwm` is usually simpler unless you package NVWM as a derivation.

### FreeBSD

FreeBSD installs X11 headers and libraries under `/usr/local`, so pass those paths when building:

```sh
doas pkg install git libX11 libXinerama
make \
  CPPFLAGS="-I/usr/local/include" \
  LDFLAGS="-L/usr/local/lib"
doas make install
mkdir -p ~/.config/nvwm
cp config.conf ~/.config/nvwm/config.conf
```

You can change the install location with `PREFIX=/path` or stage a package with `DESTDIR=/path`.
The Makefile does not require GNU `install -D`.

Manual install is also straightforward:

```sh
doas mkdir -p /usr/local/bin /etc/nvwm
doas install -m 755 nvwm /usr/local/bin/nvwm
doas install -m 644 config.conf /etc/nvwm/config.conf
```

### OpenBSD

OpenBSD keeps X11 under `/usr/X11R6`, so pass those paths when building:

```sh
make \
  CPPFLAGS="-I/usr/X11R6/include" \
  LDFLAGS="-L/usr/X11R6/lib"
doas make install
mkdir -p ~/.config/nvwm
cp config.conf ~/.config/nvwm/config.conf
```

If your OpenBSD install does not include X, install the `xbase` and `xshare` sets first.
The Makefile does not require GNU `install -D`; the manual install form shown in the FreeBSD section works too.

### Portability Notes

NVWM is an X11 window manager. It is intended to run inside an X session, not as a Wayland compositor.

NVWM works with Xorg and XLibre, because XLibre is an X11 server forked from X.Org Server with a compatibility goal.
NVWM uses normal Xlib/EWMH behavior and Xinerama for monitor geometry. If Xinerama is not active, NVWM falls back to one monitor using the root window size.

The `battery` bar item currently reads Linux-style battery information from `/sys/class/power_supply`.
On NixOS it works the same as other Linux distributions; on FreeBSD and OpenBSD it may stay empty unless that code is adapted for the platform.

The optional `screen_off_minutes` setting uses `xset`.
Install `xset` if you want that feature on a minimal system, or leave `screen_off_minutes = 0`.

### Xserver Sessions: Xorg and XLibre

NVWM runs inside a normal X11 session. Install one X11 server, not both: the traditional Xorg server or XLibre, an alternative X11 server implementation forked from X.Org Server.

NVWM does not need a separate build for XLibre. Build NVWM against the normal X11 client libraries (`libX11` and `libXinerama`) and run it inside the XLibre session.

If your distribution packages XLibre, prefer those packages. If not, use your distribution's normal Xorg packages. The XLibre project tracks package availability on its wiki:

- https://github.com/X11Libre/xserver/wiki/Are-We-XLibre-Yet%3F

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
command -v pipewire >/dev/null 2>&1 && pipewire &
command -v wireplumber >/dev/null 2>&1 && wireplumber &
exec nvwm
'
```

The minimal form may work on some systems, but the `dbus-run-session` variant is the safer default if you want audio/session services to behave normally.

If you build XLibre from source into a separate prefix, test that server first, then start NVWM on it:

```sh
XSERVER="$HOME/opt/xlibre/bin/X"
startx "$HOME/.local/bin/nvwm" -- "$XSERVER" :1
```

If your `startx` does not accept a server path directly, use `xinit`:

```sh
xinit "$HOME/.local/bin/nvwm" -- "$HOME/opt/xlibre/bin/X" :1
```

For the normal installed system server, keep the regular `~/.xinitrc`:

```sh
exec nvwm
```

Then run:

```sh
startx
```

#### Greeter / XSession

If you want to add NVWM to your greeter, one simple setup looks like this.
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
  command -v pipewire >/dev/null 2>&1 && pipewire &
  command -v wireplumber >/dev/null 2>&1 && wireplumber &
  exec nvwm
'
```

Make the session launcher executable:

```sh
sudo chmod +x /usr/local/bin/nvwm-session
```

If you use `greetd` with `tuigreet`, that is enough.
`tuigreet` will pick up `NVWM` automatically from `/usr/share/xsessions/nvwm.desktop`.

Notes:

- NVWM requires X11 client libraries at build time, not XLibre server headers.
- Multi-monitor support depends on the Xinerama extension being available and active in the running X server.
- Existing Xorg session wrappers and `.desktop` files usually work unchanged if the display manager starts XLibre as the system X server.

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

```conf
autostart = picom --config ~/.config/nvwm/picom.conf
```


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
- NixOS / nix-shell: `nix-shell -p picom`
- FreeBSD: `doas pkg install picom`
- OpenBSD: `doas pkg_add picom`

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
cd ~/nvwm
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

## Configuration

NVWM loads config files in this order:

- `/etc/nvwm/config.conf`
- `./config.conf`
- `~/.config/nvwm/config.conf`

Later files override earlier ones.

No recompile needed for config changes.

Example:

```conf
gap            = 8
border         = 2
bar_height     = 24
bar_position   = bottom
bar_enabled    = true
external_bar_height = 0
border_focus   = 7aa2f7
border_normal  = 2f3549
terminal       = kitty

bind_insert = mod+q = spawn:kitty
bind_insert = mod+shift+q = wm:quit
bind_insert = mod+Space = spawn:rofi -show drun
bind_insert = mod+1 = wm:workspace:1
bind_normal = i = wm:mode:insert
command = :q! = wm:quit
```

Use `config.conf` for behavior, keybinds, rules, bar settings, colors, spacing, and autostart.

Optional monitor power-off is controlled by `screen_off_minutes`.
The default value, `0`, leaves it disabled. Set it above `0` to turn the monitor off after that many idle minutes:

```conf
screen_off_minutes = 10
```

NVWM applies this through `xset` at startup and when config is reloaded with `:w!`.

Supported modifiers:

- `mod` = Super
- `shift`
- `ctrl`
- `alt`

Window border colors are configured separately from bar colors:

- `border_focus`: selected/focused window border
- `border_normal`: unselected window border

Both values are hex RGB and can be written with or without `#`.

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
bar_enabled = true
bar_height = 24
bar_position = bottom
bar_padding_x = 10
bar_item_gap = 6
bar_text_padding = 8
bar_workspace_min_width = 20
bar_bg = 1a1b26
bar_fg = c0caf5
bar_accent_bg = 7aa2f7
bar_accent_fg = 1a1b26
bar_muted_fg = 565f89
```

Bar colors are hex RGB values. You can write them with or without `#`, for example `7aa2f7` or `#7aa2f7`.

- `bar_bg`: full bar background
- `bar_fg`: normal bar text
- `bar_accent_bg`: active workspace / mode background
- `bar_accent_fg`: active workspace / mode text
- `bar_muted_fg`: inactive workspace / muted text

Reload NVWM with `:w!` after changing colors.

### External Bars

NVWM detects external bars automatically. Any X11 bar that sets `_NET_WM_WINDOW_TYPE_DOCK` (polybar, tint2, and most common bars do this by default) is recognized as a dock window. NVWM reads the bar's reserved area from its `_NET_WM_STRUT_PARTIAL` or `_NET_WM_STRUT` hints and adjusts the tiling work area accordingly, so tiled windows never overlap the bar.

This works on both top and bottom simultaneously and is independent of where the internal NVWM bar sits. For example, the internal bar can be at the bottom while polybar sits at the top — NVWM reserves space for both.

**Using only an external bar (replace the built-in bar)**

Disable the internal bar so only the external one is shown:

```conf
bar_enabled = false
autostart = polybar main
```

No other setting is needed. NVWM detects polybar's height automatically when it maps.

**Using an external bar alongside the built-in bar**

Leave `bar_enabled = true`. Both bars coexist on different sides:

```conf
bar_enabled = true
bar_position = bottom
autostart = polybar main
```

NVWM reserves space at the bottom for its own bar and at the top for polybar.

**Bars started outside the NVWM autostart**

If the external bar is launched from `.xinitrc` or a display manager session script before NVWM starts, NVWM scans all existing windows at startup and picks it up automatically.

**`external_bar_height` override**

You normally do not need this setting. Use it only if the bar does not advertise strut hints or if you want to force a specific reserved height:

```conf
external_bar_height = 30
```

Set it to `0` (the default) to let NVWM auto-detect from the bar's own hints. Setting it to a positive number overrides auto-detection for the side matching `bar_position`.

**Fullscreen behavior**

Dock windows are kept above tiled windows at all times. When a client enters EWMH real fullscreen (such as a browser video player), dock windows are lowered so the fullscreen content covers the whole screen. They are raised again when fullscreen exits.

**Good X11 bar choices**

- `polybar`: full-featured, practical default for X11.
- `lemonbar`: very small and script-driven.
- `tint2`: panel/taskbar style, useful if you want a more traditional desktop panel.
- `eww`: flexible widget system for custom bars.
- `xmobar`: good if you like a text/status oriented bar.
- `dzen2`: old but simple and lightweight.

Install only the bar you plan to use. NVWM does not require any external bar.

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
| `Super+Shift+q` | Quit NVWM |
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
| `Super+c` | Close the focused window |
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

- config files are loaded from `/etc/nvwm/config.conf`, then `./config.conf`, then `~/.config/nvwm/config.conf`
- after `sudo make install`, copy `config.conf` into `~/.config/nvwm/config.conf` if you want a per-user config
- use `make install PREFIX="$HOME/.local" SYSCONFDIR="$HOME/.config"` for a per-user install
- media and brightness keys are optional
- `Super+f` uses the WM fullscreen mode, while applications can still request real fullscreen through EWMH
