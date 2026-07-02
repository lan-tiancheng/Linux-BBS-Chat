#!/usr/bin/env python3
import os
import shutil
import subprocess
import tempfile
import threading

from protocol_test_utils import (
    choose_test_port,
    collect_groups,
    connect_client,
    make_friends,
    quit_client,
    receive_line,
    register_and_login,
    send_line,
)


PORT = choose_test_port()
OWNER = "100000001"

USERS = {
    "beta": ("100000001", "Beta123"),
    "alpha": ("100000002", "Alpha123"),
    "carol": ("100000003", "Carol123"),
    "david": ("100000004", "David123"),
    "eve": ("100000005", "Evepass1"),
    "frank": ("100000006", "Frank123"),
}

GROUPS = [
    ("team0", "alpha,carol", {"alpha", "carol"}),
    ("team1", "alpha,david", {"alpha", "david"}),
    ("team2", "carol,eve", {"carol", "eve"}),
    ("team3", "david,frank", {"david", "frank"}),
    ("team4", "alpha,eve,frank", {"alpha", "eve", "frank"}),
]


def start_server(tempdir):
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

    return subprocess.Popen(
        ["./bin/server", str(PORT)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
    )


def parse_group_items(lines):
    groups = {}
    for line in lines:
        assert line.startswith("GROUP_ITEM "), line
        group_id, owner, name = line[len("GROUP_ITEM ") :].split("|", 2)
        groups[name] = (group_id, owner)
    return groups


def create_groups_concurrently(beta):
    barrier = threading.Barrier(len(GROUPS))
    failures = []
    failure_lock = threading.Lock()

    def worker(name, members):
        try:
            barrier.wait(timeout=3)
            send_line(beta, f"GROUP_CREATE {name} {members}")
        except Exception as exc:
            with failure_lock:
                failures.append((name, repr(exc)))

    threads = [
        threading.Thread(target=worker, args=(name, members))
        for name, members, _ in GROUPS
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert not failures, failures

    group_ids = []
    for _ in GROUPS:
        line = receive_line(beta)
        assert line.startswith("OK group "), line
        parts = line.split()
        assert len(parts) == 4 and parts[:2] == ["OK", "group"]
        assert parts[3] == "created", line
        group_ids.append(parts[2])
    return group_ids


def main():
    tempdir = tempfile.mkdtemp()
    server = start_server(tempdir)
    clients = {}
    try:
        for nickname, (account, password) in USERS.items():
            conn = connect_client(PORT)
            register_and_login(conn, account, password, nickname)
            clients[nickname] = conn

        beta = clients["beta"]
        for friend in ("alpha", "carol", "david", "eve", "frank"):
            make_friends(beta, clients[friend], "beta", friend, f"beta-to-{friend}")

        group_ids = create_groups_concurrently(beta)
        assert len(set(group_ids)) == len(GROUPS), group_ids

        beta_groups = parse_group_items(collect_groups(beta))
        expected_names = {name for name, _, _ in GROUPS}
        assert set(beta_groups) == expected_names, beta_groups
        for name in expected_names:
            group_id, owner = beta_groups[name]
            assert owner == OWNER, beta_groups
            assert group_id in group_ids, (name, group_id, group_ids)

        visible_by_user = {
            nickname: parse_group_items(collect_groups(conn))
            for nickname, conn in clients.items()
            if nickname != "beta"
        }

        for name, _, members in GROUPS:
            group_id, owner = beta_groups[name]
            expected_item = (group_id, owner)
            for nickname, groups in visible_by_user.items():
                if nickname in members:
                    assert groups.get(name) == expected_item, (nickname, name, groups)
                else:
                    assert name not in groups, (nickname, name, groups)

        for conn in clients.values():
            quit_client(conn)

        print("group concurrency test passed")
        return 0
    finally:
        for conn in clients.values():
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