# Installation Guide

## Quick Install

### User-local (~/.local)

```bash
cd cpp-version
meson setup build --buildtype=release --prefix=$HOME/.local -Duser-install=true
meson compile -C build
meson install -C build

# Enable service
systemctl --user daemon-reload
systemctl --user enable --now mx-haptics.service
```

### System-wide (/usr/local)

```bash
cd cpp-version
meson setup build --buildtype=release
meson compile -C build
sudo meson install -C build

# Enable service
systemctl --user daemon-reload
systemctl --user enable --now mx-haptics.service
```

Both methods install:
- Binary
- Config to `/etc/mx4hyprland/config.toml` (copy to `~/.config/mx4hyprland/` to customize)
- Systemd user service
- Udev rules

---

## Manual Installation

### Prerequisites

- C++23 compatible compiler (GCC 13+, Clang 17+)
- Meson >= 1.0.0
- Ninja
- hidapi
- libudev
- toml++ (fetched automatically if not found)

### Arch Linux

```bash
sudo pacman -S meson ninja hidapi systemd-libs
```

### Debian/Ubuntu

```bash
sudo apt install meson ninja-build libhidapi-dev libudev-dev
```

### Fedora

```bash
sudo dnf install meson ninja-build hidapi-devel systemd-devel
```

## 1. Build

```bash
cd cpp-version
meson setup build --buildtype=release
meson compile -C build
```

## 2. Install Binary

```bash
# System-wide
sudo meson install -C build

# Or user-local
mkdir -p ~/.local/bin
cp build/mx4hyprland ~/.local/bin/
```

## 3. Setup Udev Rules

To allow communication with the HID device without root privileges:

1. Copy the rules file:

```bash
sudo cp 99-logitech-mx_master_4.rules /etc/udev/rules.d/
```

2. Reload udev rules:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

3. Ensure your user is in the `input` group (logout/login required):

```bash
sudo usermod -aG input $USER
```

## 4. Configuration

Create config directory and copy example config:

```bash
mkdir -p ~/.config/mx4hyprland
cp cpp-version/config.toml ~/.config/mx4hyprland/
```

Edit `~/.config/mx4hyprland/config.toml` to customize haptic effects.

## 5. Systemd Service (Autostart)

1. Create the service file:

```bash
mkdir -p ~/.config/systemd/user/
```

2. Create `~/.config/systemd/user/mx-haptics.service`:

```ini
[Unit]
Description=MX Master 4 Haptic Feedback Daemon
After=graphical-session.target
Wants=graphical-session.target

[Service]
Type=simple
ExecStart=%h/.local/bin/mx4hyprland
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=3
TimeoutStopSec=5

[Install]
WantedBy=graphical-session.target
```

3. Enable and start:

```bash
systemctl --user daemon-reload
systemctl --user enable --now mx-haptics.service
```

4. Check status:

```bash
systemctl --user status mx-haptics.service
```

5. View logs:

```bash
journalctl --user -u mx-haptics.service -f
```

## 6. Manual Run (Testing)

```bash
# Normal run
mx4hyprland

# With debug output
mx4hyprland -l debug

# Custom config
mx4hyprland -c /path/to/config.toml
```

## 7. Signals

- `SIGHUP` — Reload configuration without restart
- `SIGTERM/SIGINT` — Graceful shutdown

```bash
# Reload config
systemctl --user reload mx-haptics.service

# Or manually
kill -HUP $(pgrep mx4hyprland)
```

## 8. Troubleshooting

### "MX Master 4 not found"

1. Check if device is connected:
```bash
ls -la /dev/hidraw*
```

2. Run with debug to see what's detected:
```bash
mx4hyprland -l debug
```

3. Verify udev rules are applied:
```bash
udevadm info /dev/hidrawX | grep -E "(ID_|HID_)"
```

### "Permission denied"

1. Check udev rules are in place
2. Verify user is in `input` group: `groups $USER`
3. Try restarting or re-plugging the device

### Service fails to start

1. Check logs: `journalctl --user -u mx-haptics.service`
2. Ensure `HYPRLAND_INSTANCE_SIGNATURE` is set (only works under Hyprland)
3. Try manual run first to isolate the issue
