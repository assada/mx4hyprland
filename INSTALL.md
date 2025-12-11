# Installation Guide

## 1\. Setup Udev Rules

To allow Python to communicate with the HID device without root privileges, you need to configure udev rules.

1. Copy the rules file:

```bash
sudo cp 99-logitech-mx_master_4.rules /etc/udev/rules.d/99-logitech-mx_master_4.rules
```

2. Reload udev rules:
```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

3. Ensure your user is in the `input` group (you may need to logout/login after this):

```bash
sudo usermod -a -G input $USER
```

## 2\. Systemd Service (Autostart)

Instead of using `exec-once` in Hyprland (which doesn't handle crashes well), use a systemd user service.

1. Create the service file:

```bash
mkdir -p ~/.config/systemd/user/
nano ~/.config/systemd/user/mx-haptics.service
```

2. Paste the following configuration (adjust `WorkingDirectory` to where you cloned the repo):

```bash
[Unit]
Description=MX Master Haptic Feedback Daemon
After=graphical-session.target
Wants=graphical-session.target

[Service]
Type=simple
# REPLACE THIS with your actual path!
WorkingDirectory=/home/YOUR_USER/path/to/master4
ExecStart=/usr/bin/uv run watch.py
ExecReload=/bin/kill -HUP $MAINPID

Restart=on-failure
RestartSec=3
TimeoutStopSec=5
KillMode=mixed
Environment=PYTHONUNBUFFERED=1

[Install]
WantedBy=default.target
```

3. Enable and start the service:

```bash
systemctl --user daemon-reload
systemctl --user enable --now mx-haptics.service
```

4. Check status:

```bash
systemctl --user status mx-haptics.service
```

## 3\. Troubleshooting

If you see "Permission denied" errors in the logs:

1.  Check if the udev rules are applied.
2.  Check if your user is in the `input` group.
3.  Try restarting the computer.
