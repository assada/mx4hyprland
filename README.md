# MX Master 4 Hyprland Integration

A robust, asynchronous Python daemon that integrates the Logitech MX Master 4 mouse with Hyprland window manager. It provides haptic feedback on window focus changes and exposes an IPC socket for controlling haptics from external scripts, terminals, or other applications.

## Features

- **Zero-Latency Architecture:** Fully asynchronous event loop using `asyncio`.
- **Hyprland Integration:** Listens to socket events for immediate feedback on window focus changes.
- **IPC Socket Server:** Control mouse haptics from any shell script, keybind, or application via Unix Domain Socket.
- **Protocol Support:** HID++ communication via Bluetooth or Logi Bolt receiver.
- **JSON Configuration:** Map any Hyprland event to haptic effects with granular argument matching.

## Requirements

- Python 3.12+
- Logitech MX Master 4 mouse
- Hyprland window manager
- `hid` and `asyncio` Python library

## Installation

1. Clone the repository:
```bash
git clone https://github.com/assada/mx4hyprland
cd mx4hyprland
```

2. Install dependencies using uv:
```bash
uv sync
```

Or using pip:
```bash
pip install hid asyncio
```

## Usage

### Running the Daemon

It is recommended to run the application as a user systemd service (see INSTALL.md).

To run manually for debugging:

```bash
uv run watch.py
```

### Command Line Arguments

| Argument | Short | Description |
|----------|-------|-------------|
| `--config` | `-c` | Path to config file |
| `--log-level` | `-l` | Log level: `debug`, `info`, `warning`, `error` (default: `info`) |

```bash
uv run watch.py --log-level debug
uv run watch.py -c /path/to/custom/config.json -l debug
```

### Configuration

The daemon loads configuration from (in order of priority):
1. Path specified via `--config` argument
2. `~/.config/mx4hyprland/config.json` (XDG standard)
3. `config.json` in the project directory

If no config is found, the daemon runs without haptic events (IPC still works).

#### Config Format

```json
{
	"default_effect": null,
	"events": {
		"activewindowv2": 1,
		"workspace": 2,
		"openwindow": 3,
		"closewindow": 4,
		"fullscreen": 5,
		"openlayer": {
			"default": 1,
			"args": {
				"swaync-notification-window": 10,
				"rofi": 6
			}
		},
		"closelayer": {
			"default": 1,
			"args": {
				"swaync-notification-window": 11
			}
		}
	}
}
```

| Field | Description |
|-------|-------------|
| `default_effect` | Effect ID for unlisted events. `null` disables it |
| `events` | Dictionary of Hyprland event names |

Event values can be:
- **Integer**: Direct effect ID (e.g., `"workspace": 2`)
- **Object**: Granular control with `default` and `args` for specific event arguments

#### Hyprland Events

Common events you can configure:
- `activewindowv2` - Window focus changed
- `workspace` - Workspace switched
- `openwindow` / `closewindow` - Window opened/closed
- `fullscreen` - Fullscreen toggled
- `openlayer` / `closelayer` - Layer opened/closed (notifications, rofi, etc.)
- `focusedmon` - Monitor focus changed

See [Hyprland Wiki](https://wiki.hyprland.org/IPC/) for full event list.

### External Control (IPC)

Once the daemon is running, you can trigger haptic feedback from anywhere using `socat` or `nc` (netcat).

Socket location: `$XDG_RUNTIME_DIR/mx4hyprland.sock`

```bash
echo "1" | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/mx4hyprland.sock
```

#### Using with Hyprland Binds

```bash
bind = SUPER, 1, workspace, 1
bind = SUPER, 1, exec, echo "1" | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/mx4hyprland.sock
```

### Signal Handling

| Signal | Action |
|--------|--------|
| `SIGHUP` | Reload configuration without restart |
| `SIGTERM` | Graceful shutdown |
| `SIGINT` | Graceful shutdown (Ctrl+C) |

```bash
pkill -HUP -f watch.py
pkill -TERM -f watch.py
```

## Based on

1. https://github.com/mfabijanic/mx4hyprland/tree/feature/mx4-bluetooth
2. https://github.com/MyrikLD/mx4hyprland
3. https://github.com/mfabijanic/hyprlogi

## License

MIT
