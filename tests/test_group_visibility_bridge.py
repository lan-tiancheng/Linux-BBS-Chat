#!/usr/bin/env python3
import os
import shutil
import subprocess
import tempfile

from protocol_test_utils import (
    assert_group_invited,
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

    a = b = c = None
    try:
        a = connect_client(PORT)
        b = connect_client(PORT)
        c = connect_client(PORT)

        register_and_login(a, "100000001", "Alpha123", "alpha")
        register_and_login(b, "100000002", "Beta123", "beta")
        register_and_login(c, "100000003", "Carol123", "carol")

        make_friends(b, a, "beta", "alpha", "b-to-a")
        make_friends(b, c, "beta", "carol", "b-to-c")

        send_line(a, "FRIENDS")
        assert receive_line(a) == "FRIENDS_BEGIN"
        a_friends = []
        while True:
            line = receive_line(a)
            if line == "FRIENDS_END":
                break
            a_friends.append(line)
        assert "FRIEND 100000002|beta" in a_friends, a_friends
        assert "FRIEND 100000003|carol" not in a_friends, a_friends

        group_id = create_group(b, "bridge", "alpha,carol")
        assert_group_invited(a, group_id, '100000002', 'bridge')
        assert_group_invited(c, group_id, '100000002', 'bridge')

        assert_group_visible(a, group_id, "100000002", "bridge")
        assert_group_visible(b, group_id, "100000002", "bridge")
        assert_group_visible(c, group_id, "100000002", "bridge")

        send_line(c, f"GROUP_SEND {group_id} hello-from-carol")
        events = {receive_line(a), receive_line(b), receive_line(c)}
        expected = f"GMSG {group_id} 100000003 hello-from-carol"
        assert events == {expected}, events

        quit_client(a)
        quit_client(b)
        quit_client(c)

        print("group visibility bridge test passed")
        return 0

    finally:
        for conn in (a, b, c):
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
