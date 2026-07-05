from __future__ import annotations

import json
import queue
import threading
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


class RecordingError(RuntimeError):
    pass


def _dependencies() -> tuple[Any, Any, Any]:
    try:
        import numpy as np
        import sounddevice as sd
        import soundfile as sf
    except ImportError as exc:
        raise RecordingError(
            "Install numpy, sounddevice, and soundfile to use recording"
        ) from exc
    return np, sd, sf


@dataclass
class RecordingSession:
    session_id: str
    directory: Path
    sample_rate: int
    input_channels: int
    output_channels: int
    monitor: bool
    punch_start: float
    punch_end: float | None
    device: int | str | None
    partial_file: Path
    final_file: Path
    journal_file: Path
    block_size: int
    queue_capacity: int
    started_at: float = field(default_factory=time.time)
    sample_cursor: int = 0
    recorded_samples: int = 0
    dropped_blocks: int = 0
    callback_status: str = ""
    state: str = "starting"
    stream: Any = None
    writer_thread: threading.Thread | None = None
    audio_queue: queue.Queue[Any] = field(init=False)
    stop_event: threading.Event = field(default_factory=threading.Event)
    lock: threading.RLock = field(default_factory=threading.RLock)

    def __post_init__(self) -> None:
        self.audio_queue = queue.Queue(maxsize=self.queue_capacity)

    def journal(self) -> dict[str, Any]:
        with self.lock:
            return {
                "schemaVersion": 1,
                "sessionId": self.session_id,
                "state": self.state,
                "startedAt": self.started_at,
                "sampleRate": self.sample_rate,
                "inputChannels": self.input_channels,
                "outputChannels": self.output_channels,
                "monitor": self.monitor,
                "punchStartSeconds": self.punch_start,
                "punchEndSeconds": self.punch_end,
                "sampleCursor": self.sample_cursor,
                "recordedSamples": self.recorded_samples,
                "droppedBlocks": self.dropped_blocks,
                "callbackStatus": self.callback_status,
                "partialFile": str(self.partial_file),
                "finalFile": str(self.final_file),
            }

    def write_journal(self) -> None:
        temporary = self.journal_file.with_suffix(".tmp")
        temporary.write_text(json.dumps(self.journal(), indent=2), encoding="utf-8")
        temporary.replace(self.journal_file)


class RecordingManager:
    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._session: RecordingSession | None = None
        self._takes: list[dict[str, Any]] = []

    def devices(self) -> list[dict[str, Any]]:
        _, sd, _ = _dependencies()
        values: list[dict[str, Any]] = []
        for index, device in enumerate(sd.query_devices()):
            values.append({
                "id": index,
                "name": str(device.get("name", index)),
                "inputChannels": int(device.get("max_input_channels", 0)),
                "outputChannels": int(device.get("max_output_channels", 0)),
                "defaultSampleRate": float(device.get("default_samplerate", 0.0)),
            })
        return values

    def start(self, request: dict[str, Any]) -> dict[str, Any]:
        np, sd, sf = _dependencies()
        del np, sf
        with self._lock:
            if self._session is not None:
                raise RecordingError("A recording session is already active")

            directory = Path(str(request.get("directory", ""))).expanduser().resolve()
            if not str(request.get("directory", "")).strip():
                raise RecordingError("directory is required")
            directory.mkdir(parents=True, exist_ok=True)
            sample_rate = max(8000, min(int(request.get("sampleRate", 48000)), 384000))
            input_channels = max(1, min(int(request.get("inputChannels", 2)), 64))
            monitor = bool(request.get("monitor", False))
            output_channels = input_channels if monitor else 0
            punch_start = max(0.0, float(request.get("punchStartSeconds", 0.0)))
            raw_punch_end = request.get("punchEndSeconds")
            punch_end = None if raw_punch_end in (None, "") else float(raw_punch_end)
            if punch_end is not None and punch_end <= punch_start:
                raise RecordingError("punchEndSeconds must be greater than punchStartSeconds")
            device = request.get("device")
            block_size = max(64, min(int(request.get("blockSize", 512)), 8192))
            queue_capacity = max(8, min(int(request.get("queueBlocks", 256)), 4096))
            session_id = str(uuid.uuid4())
            stamp = time.strftime("%Y%m%d-%H%M%S")
            base = directory / f"take-{stamp}-{session_id[:8]}"
            session = RecordingSession(
                session_id=session_id,
                directory=directory,
                sample_rate=sample_rate,
                input_channels=input_channels,
                output_channels=output_channels,
                monitor=monitor,
                punch_start=punch_start,
                punch_end=punch_end,
                device=device,
                partial_file=base.with_suffix(".partial.wav"),
                final_file=base.with_suffix(".wav"),
                journal_file=base.with_suffix(".recording.json"),
                block_size=block_size,
                queue_capacity=queue_capacity,
            )
            self._session = session

        try:
            self._open_session(session, sd)
        except Exception:
            with self._lock:
                if self._session is session:
                    self._session = None
            raise
        return session.journal()

    def _open_session(self, session: RecordingSession, sd: Any) -> None:
        np, _, sf = _dependencies()

        def writer() -> None:
            try:
                with sf.SoundFile(
                    session.partial_file,
                    mode="w",
                    samplerate=session.sample_rate,
                    channels=session.input_channels,
                    subtype="FLOAT",
                ) as destination:
                    while not session.stop_event.is_set() or not session.audio_queue.empty():
                        try:
                            block = session.audio_queue.get(timeout=0.1)
                        except queue.Empty:
                            continue
                        destination.write(block)
                        destination.flush()
                        with session.lock:
                            session.recorded_samples += int(block.shape[0])
                        session.audio_queue.task_done()
            except Exception as exc:
                with session.lock:
                    session.state = "writer-failed"
                    session.callback_status = str(exc)
                session.stop_event.set()

        def callback(indata: Any, outdata: Any, frames: int, _time_info: Any, status: Any) -> None:
            with session.lock:
                if status:
                    session.callback_status = str(status)
                block_start = session.sample_cursor
                block_end = block_start + frames
                session.sample_cursor = block_end

            if outdata is not None:
                outdata.fill(0)
                if session.monitor:
                    channels = min(outdata.shape[1], indata.shape[1])
                    outdata[:, :channels] = indata[:, :channels]

            punch_start_sample = int(session.punch_start * session.sample_rate)
            punch_end_sample = (
                None if session.punch_end is None
                else int(session.punch_end * session.sample_rate)
            )
            write_start = max(block_start, punch_start_sample)
            write_end = block_end if punch_end_sample is None else min(block_end, punch_end_sample)
            if write_end <= write_start:
                return
            start_offset = write_start - block_start
            end_offset = write_end - block_start
            block = np.array(indata[start_offset:end_offset, :session.input_channels], copy=True)
            try:
                session.audio_queue.put_nowait(block)
            except queue.Full:
                with session.lock:
                    session.dropped_blocks += 1

        session.writer_thread = threading.Thread(
            target=writer,
            name=f"record-writer-{session.session_id[:8]}",
            daemon=True,
        )
        session.writer_thread.start()
        session.stream = sd.Stream(
            samplerate=session.sample_rate,
            blocksize=session.block_size,
            device=session.device,
            channels=(session.input_channels, session.output_channels),
            dtype="float32",
            callback=callback,
        )
        session.stream.start()
        with session.lock:
            session.state = "recording"
        session.write_journal()

    def stop(self) -> dict[str, Any]:
        with self._lock:
            session = self._session
            if session is None:
                raise RecordingError("No recording session is active")
            self._session = None

        with session.lock:
            session.state = "stopping"
        if session.stream is not None:
            session.stream.stop()
            session.stream.close()
        session.stop_event.set()
        if session.writer_thread is not None:
            session.writer_thread.join(timeout=10.0)
            if session.writer_thread.is_alive():
                raise RecordingError("Background writer did not stop")
        if not session.partial_file.is_file():
            raise RecordingError("Recording produced no audio file")
        session.partial_file.replace(session.final_file)
        with session.lock:
            session.state = "completed"
        session.write_journal()
        take = session.journal()
        self._takes.append(take)
        return take

    def status(self) -> dict[str, Any]:
        with self._lock:
            session = self._session
            return {
                "active": session is not None,
                "session": None if session is None else session.journal(),
                "takes": list(self._takes),
            }

    def recover(self, directory: Path) -> dict[str, Any]:
        _, _, sf = _dependencies()
        root = directory.expanduser().resolve()
        root.mkdir(parents=True, exist_ok=True)
        recovered: list[str] = []
        failed: list[dict[str, str]] = []
        for partial in root.glob("*.partial.wav"):
            try:
                info = sf.info(partial)
                if info.frames <= 0 or info.channels <= 0:
                    raise RecordingError("audio file contains no frames")
                final = partial.with_name(partial.name.replace(".partial.wav", ".recovered.wav"))
                partial.replace(final)
                recovered.append(str(final))
            except Exception as exc:
                failed.append({"file": str(partial), "error": str(exc)})
        return {"directory": str(root), "recovered": recovered, "failed": failed}


MANAGER = RecordingManager()
