# MX Master 4 Hyprland Integration

A robust, asynchronous Python daemon that integrates the Logitech MX Master 4 mouse with Hyprland window manager. It provides haptic feedback on window focus changes and exposes an IPC socket for controlling haptics from external scripts, terminals, or other applications.

## Features

- **Zero-Latency Architecture:** Fully asynchronous event loop using `asyncio`.
- **Hyprland Integration:** Listens to socket events for immediate feedback on window focus changes.
- **IPC Socket Server:** Control mouse haptics from any shell script, keybind, or application via Unix Domain Socket.
- **Protocol Support:** HID++ communication via Bluetooth or Logi Bolt receiver.
- **Configurable:** Easy to extend for different haptic patterns.

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
uv run daemon.py
```
### External Control (IPC)

Once the daemon is running, you can trigger haptic feedback from anywhere using nc (`netcat`) or `socat`.

#### Trigger a pulse (e.g., for notifications):

```bash
echo "1" | nc -U /tmp/mx-haptic-$(id -u).sock
```

#### Trigger a alert:

```bash
echo "8" | nc -U /tmp/mx-haptic-$(id -u).sock
```

#### Using with Hyprland Binds:

```bash
# haptic feedback on workspace switch
bind = SUPER, 1, workspace, 1
bind = SUPER, 1, exec, echo "pulse" | nc -U /tmp/mx-haptic-$(id -u).sock
```

## Based on

1. https://github.com/mfabijanic/mx4hyprland/tree/feature/mx4-bluetooth
2. https://github.com/MyrikLD/mx4hyprland

## License

MIT
