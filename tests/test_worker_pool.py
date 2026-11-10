"""Linux concurrency regression for the bounded worker pool.

Build first with make. This starts and stops its own four-worker server on
loopback; it is a correctness check, not a performance benchmark.
"""

import concurrent.futures
import hashlib
import json
import os
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path


def main():
    repo = Path(__file__).resolve().parents[1]
    results = repo / "test-results"
    results.mkdir(exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix="parallel-", dir=results))
    print("Evidence directory:", run_dir, flush=True)

    with socket.socket() as chooser:
        chooser.bind(("127.0.0.1", 0))
        port = chooser.getsockname()[1]

    env = os.environ.copy()
    env["THREADRUNNER_WORKERS"] = "4"

    def check_request(index):
        start = 1000 + index * 100
        expected = start + index % 32
        priority = 1 + index % 16
        digest = hashlib.sha256(struct.pack("<Q", expected)).digest()
        request = digest + struct.pack(">QQB", start, start + 32, priority)

        with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
            for chunk in (request[:13], request[13:37], request[37:]):
                sock.sendall(chunk)
                time.sleep(0.001)

            response = bytearray()
            while len(response) < 8:
                chunk = sock.recv(8 - len(response))
                if not chunk:
                    raise RuntimeError(
                        f"Request {index}: connection closed before 8 bytes"
                    )
                response.extend(chunk)

            answer = struct.unpack(">Q", response)[0]
            if answer != expected:
                raise RuntimeError(
                    f"Request {index}: expected {expected}, received {answer}"
                )
            if sock.recv(1) != b"":
                raise RuntimeError(f"Request {index}: unexpected extra bytes")
        return True

    with (run_dir / "server.log").open("w") as server_log:
        server = subprocess.Popen(
            [str(repo / "server"), str(port)],
            cwd=repo, env=env, stdout=server_log, stderr=subprocess.STDOUT,
        )
        stalled = None
        try:
            for _ in range(100):
                if server.poll() is not None:
                    raise RuntimeError("Server exited during startup")
                try:
                    stalled = socket.create_connection(
                        ("127.0.0.1", port), timeout=0.1
                    )
                    break
                except OSError:
                    time.sleep(0.05)
            else:
                raise RuntimeError("Server did not become ready")

            stalled.sendall(b"\x00" * 13)
            with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
                answers = list(pool.map(check_request, range(64)))

            if server.poll() is not None:
                raise RuntimeError("Server exited during concurrent requests")
            thread_count = len(list(Path(f"/proc/{server.pid}/task").iterdir()))
            if thread_count != 5:
                raise RuntimeError(
                    f"Expected main thread + 4 workers; found {thread_count}"
                )

            summary = {
                "workers": 4,
                "server_threads_including_main": thread_count,
                "client_threads": 12,
                "completed_requests": len(answers),
                "correct_answers": sum(answers),
                "incomplete_connection_held_open": True,
                "source_sha256": hashlib.sha256(
                    (repo / "server.c").read_bytes()
                ).hexdigest(),
                "passed": len(answers) == 64 and all(answers),
            }
            (run_dir / "summary.json").write_text(
                json.dumps(summary, indent=2) + "\n"
            )
            print(json.dumps(summary, indent=2))
        finally:
            if stalled is not None:
                stalled.close()
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


if __name__ == "__main__":
    main()
