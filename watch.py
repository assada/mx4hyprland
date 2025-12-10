import asyncio
import logging
import os
import socket
from typing import Optional
from pathlib import Path

from mx_master_4 import MXMaster4, DeviceDisconnectedError

LOG_LEVEL = logging.INFO
IPC_SOCKET_PATH = f"/tmp/mx-haptic-{os.getuid()}.sock"
HYPR_SIG = os.getenv("HYPRLAND_INSTANCE_SIGNATURE")
XDG_RUNTIME = os.getenv("XDG_RUNTIME_DIR")

logging.basicConfig(level=LOG_LEVEL, format='%(asctime)s | %(levelname)s | %(message)s')
logger = logging.getLogger("MXDaemon")

class HapticService:
    """
    Abstration layer for the physical device.
    Handles connection lifecycle and executes blocking HID calls in a thread.
    """
    def __init__(self):
        self._device: Optional[MXMaster4] = None
        self._lock = asyncio.Lock()

    def _get_device(self) -> MXMaster4:
        if self._device:
            return self._device

        logger.info("Scanning for MX Master 4...")
        dev = MXMaster4.find()
        if not dev:
            raise DeviceDisconnectedError("Device not found")

        self._device = dev
        logger.info(f"Connected to {dev.connection_type}")
        return dev

    def _send_sync(self, effect_id: int) -> None:
        """Blocking HID operation."""
        try:
            dev = self._get_device()
            with dev as d:
                d.send_haptic_feedback(effect_id=effect_id)
        except (DeviceDisconnectedError, OSError) as e:
            logger.warning(f"Device error: {e}. Resetting connection.")
            self._device = None
        except Exception as e:
            logger.error(f"Unexpected HID error: {e}")

    async def trigger(self, effect_id: int) -> None:
        """Async entry point. Offloads work to a thread to keep the event loop responsive."""
        async with self._lock:
            await asyncio.to_thread(self._send_sync, effect_id)

class IPCServer:
    """
    Listens for external commands via Unix Domain Socket.
    Protocol: accepts integer strings (e.g., "1", "2") or named aliases.
    """
    def __init__(self, service: HapticService):
        self.service = service

    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            data = await reader.read(100)
            msg = data.decode().strip().lower()

            logger.debug(f"IPC Command received: {msg}")

            effect_id = int(msg)

            if effect_id is not None:
                await self.service.trigger(effect_id)
            else:
                logger.warning(f"Unknown command: {msg}")

        except Exception as e:
            logger.error(f"IPC Error: {e}")
        finally:
            writer.close()
            await writer.wait_closed()

    async def start(self) -> None:
        if os.path.exists(IPC_SOCKET_PATH):
            os.remove(IPC_SOCKET_PATH)

        server = await asyncio.start_unix_server(self.handle_client, path=IPC_SOCKET_PATH)
        logger.info(f"IPC Server listening on {IPC_SOCKET_PATH}")

        # Ensure socket is writable by user (sometimes umask messes this up)
        os.chmod(IPC_SOCKET_PATH, 0o600)

        await server.serve_forever()

class HyprlandMonitor:
    """
    Connects to Hyprland event socket and triggers haptics on window focus change.
    """
    def __init__(self, service: HapticService):
        self.service = service
        self.last_window = ""

    async def start(self) -> None:
        if not HYPR_SIG or not XDG_RUNTIME:
            logger.error("Hyprland environment variables missing.")
            return

        socket_path = Path(XDG_RUNTIME) / "hypr" / HYPR_SIG / ".socket2.sock"

        while True:
            try:
                reader, _ = await asyncio.open_unix_connection(str(socket_path))
                logger.info("Connected to Hyprland")

                while True:
                    data = await reader.readuntil(b'\n')
                    line = data.decode('utf-8', errors='replace').strip()

                    if ">>" not in line: continue

                    cmd, args = line.split(">>", 1)

                    # Logic: Only trigger if the active window actually changed
                    if cmd == "activewindowv2":
                        if args != self.last_window:
                            self.last_window = args
                            # Trigger effect ID 1 (soft pulse) for focus change
                            asyncio.create_task(self.service.trigger(1))

            except (ConnectionRefusedError, FileNotFoundError):
                logger.error("Cannot connect to Hyprland. Retrying in 3s...")
                await asyncio.sleep(3)
            except Exception as e:
                logger.error(f"Hyprland listener crash: {e}. Restarting listener...")
                await asyncio.sleep(1)

async def main():
    service = HapticService()
    ipc = IPCServer(service)
    monitor = HyprlandMonitor(service)

    try:
        await asyncio.gather(
            ipc.start(),
            monitor.start(),
        )
    except asyncio.CancelledError:
        logger.info("Shutting down...")

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
