#!/usr/bin/env python3
import os
import shutil
import subprocess
import tempfile

from protocol_test_utils import (
    assert_group_not_visible,
    assert_group_visible,
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


def assert_can_send_to_group(conn, group_id, sender, recipients, message):
    send_line(conn, f"GROUP_SEND {group_id} {message}")
    expected = f"GMSG {group_id} {sender} {message}"
    events = {receive_line(recipient) for recipient in recipients}
    assert events == {expected}, events


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
    os.makedirs(env["BBS_UPLOAD_DIR"], exist_ok=True)

    server = subprocess.Popen(
        ["./bin/server", str(PORT)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
    )

    alpha = beta = carol = david = eve = None
    try:
        alpha = connect_client(PORT)
        beta = connect_client(PORT)
        carol = connect_client(PORT)
        david = connect_client(PORT)
        eve = connect_client(PORT)

        register_and_login(alpha, "100000001", "Alpha123", "alpha")
        register_and_login(beta, "100000002", "Beta123", "beta")
        register_and_login(carol, "100000003", "Carol123", "carol")
        register_and_login(david, "100000004", "David123", "david")
        register_and_login(eve, "100000005", "Evepass1", "eve")

        make_friends(beta, alpha, "beta", "alpha", "b-to-a")
        make_friends(beta, carol, "beta", "carol", "b-to-c")
        make_friends(beta, david, "beta", "david", "b-to-d")

        team_id = create_group(beta, "team", "alpha,carol,david")
        for conn in (alpha, beta, carol, david):
            assert_group_visible(conn, team_id, OWNER, "team")
        assert_group_not_visible(eve, team_id)

        mixed_id = create_group(beta, "mixed", "alpha,100000003")
        assert_group_visible(alpha, mixed_id, OWNER, "mixed")
        assert_group_visible(beta, mixed_id, OWNER, "mixed")
        assert_group_visible(carol, mixed_id, OWNER, "mixed")
        assert_group_not_visible(david, mixed_id)
        assert_group_not_visible(eve, mixed_id)

        spaced_id = create_group(beta, "spaced", "alpha, carol")
        assert_group_visible(alpha, spaced_id, OWNER, "spaced")
        assert_group_visible(beta, spaced_id, OWNER, "spaced")
        assert_group_visible(carol, spaced_id, OWNER, "spaced")
        assert_group_not_visible(david, spaced_id)
        assert_group_not_visible(eve, spaced_id)

        partial_id = create_group(beta, "partial", "alpha,unknown,carol")
        assert_group_visible(alpha, partial_id, OWNER, "partial")
        assert_group_visible(beta, partial_id, OWNER, "partial")
        assert_group_visible(carol, partial_id, OWNER, "partial")
        assert_group_not_visible(david, partial_id)
        assert_group_not_visible(eve, partial_id)

        secure_id = create_group(beta, "secure", "alpha,eve")
        assert_group_visible(alpha, secure_id, OWNER, "secure")
        assert_group_visible(beta, secure_id, OWNER, "secure")
        assert_group_not_visible(eve, secure_id)

        send_line(eve, f"GROUP_SEND {secure_id} hello")
        assert receive_line(eve) == "ERR group not found"

        assert_can_send_to_group(
            carol,
            team_id,
            "100000003",
            (alpha, beta, carol, david),
            "hello-from-carol",
        )

        for conn in (alpha, beta, carol, david, eve):
            quit_client(conn)

        print("group create edge test passed")
        return 0
    finally:
        for conn in (alpha, beta, carol, david, eve):
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
