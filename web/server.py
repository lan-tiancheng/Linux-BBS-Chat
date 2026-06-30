#!/usr/bin/env python3
import base64
import json
import os
import socket
import subprocess
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
STATIC_DIR = Path(__file__).resolve().parent / "static"
BACKEND_HOST = os.environ.get("BACKEND_HOST", "127.0.0.1")
BACKEND_PORT = int(os.environ.get("BACKEND_PORT", "8888"))
WEB_HOST = os.environ.get("WEB_HOST", "0.0.0.0")
WEB_PORT = int(os.environ.get("WEB_PORT", "8080"))
AUTO_START_BACKEND = os.environ.get("WEB_AUTO_START_BACKEND", "1") != "0"
MAX_BODY = 20 * 1024 * 1024
IDLE_TIMEOUT_SECONDS = 30 * 60


class BackendSession:
    def __init__(self):
        self.id = uuid.uuid4().hex
        self.sock = socket.create_connection((BACKEND_HOST, BACKEND_PORT), timeout=5)
        self.sock.settimeout(0.2)
        self.lock = threading.Lock()
        self.state_lock = threading.Lock()
        self.buffer = bytearray()
        self.events = []
        self.closed = False
        self.last_activity = time.time()
        self.current_account = ""
        self.current_nickname = ""
        self.greeting = self._read_line_blocking(timeout=3)
        self.reader = threading.Thread(target=self._reader_loop, daemon=True)
        self.reader.start()

    def _read_line_blocking(self, timeout=3):
        deadline = time.time() + timeout
        data = bytearray()
        old_timeout = self.sock.gettimeout()
        self.sock.settimeout(0.2)
        try:
            while time.time() < deadline:
                try:
                    chunk = self.sock.recv(1)
                except socket.timeout:
                    continue
                if not chunk:
                    self.closed = True
                    return ""
                if chunk == b"\n":
                    return data.decode("utf-8", errors="replace").rstrip("\r")
                data.extend(chunk)
        finally:
            self.sock.settimeout(old_timeout)
        return data.decode("utf-8", errors="replace")

    def _recv_exact(self, size):
        data = bytearray()
        if self.buffer:
            count = min(size, len(self.buffer))
            data.extend(self.buffer[:count])
            del self.buffer[:count]
        while len(data) < size:
            chunk = self.sock.recv(size - len(data))
            if not chunk:
                raise ConnectionError("backend closed during file transfer")
            data.extend(chunk)
        return bytes(data)

    def _append_event(self, event):
        with self.state_lock:
            line = event.get("line", "")
            if line.startswith("OK logged in "):
                identity = line.replace("OK logged in ", "", 1).strip()
                account, _, nickname = identity.partition("|")
                self.current_account = account
                self.current_nickname = nickname or account
            elif line == "OK logged out":
                self.current_account = ""
                self.current_nickname = ""
            self.events.append(event)
            if len(self.events) > 300:
                del self.events[: len(self.events) - 300]

    def _reader_loop(self):
        while not self.closed:
            try:
                chunk = self.sock.recv(4096)
                if not chunk:
                    self.closed = True
                    self._append_event({"type": "closed", "line": "connection closed"})
                    break
                self.buffer.extend(chunk)
                self._process_buffer()
            except socket.timeout:
                continue
            except OSError as exc:
                self.closed = True
                self._append_event({"type": "error", "line": str(exc)})
                break

    def _process_buffer(self):
        while b"\n" in self.buffer:
            raw, _, rest = self.buffer.partition(b"\n")
            self.buffer = bytearray(rest)
            line = raw.decode("utf-8", errors="replace").rstrip("\r")
            if line.startswith("FILE ") or line.startswith("BBS_FILE "):
                parts = line.split(" ", 2)
                if len(parts) == 3:
                    try:
                        size = int(parts[2])
                        payload = self._recv_exact(size)
                        self._append_event(
                            {
                                "type": "file",
                                "line": line,
                                "filename": parts[1],
                                "data": base64.b64encode(payload).decode("ascii"),
                            }
                        )
                        continue
                    except (ValueError, OSError, ConnectionError) as exc:
                        self._append_event({"type": "error", "line": str(exc)})
                        continue
            self._append_event({"type": "line", "line": line})

    def send_line(self, line):
        self.touch()
        with self.lock:
            if self.closed:
                raise ConnectionError("backend session is closed")
            self.sock.sendall(line.encode("utf-8") + b"\n")

    def send_upload(self, command, payload):
        self.touch()
        with self.lock:
            if self.closed:
                raise ConnectionError("backend session is closed")
            self.sock.sendall(command.encode("utf-8") + b"\n" + payload)

    def pop_events(self, after):
        events = self.events[after:]
        return len(self.events), events

    def touch(self):
        with self.state_lock:
            self.last_activity = time.time()

    def snapshot(self):
        with self.state_lock:
            return {
                "session": self.id,
                "greeting": self.greeting,
                "currentAccount": self.current_account,
                "currentNickname": self.current_nickname,
                "closed": self.closed,
                "lastActivity": self.last_activity,
            }

    def close(self, logout=False):
        self.closed = True
        if logout:
            try:
                self.sock.sendall(b"LOGOUT\n")
            except OSError:
                pass
        try:
            self.sock.close()
        except OSError:
            pass


sessions = {}
sessions_lock = threading.Lock()
backend_process = None


def start_backend_if_needed():
    global backend_process
    if not AUTO_START_BACKEND:
        return
    try:
        with socket.create_connection((BACKEND_HOST, BACKEND_PORT), timeout=0.3):
            return
    except OSError:
        pass
    subprocess.run(["make", "all"], cwd=ROOT, check=True)
    backend_process = subprocess.Popen(
        [str(ROOT / "bin" / "server"), str(BACKEND_PORT)],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
    )
    deadline = time.time() + 5
    while time.time() < deadline:
        try:
            with socket.create_connection((BACKEND_HOST, BACKEND_PORT), timeout=0.3):
                return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError("backend server did not start")


def json_response(handler, status, payload):
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(data)))
    handler.end_headers()
    handler.wfile.write(data)


def read_json(handler):
    length = int(handler.headers.get("Content-Length", "0"))
    if length > MAX_BODY:
        raise ValueError("request body too large")
    body = handler.rfile.read(length)
    if not body:
        return {}
    return json.loads(body.decode("utf-8"))


def get_session(session_id):
    with sessions_lock:
        session = sessions.get(session_id)
    if session is None or session.closed:
        raise KeyError("invalid session")
    return session


def get_existing_session(session_id):
    if not session_id:
        return None
    with sessions_lock:
        session = sessions.get(session_id)
    if session is None or session.closed:
        return None
    return session


def cleanup_idle_sessions():
    while True:
        time.sleep(30)
        now = time.time()
        expired = []
        with sessions_lock:
            for session_id, session in list(sessions.items()):
                snapshot = session.snapshot()
                if snapshot["closed"]:
                    expired.append(session_id)
                elif now - snapshot["lastActivity"] > IDLE_TIMEOUT_SECONDS:
                    session.close(logout=True)
                    expired.append(session_id)
            for session_id in expired:
                sessions.pop(session_id, None)


class WebHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        return

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/health":
            json_response(self, 200, {"ok": True})
            return
        if path == "/":
            path = "/index.html"
        target = (STATIC_DIR / path.lstrip("/")).resolve()
        if not str(target).startswith(str(STATIC_DIR.resolve())) or not target.exists():
            self.send_error(404)
            return
        content_type = "text/plain"
        if target.suffix == ".html":
            content_type = "text/html; charset=utf-8"
        elif target.suffix == ".css":
            content_type = "text/css; charset=utf-8"
        elif target.suffix == ".js":
            content_type = "application/javascript; charset=utf-8"
        data = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        try:
            payload = read_json(self)
            path = urlparse(self.path).path
            if path == "/api/session":
                session = get_existing_session(payload.get("session", ""))
                resumed = session is not None
                if session is None:
                    session = BackendSession()
                    with sessions_lock:
                        sessions[session.id] = session
                session.touch()
                result = session.snapshot()
                result["resumed"] = resumed
                json_response(self, 200, result)
                return
            session = get_session(payload.get("session", ""))
            if path == "/api/command":
                session.send_line(payload["command"])
                json_response(self, 200, {"ok": True})
                return
            if path == "/api/upload":
                data = base64.b64decode(payload.get("data", ""))
                session.send_upload(payload["command"], data)
                json_response(self, 200, {"ok": True})
                return
            if path == "/api/events":
                cursor = int(payload.get("cursor", 0))
                next_cursor, events = session.pop_events(cursor)
                json_response(self, 200, {"cursor": next_cursor, "events": events})
                return
            if path == "/api/close":
                session.close(logout=bool(payload.get("logout", False)))
                with sessions_lock:
                    sessions.pop(session.id, None)
                json_response(self, 200, {"ok": True})
                return
            self.send_error(404)
        except Exception as exc:
            json_response(self, 400, {"ok": False, "error": str(exc)})


def main():
    start_backend_if_needed()
    threading.Thread(target=cleanup_idle_sessions, daemon=True).start()
    server = ThreadingHTTPServer((WEB_HOST, WEB_PORT), WebHandler)
    print(f"web frontend listening on http://{WEB_HOST}:{WEB_PORT}")
    try:
        server.serve_forever()
    finally:
        if backend_process is not None:
            backend_process.terminate()


if __name__ == "__main__":
    main()
