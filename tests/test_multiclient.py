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
    environment["BBS_FRIENDS_FILE"] = os.path.join(upload_dir, "friends.db")
    environment["BBS_PRIVATE_REQUESTS_FILE"] = os.path.join(upload_dir, "private_requests.db")
    environment["BBS_GROUPS_FILE"] = os.path.join(upload_dir, "groups.db")
    environment["BBS_GROUP_MEMBERS_FILE"] = os.path.join(upload_dir, "group_members.db")
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
        send_line(first, f"UPLOAD beta rejected.bin {len(rejected_payload)}")
        first.sendall(rejected_payload)
        assert receive_line(first) == "ERR login required"
        send_line(first, "PING")
        assert receive_line(first) == "OK PONG"

        send_line(first, "REGISTER 100000001 Alpha123 alpha")
        assert receive_line(first) == "OK registered"
        send_line(first, "REGISTER 100000001 Alpha123 alpha")
        assert receive_line(first) == "ERR account or nickname already exists"
        send_line(first, "LOGIN alpha Wrong123")
        assert receive_line(first) == "ERR invalid login or password"
        send_line(first, "LOGIN alpha Alpha123")
        assert receive_line(first) == "OK logged in 100000001|alpha"

        send_line(second, "REGISTER 100000002 Beta123 beta")
        assert receive_line(second) == "OK registered"
        send_line(second, "LOGIN 100000002 Beta123")
        assert receive_line(second) == "OK logged in 100000002|beta"

        third = connect_client()
        send_line(third, "REGISTER 100000003 Carol123 carol")
        assert receive_line(third) == "OK registered"
        send_line(third, "LOGIN carol Carol123")
        assert receive_line(third) == "OK logged in 100000003|carol"

        missing_payload = b"discard-for-missing-user"
        send_line(first, f"UPLOAD nobody missing.bin {len(missing_payload)}")
        first.sendall(missing_payload)
        assert receive_line(first) == "ERR recipient does not exist"
        send_line(first, "PING")
        assert receive_line(first) == "OK PONG"

        duplicate = connect_client()
        send_line(duplicate, "LOGIN 100000001 Alpha123")
        assert receive_line(duplicate) == "ERR user already logged in"
        send_line(duplicate, "QUIT")
        assert receive_line(duplicate) == "OK bye"
        duplicate.close()
        duplicate = None

        send_line(first, "WHO")
        online = receive_line(first).split()
        assert online[0:2] == ["ONLINE", "3"], online
        assert set(online[2:]) == {
            "100000001|alpha",
            "100000002|beta",
            "100000003|carol",
        }, online

        send_line(first, "PRIVATE_START beta private-hello")
        assert receive_line(second) == "PMSG 100000001 private-hello"
        assert receive_line(first) == "OK private request sent"
        send_line(first, "SENT_REQUESTS")
        assert receive_line(first) == "SENT_REQUESTS_BEGIN"
        assert receive_line(first) == "SENT_REQUEST 100000002|beta|private-hello"
        assert receive_line(first) == "SENT_REQUESTS_END"
        send_line(second, "REQUESTS")
        assert receive_line(second) == "REQUESTS_BEGIN"
        assert receive_line(second) == "REQUEST 100000001|alpha|private-hello"
        assert receive_line(second) == "REQUESTS_END"
        send_line(second, "PRIVATE_REPLY alpha reply-hello")
        assert receive_line(first) == "PMSG 100000002 reply-hello"
        assert receive_line(second) == "OK private message sent"
        send_line(first, "SENT_REQUESTS")
        assert receive_line(first) == "SENT_REQUESTS_BEGIN"
        assert receive_line(first) == "SENT_REQUESTS_END"
        send_line(first, "FRIENDS")
        assert receive_line(first) == "FRIENDS_BEGIN"
        assert receive_line(first) == "FRIEND 100000002|beta"
        assert receive_line(first) == "FRIENDS_END"

        send_line(first, "GROUP_CREATE project beta")
        assert receive_line(first) == "OK group 1 created"
        send_line(first, "GROUP_SEND 1 hello-from-first")
        first_event = receive_line(first)
        second_event = receive_line(second)
        assert first_event == "GMSG 1 100000001 hello-from-first", first_event
        assert second_event == first_event, (first_event, second_event)

        send_line(first, "HISTORY_PRIVATE beta")
        assert receive_line(first) == "PRIVATE_HISTORY_BEGIN"
        private_history = []
        while True:
            line = receive_line(first)
            if line == "PRIVATE_HISTORY_END":
                break
            private_history.append(line)
        assert any("private-hello" in line for line in private_history), private_history

        send_line(first, "HISTORY_GROUP 1")
        assert receive_line(first) == "GROUP_HISTORY_BEGIN"
        group_history = []
        while True:
            line = receive_line(first)
            if line == "GROUP_HISTORY_END":
                break
            group_history.append(line)
        assert len(group_history) == 1, group_history
        assert group_history[0].startswith("HGMSG "), group_history
        assert group_history[0].split("|")[1:] == [
            "1",
            "100000001",
            "hello-from-first",
        ], group_history

        payload = b"binary\x00file\ncontents\xff"
        send_line(first, f"UPLOAD beta sample.bin {len(payload)}")
        first.sendall(payload)
        assert receive_line(second) == (
            f"FILE_READY 100000001 sample.bin {len(payload)}"
        )
        assert receive_line(first) == "OK uploaded sample.bin for beta"

        replacement = b"must-not-overwrite"
        send_line(first, f"UPLOAD beta sample.bin {len(replacement)}")
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
        send_line(first, "PRIVATE_START beta are-you-there")
        assert receive_line(first) == "OK private message sent"

        send_line(second, "ECHO second-client")
        assert receive_line(second) == "ECHO second-client"

        send_line(first, "LOGOUT")
        assert receive_line(first) == "OK logged out"
        send_line(first, "GROUP_SEND 1 blocked")
        assert receive_line(first) == "ERR login required"

        with open(chat_log.name, encoding="utf-8") as log_file:
            log_contents = log_file.read()
        assert "[GROUP] 100000001 -> group-1: hello-from-first" in log_contents, log_contents
        assert "[PRIVATE] 100000001 -> 100000002: private-hello" in log_contents, log_contents
        assert "[PRIVATE] 100000001 -> 100000002: are-you-there" in log_contents, log_contents
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
