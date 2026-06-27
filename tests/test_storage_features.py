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


def assert_file_contains(path, expected):
    with open(path, encoding="utf-8") as handle:
        contents = handle.read()
    assert expected in contents, f"{expected!r} not found in {path}: {contents!r}"


def main():
    port = choose_test_port()
    root = tempfile.mkdtemp(prefix="bbs-storage-")
    data_dir = os.path.join(root, "data")
    logs_dir = os.path.join(root, "logs")
    upload_dir = os.path.join(root, "uploads", "chat")
    download_dir = os.path.join(root, "downloads")
    backup_dir = os.path.join(root, "backup")

    environment = os.environ.copy()
    environment["BBS_DATA_DIR"] = data_dir
    environment["BBS_LOG_DIR"] = logs_dir
    environment["BBS_UPLOAD_DIR"] = upload_dir
    environment["BBS_DOWNLOAD_DIR"] = download_dir
    environment["BBS_BACKUP_DIR"] = backup_dir

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

        expected_dirs = [data_dir, logs_dir, upload_dir, download_dir, backup_dir]
        for directory in expected_dirs:
            assert os.path.isdir(directory), directory

        users_file = os.path.join(data_dir, "users.db")
        posts_file = os.path.join(data_dir, "posts.db")
        replies_file = os.path.join(data_dir, "replies.db")
        files_file = os.path.join(data_dir, "files.db")
        chat_log = os.path.join(logs_dir, "chat.log")
        for path in [users_file, posts_file, replies_file, files_file, chat_log]:
            assert os.path.isfile(path), path

        send_line(alice, "REGISTER alice storagepass")
        assert receive_line(alice) == "OK registered"
        send_line(alice, "LOGIN alice storagepass")
        assert receive_line(alice) == "OK logged in alice"
        send_line(bob, "REGISTER bob storagepass")
        assert receive_line(bob) == "OK registered"
        send_line(bob, "LOGIN bob storagepass")
        assert receive_line(bob) == "OK logged in bob"

        assert_file_contains(users_file, "alice:")
        assert_file_contains(users_file, "bob:")

        send_line(alice, "POST first_title first post content")
        assert receive_line(alice) == "OK post 1 created"
        send_line(bob, "REPLY 1 reply content from bob")
        assert receive_line(bob) == "OK reply 1 created"

        send_line(alice, "LISTPOST")
        assert receive_line(alice) == "OK posts"
        assert receive_line(alice) == "1 | alice | first_title"
        assert receive_line(alice) == "OK total 1"

        send_line(alice, "VIEWPOST 1")
        assert receive_line(alice) == "POST 1 alice first_title"
        assert receive_line(alice) == "first post content"
        assert receive_line(alice) == "1 | bob | reply content from bob"
        assert receive_line(alice) == "OK replies 1"

        assert_file_contains(posts_file, "1|alice|first_title|first%20post%20content|")
        assert_file_contains(replies_file, "1|1|bob|reply%20content%20from%20bob|")

        payload = b"storage file payload\n"
        send_line(alice, f"UPLOAD bob sample.txt {len(payload)}")
        alice.sendall(payload)
        assert receive_line(bob) == f"FILE_READY alice sample.txt {len(payload)}"
        assert receive_line(alice) == "OK uploaded sample.txt for bob"

        stored_upload = os.path.join(upload_dir, "bob__sample.txt")
        assert os.path.isfile(stored_upload), stored_upload
        with open(stored_upload, "rb") as handle:
            assert handle.read() == payload
        assert_file_contains(files_file, "|bob|alice|bob|sample.txt|")
        assert_file_contains(files_file, "%2Fuploads%2Fchat%2Fbob__sample.txt|")

        send_line(bob, "DOWNLOAD sample.txt")
        assert receive_line(bob) == f"FILE sample.txt {len(payload)}"
        assert receive_bytes(bob, len(payload)) == payload

        send_line(alice, "BACKUP storage_test")
        backup_response = receive_line(alice)
        assert backup_response.startswith("OK backup created "), backup_response
        snapshot_dir = backup_response.removeprefix("OK backup created ")
        assert os.path.isdir(snapshot_dir), snapshot_dir
        for relative_path in [
            "data/users.db",
            "data/posts.db",
            "data/replies.db",
            "data/files.db",
            "logs/chat.log",
            "uploads/bob__sample.txt",
        ]:
            assert os.path.exists(os.path.join(snapshot_dir, relative_path)), relative_path

        send_line(alice, "QUIT")
        assert receive_line(alice) == "OK bye"
        send_line(bob, "QUIT")
        assert receive_line(bob) == "OK bye"
        print("storage feature test passed")
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
        if server.returncode not in (-15, 0):
            output = server.stdout.read() if server.stdout else ""
            if output:
                print(output)
        shutil.rmtree(root)


if __name__ == "__main__":
    raise SystemExit(main())
