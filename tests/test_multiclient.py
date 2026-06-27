#!/usr/bin/env python3
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time


HOST = "127.0.0.1"


def choose_test_port():
    configured = os.environ.get("TEST_PORT")
    if configured:
        return int(configured)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind((HOST, 0))
        return probe.getsockname()[1]


PORT = choose_test_port()


def receive_line(connection):
    data = bytearray()
    while True:
        chunk = connection.recv(1)
        if not chunk:
            raise RuntimeError("connection closed before a complete line")
        if chunk == b"\n":
            return data.decode("utf-8").rstrip("\r")
        data.extend(chunk)


def send_line(connection, text):
    connection.sendall((text + "\n").encode("utf-8"))


def receive_bytes(connection, size):
    data = bytearray()
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise RuntimeError("connection closed during file transfer")
        data.extend(chunk)
    return bytes(data)


def connect_client():
    deadline = time.time() + 3
    while True:
        try:
            connection = socket.create_connection((HOST, PORT), timeout=1)
            connection.settimeout(2)
            greeting = receive_line(connection)
            assert greeting.startswith("OK connected"), greeting
            return connection
        except OSError:
            if time.time() >= deadline:
                raise
            time.sleep(0.05)


def main():
    users_file = tempfile.NamedTemporaryFile(delete=False)
    users_file.close()
    chat_log = tempfile.NamedTemporaryFile(delete=False)
    chat_log.close()
    upload_dir = tempfile.mkdtemp()
    environment = os.environ.copy()
    environment["BBS_USERS_FILE"] = users_file.name
    environment["BBS_CHAT_LOG"] = chat_log.name
    environment["BBS_UPLOAD_DIR"] = upload_dir
    server = subprocess.Popen(
        ["./bin/server", str(PORT)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=environment,
    )
    first = None
    second = None
    third = None
    duplicate = None
    try:
        first = connect_client()
        second = connect_client()

        send_line(first, "PING")
        assert receive_line(first) == "OK PONG"

        send_line(first, "WHO")
        assert receive_line(first) == "ERR login required"

        rejected_payload = b"must-be-drained"
        send_line(first, f"UPLOAD bob rejected.bin {len(rejected_payload)}")
        first.sendall(rejected_payload)
        assert receive_line(first) == "ERR login required"
        send_line(first, "PING")
        assert receive_line(first) == "OK PONG"

        send_line(first, "REGISTER alice pass-a")
        assert receive_line(first) == "OK registered"
        send_line(first, "REGISTER alice pass-a")
        assert receive_line(first) == "ERR username already exists"
        send_line(first, "LOGIN alice wrong-password")
        assert receive_line(first) == "ERR invalid username or password"
        send_line(first, "LOGIN alice pass-a")
        assert receive_line(first) == "OK logged in alice"

        send_line(second, "REGISTER bob pass-b")
        assert receive_line(second) == "OK registered"
        send_line(second, "LOGIN bob pass-b")
        assert receive_line(second) == "OK logged in bob"

        third = connect_client()
        send_line(third, "REGISTER carol pass-c")
        assert receive_line(third) == "OK registered"
        send_line(third, "LOGIN carol pass-c")
        assert receive_line(third) == "OK logged in carol"

        missing_payload = b"discard-for-missing-user"
        send_line(first, f"UPLOAD nobody missing.bin {len(missing_payload)}")
        first.sendall(missing_payload)
        assert receive_line(first) == "ERR recipient does not exist"
        send_line(first, "PING")
        assert receive_line(first) == "OK PONG"

        duplicate = connect_client()
        send_line(duplicate, "LOGIN alice pass-a")
        assert receive_line(duplicate) == "ERR user already logged in"
        send_line(duplicate, "QUIT")
        assert receive_line(duplicate) == "OK bye"
        duplicate.close()
        duplicate = None

        send_line(first, "WHO")
        online = receive_line(first).split()
        assert online[0:2] == ["ONLINE", "3"], online
        assert set(online[2:]) == {"alice", "bob", "carol"}, online

        send_line(first, "GROUP hello-from-first")
        first_event = receive_line(first)
        second_event = receive_line(second)
        third_event = receive_line(third)
        assert first_event.startswith("MSG alice "), first_event
        assert first_event.endswith(" hello-from-first"), first_event
        assert second_event == first_event, (first_event, second_event)
        assert third_event == first_event, (first_event, third_event)

        send_line(first, "PRIVATE bob private-hello")
        assert receive_line(second) == "PMSG alice private-hello"
        assert receive_line(first) == "OK private message sent"

        payload = b"binary\x00file\ncontents\xff"
        send_line(first, f"UPLOAD bob sample.bin {len(payload)}")
        first.sendall(payload)
        assert receive_line(second) == (
            f"FILE_READY alice sample.bin {len(payload)}"
        )
        assert receive_line(first) == "OK uploaded sample.bin for bob"

        replacement = b"must-not-overwrite"
        send_line(first, f"UPLOAD bob sample.bin {len(replacement)}")
        first.sendall(replacement)
        assert receive_line(first) == "ERR upload failed"
        send_line(first, "PING")
        assert receive_line(first) == "OK PONG"

        send_line(second, "DOWNLOAD sample.bin")
        assert receive_line(second) == f"FILE sample.bin {len(payload)}"
        assert receive_bytes(second, len(payload)) == payload

        send_line(first, "DOWNLOAD sample.bin")
        assert receive_line(first) == "ERR file not found"

        send_line(second, "LOGOUT")
        assert receive_line(second) == "OK logged out"
        send_line(first, "PRIVATE bob are-you-there")
        assert receive_line(first) == "OK private message stored for offline user"

        send_line(second, "ECHO second-client")
        assert receive_line(second) == "ECHO second-client"

        send_line(first, "LOGOUT")
        assert receive_line(first) == "OK logged out"
        send_line(first, "GROUP blocked")
        assert receive_line(first) == "ERR login required"

        with open(chat_log.name, encoding="utf-8") as log_file:
            log_contents = log_file.read()
        assert "[GROUP] alice: hello-from-first" in log_contents, log_contents
        assert "[PRIVATE] alice -> bob: private-hello" in log_contents, log_contents
        send_line(first, "QUIT")
        assert receive_line(first) == "OK bye"
        send_line(second, "QUIT")
        assert receive_line(second) == "OK bye"
        send_line(third, "QUIT")
        assert receive_line(third) == "OK bye"
        print("multi-client smoke test passed")
        return 0
    finally:
        if first is not None:
            first.close()
        if second is not None:
            second.close()
        if third is not None:
            third.close()
        if duplicate is not None:
            duplicate.close()
        server.terminate()
        try:
            server.wait(timeout=2)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        if server.returncode not in (-15, 0):
            output = server.stdout.read() if server.stdout else ""
            print(output, file=sys.stderr)
        os.unlink(users_file.name)
        os.unlink(chat_log.name)
        shutil.rmtree(upload_dir)


if __name__ == "__main__":
    raise SystemExit(main())
