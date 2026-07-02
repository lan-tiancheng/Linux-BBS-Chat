import socket
import time


HOST = "127.0.0.1"


def choose_test_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind((HOST, 0))
        return probe.getsockname()[1]


def receive_line(conn):
    data = bytearray()
    while True:
        chunk = conn.recv(1)
        if not chunk:
            raise RuntimeError("connection closed before a complete line")
        if chunk == b"\n":
            return data.decode("utf-8").rstrip("\r")
        data.extend(chunk)


def send_line(conn, text):
    conn.sendall((text + "\n").encode("utf-8"))


def connect_client(port):
    deadline = time.time() + 3
    while True:
        try:
            conn = socket.create_connection((HOST, port), timeout=1)
            conn.settimeout(2)
            greeting = receive_line(conn)
            assert greeting.startswith("OK connected"), greeting
            return conn
        except OSError:
            if time.time() >= deadline:
                raise
            time.sleep(0.05)


def register_and_login(conn, account, password, nickname):
    send_line(conn, f"REGISTER {account} {password} {nickname}")
    assert receive_line(conn) == "OK registered"
    send_line(conn, f"LOGIN {nickname} {password}")
    assert receive_line(conn) == f"OK logged in {account}|{nickname}"


def make_friends(sender, receiver, sender_nick, receiver_nick, message):
    send_line(sender, f"PRIVATE_START {receiver_nick} {message}")
    pushed = receive_line(receiver)
    assert pushed.startswith("PMSG "), pushed
    assert receive_line(sender) == "OK private request sent"

    send_line(receiver, f"PRIVATE_REPLY {sender_nick} reply-{message}")
    pushed_back = receive_line(sender)
    assert pushed_back.startswith("PMSG "), pushed_back
    assert receive_line(receiver) == "OK private message sent"




def collect_groups(conn):
    send_line(conn, 'GROUPS')
    line = receive_line(conn)
    while line.startswith('EVENT '):
        line = receive_line(conn)
    assert line == 'GROUPS_BEGIN'
    items = []
    while True:
        line = receive_line(conn)
        if line == 'GROUPS_END':
            return items
        items.append(line)


def assert_group_visible(conn, group_id, owner, name):
    groups = collect_groups(conn)
    expected = f"GROUP_ITEM {group_id}|{owner}|{name}"
    assert expected in groups, f"expected {expected}, got {groups}"


def assert_group_not_visible(conn, group_id):
    groups = collect_groups(conn)
    assert not any(line.startswith(f"GROUP_ITEM {group_id}|") for line in groups), groups


def create_group(conn, name, members):
    send_line(conn, f"GROUP_CREATE {name} {members}")
    created = receive_line(conn)
    assert created.startswith("OK group "), created
    parts = created.split()
    assert len(parts) == 4 and parts[:2] == ["OK", "group"] and parts[3] == "created", created
    return parts[2]


def quit_client(conn):
    send_line(conn, "QUIT")
    assert receive_line(conn) == "OK bye"

def assert_group_invited(conn, group_id, owner, name):
    expected = 'EVENT GROUP_INVITED ' + str(group_id) + '|' + owner + '|' + name
    assert receive_line(conn) == expected
