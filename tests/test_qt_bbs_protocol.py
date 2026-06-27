#!/usr/bin/env python3
import os
import shutil
import socket
import subprocess
import tempfile
import time


HOST = "127.0.0.1"


def choose_test_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind((HOST, 0))
        return probe.getsockname()[1]


def receive_line(connection):
    data = bytearray()
    while True:
        chunk = connection.recv(1)
        if not chunk:
            raise RuntimeError("connection closed before a complete line")
        if chunk == b"\n":
            return data.decode("utf-8").rstrip("\r")
        data.extend(chunk)


def receive_bytes(connection, size):
    data = bytearray()
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise RuntimeError("connection closed during file transfer")
        data.extend(chunk)
    return bytes(data)


def send_line(connection, text):
    connection.sendall((text + "\n").encode("utf-8"))


def connect_client(port):
    deadline = time.time() + 3
    while True:
        try:
            connection = socket.create_connection((HOST, port), timeout=1)
            connection.settimeout(2)
            greeting = receive_line(connection)
            assert greeting.startswith("OK connected"), greeting
            return connection
        except OSError:
            if time.time() >= deadline:
                raise
            time.sleep(0.05)


def main():
    port = choose_test_port()
    root = tempfile.mkdtemp(prefix="bbs-qt-")
    environment = os.environ.copy()
    environment["BBS_DATA_DIR"] = os.path.join(root, "data")
    environment["BBS_LOG_DIR"] = os.path.join(root, "logs")
    environment["BBS_UPLOAD_DIR"] = os.path.join(root, "uploads", "chat")
    environment["BBS_DOWNLOAD_DIR"] = os.path.join(root, "downloads")
    environment["BBS_BACKUP_DIR"] = os.path.join(root, "backup")

    server = subprocess.Popen(
        ["./bin/server", str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=environment,
    )
    alice = None
    bob = None
    try:
        alice = connect_client(port)
        bob = connect_client(port)

        send_line(alice, "REGISTER alice pass-a")
        assert receive_line(alice) == "OK registered"
        send_line(alice, "LOGIN alice pass-a")
        assert receive_line(alice) == "OK logged in alice"
        send_line(bob, "REGISTER bob pass-b")
        assert receive_line(bob) == "OK registered"
        send_line(bob, "LOGIN bob pass-b")
        assert receive_line(bob) == "OK logged in bob"

        send_line(alice, "BBS_CREATE qt title|qt content")
        assert receive_line(alice) == "OK post 1 created"
        post_payload = b"post attachment bytes\n"
        send_line(alice, f"BBS_UPLOAD_POST 1 post.txt {len(post_payload)}")
        alice.sendall(post_payload)
        assert receive_line(alice) == "OK uploaded post.txt"

        send_line(bob, "BBS_REPLY 1|reply from bob")
        assert receive_line(bob) == "OK reply 1 created"
        reply_payload = b"reply attachment bytes\n"
        send_line(bob, f"BBS_UPLOAD_REPLY 1 reply.txt {len(reply_payload)}")
        bob.sendall(reply_payload)
        assert receive_line(bob) == "OK uploaded reply.txt"

        send_line(bob, "BBS_UPLOAD_POST 1 denied.txt 6")
        bob.sendall(b"denied")
        assert receive_line(bob) == "ERR only post author can upload attachment"

        send_line(bob, "BBS_LIST")
        assert receive_line(bob) == "BBS_POSTS_BEGIN"
        post_line = receive_line(bob)
        assert post_line.startswith(
            "BBS_POST 1|alice|qt title|qt content|post.txt|"
        ), post_line
        assert receive_line(bob) == "BBS_POSTS_END"

        send_line(bob, "BBS_VIEW 1")
        assert receive_line(bob) == "BBS_POST_BEGIN"
        post_line = receive_line(bob)
        assert post_line.startswith(
            "BBS_POST 1|alice|qt title|qt content|post.txt|"
        ), post_line
        assert receive_line(bob) == "BBS_REPLIES_BEGIN"
        reply_line = receive_line(bob)
        assert reply_line.startswith(
            "BBS_REPLY 1|1|bob|reply from bob|reply.txt|"
        ), reply_line
        assert receive_line(bob) == "BBS_REPLIES_END"
        assert receive_line(bob) == "BBS_POST_END"

        send_line(bob, "BBS_DOWNLOAD_POST 1")
        assert receive_line(bob) == f"BBS_FILE post.txt {len(post_payload)}"
        assert receive_bytes(bob, len(post_payload)) == post_payload

        send_line(alice, "BBS_DOWNLOAD_REPLY 1")
        assert receive_line(alice) == f"BBS_FILE reply.txt {len(reply_payload)}"
        assert receive_bytes(alice, len(reply_payload)) == reply_payload

        send_line(alice, "GROUP qt-history-check")
        assert receive_line(alice).endswith(" qt-history-check")
        assert receive_line(bob).endswith(" qt-history-check")
        send_line(alice, "HISTORY")
        assert receive_line(alice) == "HISTORY_BEGIN"
        history_line = receive_line(alice)
        assert "qt-history-check" in history_line, history_line
        assert receive_line(alice) == "HISTORY_END"

        files_file = os.path.join(environment["BBS_DATA_DIR"], "files.db")
        with open(files_file, encoding="utf-8") as handle:
            file_index = handle.read()
        assert "post%3A1" in file_index, file_index
        assert "reply%3A1" in file_index, file_index

        send_line(alice, "QUIT")
        assert receive_line(alice) == "OK bye"
        send_line(bob, "QUIT")
        assert receive_line(bob) == "OK bye"
        print("qt bbs protocol test passed")
        return 0
    finally:
        if alice is not None:
            alice.close()
        if bob is not None:
            bob.close()
        server.terminate()
        try:
            server.wait(timeout=2)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        shutil.rmtree(root)


if __name__ == "__main__":
    raise SystemExit(main())
