import argparse
import json
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


def collect_output(process: subprocess.Popen[str], received: queue.Queue[str | None]) -> None:
    assert process.stdout is not None
    for line in process.stdout:
        received.put(line)
    received.put(None)


def drain(received: queue.Queue[str | None], lines: list[str]) -> None:
    while True:
        try:
            line = received.get_nowait()
        except queue.Empty:
            return
        if line is not None:
            lines.append(line)


def main() -> int:
    parser = argparse.ArgumentParser(description="End-to-end published SFS server verification.")
    parser.add_argument("exe", type=Path)
    parser.add_argument("world", type=Path)
    parser.add_argument("old_client", type=Path)
    parser.add_argument("expected_rockets", type=int)
    args = parser.parse_args()

    exe = args.exe.resolve()
    world = args.world.resolve()
    old_client = args.old_client.resolve()
    if not exe.is_file():
        raise FileNotFoundError(exe)
    if not world.is_dir():
        raise NotADirectoryError(world)
    if not old_client.is_file():
        raise FileNotFoundError(old_client)

    root = Path(tempfile.gettempdir()) / f"sfs-published-e2e-{os.getpid()}-{int(time.time())}"
    state = root / "server-state.json"
    port = reserve_udp_port()
    server = subprocess.Popen(
        [str(exe), "--world", str(world), "--state", str(state), "--port", str(port), "--autosave", "0"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
    )
    lines: list[str] = []
    received: queue.Queue[str | None] = queue.Queue()
    reader = threading.Thread(target=collect_output, args=(server, received), daemon=True)
    reader.start()

    try:
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                line = received.get(timeout=0.2)
            except queue.Empty:
                if server.poll() is not None:
                    break
                continue
            if line is None:
                break
            lines.append(line)
            if "[启动] UDP" in line:
                break
        else:
            raise TimeoutError("Published server did not become ready.")
        if not any("[启动] UDP" in line for line in lines):
            raise RuntimeError("Published server exited before readiness.")

        client = subprocess.run(
            ["dotnet", str(old_client), str(port), str(args.expected_rockets)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=60,
        )
        print(client.stdout, end="")
        if client.stderr:
            print(client.stderr, end="", file=sys.stderr)
        if client.returncode != 0 or "ORIGINAL_DLL_INTEROP_OK" not in client.stdout:
            raise RuntimeError(f"Original-DLL client failed with exit {client.returncode}.")

        server.send_signal(signal.CTRL_BREAK_EVENT)
        server.wait(timeout=20)
        reader.join(timeout=5)
        drain(received, lines)
        output = "".join(lines)
        print(output, end="")

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            try:
                probe.bind(("127.0.0.1", port))
                port_released = True
            except OSError:
                port_released = False

        saved_location = None
        if state.is_file():
            saved = json.loads(state.read_text(encoding="utf-8"))
            saved_location = saved["World"]["Rockets"]["0"]["Location"]
        state_has_relayed_update = (
            isinstance(saved_location, dict)
            and saved_location.get("X") == 321.25
            and saved_location.get("Y") == -654.5
            and saved_location.get("Address") == "Moon"
        )
        ok = (
            server.returncode == 0
            and state.is_file()
            and state.stat().st_size > 0
            and state_has_relayed_update
            and "[停止] 状态已保存，网络线程已停止，服务端已退出。" in output
            and "Unhandled exception" not in output
            and port_released
        )
        if not ok:
            raise RuntimeError(
                f"Final checks failed: exit={server.returncode}, state={state.is_file()}, "
                f"bytes={state.stat().st_size if state.is_file() else 0}, "
                f"state_has_relayed_update={state_has_relayed_update}, port_released={port_released}"
            )
        print(
            f"PUBLISHED_E2E_OK exit=0 rockets={args.expected_rockets} "
            f"state_bytes={state.stat().st_size} state_update_persisted={state_has_relayed_update} "
            f"port_released={port_released}"
        )
        return 0
    except Exception as exc:
        if server.poll() is None:
            server.kill()
            server.wait(timeout=10)
        reader.join(timeout=5)
        drain(received, lines)
        print("".join(lines), end="")
        print(f"PUBLISHED_E2E_FAIL {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
