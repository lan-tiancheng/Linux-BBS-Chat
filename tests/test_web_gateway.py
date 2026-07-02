#!/usr/bin/env python3
import base64
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.request


HOST = "127.0.0.1"


def choose_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind((HOST, 0))
        return probe.getsockname()[1]


def post(port, path, payload=None):
    data = json.dumps(payload or {}).encode("utf-8")
    request = urllib.request.Request(
        f"http://{HOST}:{port}{path}",
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=3) as response:
        return json.loads(response.read().decode("utf-8"))


def wait_for(port, session, cursor, predicate, timeout=4):
    deadline = time.time() + timeout
    seen = []
    while time.time() < deadline:
        result = post(port, "/api/events", {"session": session, "cursor": cursor})
        cursor = result["cursor"]
        for event in result["events"]:
            seen.append(event)
            if predicate(event):
                return cursor, event
        time.sleep(0.1)
    raise AssertionError(f"timed out waiting for expected event; seen={seen!r}")


def collect_until(port, session, cursor, predicate, timeout=4):
    deadline = time.time() + timeout
    seen = []
    while time.time() < deadline:
        result = post(port, "/api/events", {"session": session, "cursor": cursor})
        cursor = result["cursor"]
        seen.extend(result["events"])
        if any(predicate(event) for event in seen):
            return cursor, seen
        time.sleep(0.1)
    raise AssertionError(f"timed out waiting for expected event; seen={seen!r}")


def command(port, session, line):
    post(port, "/api/command", {"session": session, "command": line})


def upload(port, session, line, payload):
    post(
        port,
        "/api/upload",
        {
            "session": session,
            "command": line,
            "data": base64.b64encode(payload).decode("ascii"),
        },
    )


def line_is(expected):
    return lambda event: event.get("line") == expected


def line_starts(prefix):
    return lambda event: event.get("line", "").startswith(prefix)


def main():
    web_port = choose_port()
    backend_port = choose_port()
    root = tempfile.mkdtemp(prefix="bbs-web-")
    env = os.environ.copy()
    env["WEB_PORT"] = str(web_port)
    env["BACKEND_PORT"] = str(backend_port)
    env["BBS_DATA_DIR"] = os.path.join(root, "data")
    env["BBS_LOG_DIR"] = os.path.join(root, "logs")
    env["BBS_UPLOAD_DIR"] = os.path.join(root, "uploads", "chat")
    env["BBS_DOWNLOAD_DIR"] = os.path.join(root, "downloads")
    env["BBS_BACKUP_DIR"] = os.path.join(root, "backup")
    process = subprocess.Popen(
        ["python3", "web/server.py"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
    )
    try:
        deadline = time.time() + 8
        while time.time() < deadline:
            try:
                with urllib.request.urlopen(f"http://{HOST}:{web_port}/api/health", timeout=1) as response:
                    assert response.status == 200
                    break
            except OSError:
                time.sleep(0.1)
        else:
            raise AssertionError("web server did not start")

        created = post(web_port, "/api/session")
        session = created["session"]
        assert created["greeting"].startswith("OK connected"), created
        cursor = 0

        command(web_port, session, "REGISTER 100000001 Webpass1 webalpha")
        cursor, _ = wait_for(web_port, session, cursor, line_is("OK registered"))
        command(web_port, session, "LOGIN webalpha Webpass1")
        cursor, _ = wait_for(web_port, session, cursor, line_is("OK logged in 100000001|webalpha"))

        resumed = post(web_port, "/api/session", {"session": session})
        assert resumed["resumed"] is True, resumed
        assert resumed["currentAccount"] == "100000001", resumed
        assert resumed["currentNickname"] == "webalpha", resumed

        command(web_port, session, "BBS_CREATE web title|web content")
        cursor, _ = wait_for(web_port, session, cursor, line_is("OK post 1 created"))
        command(web_port, session, "BBS_REPLY 1|web reply")
        cursor, _ = wait_for(web_port, session, cursor, line_is("OK reply 1 created"))

        payload = b"web payload\n"
        upload(web_port, session, f"BBS_UPLOAD_POST 1 web.txt {len(payload)}", payload)
        cursor, _ = wait_for(web_port, session, cursor, line_is("OK uploaded web.txt"))

        command(web_port, session, "BBS_LIST")
        cursor, events = collect_until(web_port, session, cursor, line_is("BBS_POSTS_END"))
        lines = [event.get("line", "") for event in events]
        assert "BBS_POSTS_BEGIN" in lines, lines
        assert any(
            line.startswith("BBS_POST 1|webalpha|web title|web content|web.txt|")
            for line in lines
        ), lines
        assert "BBS_POSTS_END" in lines, lines

        command(web_port, session, "BBS_DOWNLOAD_POST 1")
        cursor, event = wait_for(web_port, session, cursor, lambda item: item.get("type") == "file")
        assert event["filename"] == "web.txt", event
        assert base64.b64decode(event["data"]) == payload

        command(web_port, session, "BACKUP webtest")
        cursor, _ = wait_for(web_port, session, cursor, line_starts("OK backup created "))
        post(web_port, "/api/close", {"session": session})
        print("web gateway test passed")
        return 0
    finally:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        shutil.rmtree(root)


if __name__ == "__main__":
    raise SystemExit(main())
