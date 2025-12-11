import argparse
import asyncio
import json
import logging
import os
import signal
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional, Protocol, runtime_checkable

from mx_master_4 import MXMaster4, DeviceDisconnectedError

LOG_LEVELS = {
    "debug": logging.DEBUG,
    "info": logging.INFO,
    "warning": logging.WARNING,
    "error": logging.ERROR,
}

APP_NAME = "mx4hyprland"
CONFIG_FILENAME = "config.json"
XDG_CONFIG_HOME = Path(os.getenv("XDG_CONFIG_HOME", Path.home() / ".config"))
XDG_RUNTIME_DIR = Path(os.getenv("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))

logger = logging.getLogger("MXDaemon")

def setup_logging(level: str) -> None:
    log_level = LOG_LEVELS.get(level.lower(), logging.INFO)
    logging.basicConfig(
        level=log_level,
        format="%(asctime)s | %(levelname)s | %(name)s | %(message)s"
    )

@runtime_checkable
class HapticDevice(Protocol):
    def send_pulse(self, effect_id: int) -> None: ...
    def connect(self) -> None: ...
    def disconnect(self) -> None: ...

class MXMaster4Adapter:
    def __init__(self):
        self._device: Optional[MXMaster4] = None

    def connect(self) -> None:
        if self._device:
            return

        found = MXMaster4.find()
        if not found:
            raise DeviceDisconnectedError("MX Master 4 not found")

        self._device = found
        self._device.open()
        logger.info(f"Connected to {self._device.connection_type}")

    def disconnect(self) -> None:
        if self._device:
            try:
                self._device.close()
            except Exception as e:
                logger.warning(f"Error closing device: {e}")
            finally:
                self._device = None

    def send_pulse(self, effect_id: int) -> None:
        if not self._device:
            self.connect()

        if self._device:
            self._device.send_haptic_feedback(effect_id=effect_id)

@dataclass
class AppConfig:
    default_effect: Optional[int] = None
    events: dict[str, Any] = field(default_factory=dict)

    @classmethod
    def load(cls, path: Optional[Path] = None) -> "AppConfig":
        paths = [path] if path else [
            XDG_CONFIG_HOME / APP_NAME / CONFIG_FILENAME,
            Path(__file__).parent / CONFIG_FILENAME
        ]

        for p in paths:
            if p and p.exists():
                try:
                    with open(p, "r", encoding="utf-8") as f:
                        data = json.load(f)
                    logger.info(f"Config loaded from {p}")
                    return cls(
                        default_effect=data.get("default_effect"),
                        events=data.get("events", {})
                    )
                except Exception as e:
                    logger.error(f"Failed to load config {p}: {e}")

        logger.warning("No config found, using defaults")
        return cls()

    def get_effect(self, event_name: str, event_args: str) -> Optional[int]:
        cfg = self.events.get(event_name)
        if cfg is None:
            return self.default_effect

        if isinstance(cfg, int):
            return cfg

        if isinstance(cfg, dict):
            mapped = cfg.get("args", {}).get(event_args)
            if mapped is not None:
                return mapped
            return cfg.get("default", self.default_effect)

        return self.default_effect

class HapticManager:
    def __init__(self, device: HapticDevice):
        self._device = device
        self._queue: asyncio.Queue[int] = asyncio.Queue(maxsize=10)
        self._worker_task: Optional[asyncio.Task] = None

    async def start(self) -> None:
        self._worker_task = asyncio.create_task(self._worker())
        logger.info("Haptic Manager started")

    async def stop(self) -> None:
        if self._worker_task:
            self._worker_task.cancel()
            try:
                await self._worker_task
            except asyncio.CancelledError:
                pass
        await asyncio.to_thread(self._device.disconnect)

    async def trigger(self, effect_id: int) -> None:
        try:
            self._queue.put_nowait(effect_id)
        except asyncio.QueueFull:
            logger.warning("Haptic queue full, dropping event")

    async def _worker(self) -> None:
        while True:
            effect_id = await self._queue.get()
            try:
                await asyncio.to_thread(self._safe_send, effect_id)
            finally:
                self._queue.task_done()

    def _safe_send(self, effect_id: int) -> None:
        try:
            self._device.send_pulse(effect_id)
        except (DeviceDisconnectedError, OSError):
            logger.warning("Device disconnected, attempting reconnect...")
            self._device.disconnect()
            try:
                self._device.connect()
                self._device.send_pulse(effect_id)
            except Exception as e:
                logger.error(f"Reconnect failed: {e}")
        except Exception as e:
            logger.error(f"Unexpected HID error: {e}")

class IPCServer:
    def __init__(self, manager: HapticManager, socket_path: Path):
        self._manager = manager
        self._socket_path = socket_path

    async def start(self) -> None:
        if self._socket_path.exists():
            self._socket_path.unlink()

        server = await asyncio.start_unix_server(
            self._handle_client,
            path=str(self._socket_path)
        )
        self._socket_path.chmod(0o600)
        logger.info(f"IPC listening on {self._socket_path}")
        await server.serve_forever()

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            data = await reader.read(128)
            message = data.decode().strip()

            if message.isdigit():
                await self._manager.trigger(int(message))
            else:
                logger.debug(f"Received unknown IPC command: {message}")

        except Exception as e:
            logger.error(f"IPC Error: {e}")
        finally:
            writer.close()
            await writer.wait_closed()

class HyprlandListener:
    def __init__(self, manager: HapticManager, config: AppConfig):
        self._manager = manager
        self._config = config
        self._cache: dict[str, str] = {}

    def update_config(self, config: AppConfig) -> None:
        self._config = config
        logger.info("Hyprland listener config updated")

    async def start(self) -> None:
        signature = os.getenv("HYPRLAND_INSTANCE_SIGNATURE")
        if not signature:
            logger.error("HYPRLAND_INSTANCE_SIGNATURE not found")
            return

        socket_path = XDG_RUNTIME_DIR / "hypr" / signature / ".socket2.sock"

        while True:
            reader: Optional[asyncio.StreamReader] = None
            writer: Optional[asyncio.StreamWriter] = None
            try:
                reader, writer = await asyncio.open_unix_connection(str(socket_path))
                logger.info("Connected to Hyprland socket2")

                while True:
                    line_bytes = await reader.readuntil(b'\n')
                    line = line_bytes.decode('utf-8', errors='ignore').strip()

                    if ">>" in line:
                        await self._process_event(line)

            except (FileNotFoundError, ConnectionRefusedError):
                logger.warning("Hyprland socket unreachable, retrying in 3s...")
                await asyncio.sleep(3)
            except asyncio.IncompleteReadError:
                logger.warning("Hyprland connection closed, reconnecting...")
                await asyncio.sleep(1)
            except Exception as e:
                logger.error(f"Listener crashed: {e}")
                await asyncio.sleep(1)
            finally:
                if writer:
                    writer.close()
                    try:
                        await writer.wait_closed()
                    except Exception:
                        pass

    async def _process_event(self, raw_line: str) -> None:
        event, args = raw_line.split(">>", 1)

        # Dedup logic for high-frequency events
        if event in ("workspace", "activewindow", "focusedmon", "activewindowv2"):
            if self._cache.get(event) == args:
                return
            self._cache[event] = args

        effect_id = self._config.get_effect(event, args)
        if effect_id is not None:
            await self._manager.trigger(effect_id)

async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", "--config", type=Path)
    parser.add_argument("-l", "--log-level", default="info")
    args = parser.parse_args()

    setup_logging(args.log_level)

    config = AppConfig.load(args.config)

    device_adapter = MXMaster4Adapter()
    haptic_manager = HapticManager(device_adapter)

    ipc_socket = XDG_RUNTIME_DIR / f"{APP_NAME}.sock"
    ipc_server = IPCServer(haptic_manager, ipc_socket)

    hypr_listener = HyprlandListener(haptic_manager, config)

    loop = asyncio.get_running_loop()
    stop_event = asyncio.Event()

    def reload_config():
        new_config = AppConfig.load(args.config)
        hypr_listener.update_config(new_config)

    loop.add_signal_handler(signal.SIGHUP, reload_config)
    loop.add_signal_handler(signal.SIGTERM, stop_event.set)
    loop.add_signal_handler(signal.SIGINT, stop_event.set)

    await haptic_manager.start()

    tasks = [
        asyncio.create_task(ipc_server.start(), name="ipc"),
        asyncio.create_task(hypr_listener.start(), name="hyprland"),
        asyncio.create_task(stop_event.wait(), name="shutdown"),
    ]

    try:
        done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)

        for task in pending:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

    except asyncio.CancelledError:
        for task in tasks:
            task.cancel()
    finally:
        logger.info("Shutting down...")
        await haptic_manager.stop()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
