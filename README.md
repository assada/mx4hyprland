# MX4Hyprland

Haptic feedback daemon for Logitech MX Master 4 mouse with Hyprland integration.

## Features

- **Modern C++23** with RAII and `std::jthread`
- **TOML configuration** for better readability
- **Hyprland IPC integration** for window events
- **Custom IPC server** for manual haptic triggers
- **Support for both Bolt and Bluetooth** connections

## Building

### Requirements

- C++23 compatible compiler (GCC 13+, Clang 17+)
- Meson >= 1.0.0
- Ninja
- hidapi
- toml++ (fetched automatically if not found)

### Build

```bash
meson setup build --buildtype=release
meson compile -C build
```

### Install

```bash
meson install -C build
```

## Configuration

Configuration file is searched in:
1. `$XDG_CONFIG_HOME/mx4hyprland/config.toml`
2. `./config.toml`

### Example config.toml

```toml
# Default haptic effect for events not explicitly configured
# default_effect = 1

[events]
activewindowv2 = 1
workspace = 2
openwindow = 3
closewindow = 4
fullscreen = 5

[events.openlayer]
default = 1

[events.openlayer.args]
swaync-notification-window = 10
rofi = 6

[events.closelayer]
default = 1

[events.closelayer.args]
swaync-notification-window = 11
```

### Haptic Effects

| ID | Description |
|----|-------------|
| 0  | Stop/Off    |
| 1-15 | Various haptic patterns |

## Usage

```bash
# Run with default config
mx4hyprland

# Specify config file
mx4hyprland -c /path/to/config.toml

# Enable debug logging
mx4hyprland -l debug
```

### Signals

- `SIGHUP` - Reload configuration
- `SIGTERM/SIGINT` - Graceful shutdown

### Manual IPC

Send haptic effect via Unix socket:

```bash
echo "5" | nc -U $XDG_RUNTIME_DIR/mx4hyprland.sock
```

## Systemd Service

Create `~/.config/systemd/user/mx-haptics.service`:

```ini
[Unit]
Description=MX Master 4 Haptic Feedback Daemon
After=graphical-session.target

[Service]
Type=simple
ExecStart=%h/.local/bin/mx4hyprland
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
```

Enable and start:

```bash
systemctl --user enable --now mx-haptics.service
```
