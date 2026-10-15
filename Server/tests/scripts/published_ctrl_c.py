import argparse
import os
import queue
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


def reserve_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify published server Ctrl+Break shutdown.")
    parser.add_argument("exe", type=Path)
    parser.add_argument("world", type=Path)
    args = parser.parse_args()

    exe = args.exe.resolve()
    world = args.world.resolve()
    if not exe.is_file():
        raise FileNotFoundError(exe)
    if not world.is_dir():
        raise NotADirectoryError(world)

    run_root = Path(tempfile.gettempdir()) / f"sfs-ctrl-c-{os.getpid()}-{int(time.time())}"
    state = run_root / "server-state.json"
    port = reserve_udp_port()
    command = [
        str(exe),
        "--world", str(world),
        "--state", str(state),
        "--port", str(port),
        "--autosave", "0",
    ]
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
    )

    lines: list[str] = []
    received: queue.Queue[str | None] = queue.Queue()

    def collect() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            received.put(line)
        received.put(None)

    reader = threading.Thread(target=collect, daemon=True)
    reader.start()
    deadline = time.monotonic() + 20
    started = False
    while time.monotonic() < deadline:
        try:
            line = received.get(timeout=0.2)
        except queue.Empty:
            if process.poll() is not None:
                break
            continue
        if line is None:
            break
        lines.append(line)
        if "[启动] UDP" in line:
            started = True
            break

    if not started:
        process.kill()
        process.wait(timeout=10)
        print("".join(lines), end="")
        print("CTRL_C_REGRESSION_FAIL server did not become ready", file=sys.stderr)
        return 1

    process.send_signal(signal.CTRL_BREAK_EVENT)
    try:
        process.wait(timeout=20)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)
        print("".join(lines), end="")
        print("CTRL_C_REGRESSION_FAIL server did not stop", file=sys.stderr)
        return 1

    reader.join(timeout=5)
    while True:
        try:
            line = received.get_nowait()
        except queue.Empty:
            break
        if line is not None:
            lines.append(line)
    output = "".join(lines)
    print(output, end="")

    ok = (
        process.returncode == 0
        and state.is_file()
        and state.stat().st_size > 0
        and "[停止] 状态已保存，网络线程已停止，服务端已退出。" in output
        and "Unhandled exception" not in output
    )
    if not ok:
        print(
            f"CTRL_C_REGRESSION_FAIL exit={process.returncode} state={state} "
            f"state_exists={state.is_file()}",
            file=sys.stderr,
        )
        return 1

    print(f"CTRL_C_REGRESSION_OK exit=0 state={state} bytes={state.stat().st_size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
