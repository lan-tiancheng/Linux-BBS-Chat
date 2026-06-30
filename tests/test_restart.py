#!/usr/bin/env python3
import os
import socket
import subprocess
import tempfile
import time


HOST = "127.0.0.1"


def choose_port():
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


def send_line(connection, text):
    connection.sendall((text + "\n").encode("utf-8"))


def start_server(port, environment):
    server = subprocess.Popen(
        ["./bin/server", str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=environment,
    )
    deadline = time.time() + 3
    while time.time() < deadline:
        if server.poll() is not None:
            output = server.stdout.read() if server.stdout else ""
            raise RuntimeError(f"server exited early:\n{output}")
        try:
            connection = socket.create_connection((HOST, port), timeout=0.5)
            connection.settimeout(2)
            greeting = receive_line(connection)
            assert greeting.startswith("OK connected"), greeting
            return server, connection
        except OSError:
            time.sleep(0.05)
    server.terminate()
    server.wait(timeout=2)
    raise RuntimeError("server did not start in time")


def stop_server(server):
    server.terminate()
    try:
        server.wait(timeout=2)
    except subprocess.TimeoutExpired:
        server.kill()
        server.wait()


def connect_client(port):
    connection = socket.create_connection((HOST, port), timeout=1)
    connection.settimeout(2)
    greeting = receive_line(connection)
    assert greeting.startswith("OK connected"), greeting
    return connection


def main():
    users_file = tempfile.NamedTemporaryFile(delete=False)
    users_file.close()
    chat_log = tempfile.NamedTemporaryFile(delete=False)
    chat_log.close()
    upload_dir = tempfile.TemporaryDirectory()
    environment = os.environ.copy()
    environment["BBS_USERS_FILE"] = users_file.name
    environment["BBS_CHAT_LOG"] = chat_log.name
    environment["BBS_UPLOAD_DIR"] = upload_dir.name
    environment["BBS_FRIENDS_FILE"] = os.path.join(upload_dir.name, "friends.db")
    environment["BBS_PRIVATE_REQUESTS_FILE"] = os.path.join(upload_dir.name, "private_requests.db")
    environment["BBS_GROUPS_FILE"] = os.path.join(upload_dir.name, "groups.db")
    environment["BBS_GROUP_MEMBERS_FILE"] = os.path.join(upload_dir.name, "group_members.db")

    first_server = None
    second_server = None
    client = None
    try:
        first_port = choose_port()
        first_server, client = start_server(first_port, environment)
        send_line(client, "REGISTER 100000001 Persist123 persist")
        assert receive_line(client) == "OK registered"
        send_line(client, "QUIT")
        assert receive_line(client) == "OK bye"
        client.close()
        client = None
        stop_server(first_server)
        first_server = None

        second_port = choose_port()
        second_server, client = start_server(second_port, environment)
        send_line(client, "LOGIN persist Persist123")
        assert receive_line(client) == "OK logged in 100000001|persist"

        # Simulate a terminal or network disappearing without LOGOUT/QUIT.
        client.close()
        client = None

        deadline = time.time() + 3
        while True:
            retry = connect_client(second_port)
            send_line(retry, "LOGIN 100000001 Persist123")
            response = receive_line(retry)
            if response == "OK logged in 100000001|persist":
                client = retry
                break
            retry.close()
            assert response == "ERR user already logged in", response
            if time.time() >= deadline:
                raise AssertionError("disconnected user remained online")
            time.sleep(0.05)

        send_line(client, "QUIT")
        assert receive_line(client) == "OK bye"
        print("restart and disconnect test passed")
        return 0
    finally:
        if client is not None:
            client.close()
        if first_server is not None:
            stop_server(first_server)
        if second_server is not None:
            stop_server(second_server)
        upload_dir.cleanup()
        os.unlink(users_file.name)
        os.unlink(chat_log.name)


if __name__ == "__main__":
    raise SystemExit(main())
