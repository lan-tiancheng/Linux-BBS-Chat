#!/usr/bin/env python3
import os
import shutil
import select
import subprocess
import tempfile

from protocol_test_utils import (
    assert_group_invited,
    choose_test_port,
    connect_client,
    create_group,
    make_friends,
    quit_client,
    receive_line,
    register_and_login,
    send_line,
)


PORT = choose_test_port()
OWNER = "100000002"


def assert_no_push(conn, label):
    readable, _, _ = select.select([conn], [], [], 0.25)
    if readable:
        line = receive_line(conn)
        raise AssertionError(f"{label} unexpectedly received {line!r}")


def read_group_history(conn, group_id):
    send_line(conn, f"HISTORY_GROUP {group_id}")
    assert receive_line(conn) == "GROUP_HISTORY_BEGIN"
    history = []
    while True:
        line = receive_line(conn)
        if line == "GROUP_HISTORY_END":
            return history
        history.append(line)


def login_existing(conn, nickname, password, account):
    send_line(conn, f"LOGIN {nickname} {password}")
    assert receive_line(conn) == f"OK logged in {account}|{nickname}"


def main():
    tempdir = tempfile.mkdtemp()
    env = os.environ.copy()
    env["BBS_USERS_FILE"] = os.path.join(tempdir, "users.db")
    env["BBS_CHAT_LOG"] = os.path.join(tempdir, "chat.log")
    env["BBS_UPLOAD_DIR"] = os.path.join(tempdir, "uploads")
    env["BBS_FRIENDS_FILE"] = os.path.join(tempdir, "friends.db")
    env["BBS_PRIVATE_REQUESTS_FILE"] = os.path.join(tempdir, "private_requests.db")
    env["BBS_GROUPS_FILE"] = os.path.join(tempdir, "groups.db")
    env["BBS_GROUP_MEMBERS_FILE"] = os.path.join(tempdir, "group_members.db")
    env["BBS_NOTIFICATIONS_FILE"] = os.path.join(tempdir, "notifications.db")
    os.makedirs(env["BBS_UPLOAD_DIR"], exist_ok=True)

    server = subprocess.Popen(
        ["./bin/server", str(PORT)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
    )

    alpha = beta = carol = eve = None
    try:
        alpha = connect_client(PORT)
        beta = connect_client(PORT)
        carol = connect_client(PORT)
        eve = connect_client(PORT)

        register_and_login(alpha, "100000001", "Alpha123", "alpha")
        register_and_login(beta, "100000002", "Beta123", "beta")
        register_and_login(carol, "100000003", "Carol123", "carol")
        register_and_login(eve, "100000004", "Evepass1", "eve")

        make_friends(beta, alpha, "beta", "alpha", "b-to-a")
        make_friends(beta, carol, "beta", "carol", "b-to-c")

        group_id = create_group(beta, "secure", "alpha,carol")
        assert_group_invited(alpha, group_id, OWNER, 'secure')
        assert_group_invited(carol, group_id, OWNER, 'secure')

        send_line(eve, f"GROUP_SEND {group_id} hello")
        assert receive_line(eve) == "ERR group not found"
        for label, conn in (("alpha", alpha), ("beta", beta), ("carol", carol)):
            assert_no_push(conn, label)

        send_line(eve, f"HISTORY_GROUP {group_id}")
        assert receive_line(eve) == "ERR group not found"

        carol.close()
        carol = None

        send_line(beta, f"GROUP_SEND {group_id} after-disconnect")
        expected = f"GMSG {group_id} {OWNER} after-disconnect"
        events = {receive_line(alpha), receive_line(beta)}
        assert events == {expected}, events

        carol = connect_client(PORT)
        login_existing(carol, "carol", "Carol123", "100000003")
        history = read_group_history(carol, group_id)
        assert any(
            line.startswith("HGMSG ")
            and line.split("|")[1:] == [group_id, OWNER, "after-disconnect"]
            for line in history
        ), history

        for conn in (alpha, beta, carol, eve):
            quit_client(conn)

        print("group message permission test passed")
        return 0
    finally:
        for conn in (alpha, beta, carol, eve):
            if conn is not None:
                try:
                    conn.close()
                except OSError:
                    pass
        server.terminate()
        try:
            server.wait(timeout=2)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        shutil.rmtree(tempdir)


if __name__ == "__main__":
    raise SystemExit(main())