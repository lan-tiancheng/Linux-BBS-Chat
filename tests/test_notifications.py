#!/usr/bin/env python3
import os
import shutil
import subprocess
import tempfile

from protocol_test_utils import (
    choose_test_port,
    connect_client,
    make_friends,
    receive_line,
    register_and_login,
    send_line,
)


PORT = choose_test_port()


def collect_notifications(conn):
    send_line(conn, "NOTIFICATIONS")
    assert receive_line(conn) == "NOTIFICATIONS_BEGIN"
    rows = []
    while True:
        line = receive_line(conn)
        if line == "NOTIFICATIONS_END":
            return rows
        assert line.startswith("NOTIFICATION "), line
        fields = line.replace("NOTIFICATION ", "", 1).split("|")
        assert len(fields) == 6, line
        rows.append(
            {
                "id": fields[0],
                "type": fields[1],
                "target": fields[2],
                "message": fields[3],
                "created_at": fields[4],
                "read": fields[5],
            }
        )


def find_notification(rows, notification_type, target=None):
    for row in rows:
        if row["type"] == notification_type and (
            target is None or row["target"] == str(target)
        ):
            return row
    raise AssertionError(f"missing {notification_type} {target}: {rows}")


def login_existing(conn, nickname, password, account):
    send_line(conn, f"LOGIN {nickname} {password}")
    assert receive_line(conn) == f"OK logged in {account}|{nickname}"


def main():
    tempdir = tempfile.mkdtemp()
    env = os.environ.copy()
    env["BBS_DATA_DIR"] = os.path.join(tempdir, "data")
    env["BBS_LOG_DIR"] = os.path.join(tempdir, "logs")
    env["BBS_UPLOAD_DIR"] = os.path.join(tempdir, "uploads")
    env["BBS_DOWNLOAD_DIR"] = os.path.join(tempdir, "downloads")
    env["BBS_BACKUP_DIR"] = os.path.join(tempdir, "backup")

    server = subprocess.Popen(
        ["./bin/server", str(PORT)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
    )

    alpha = beta = carol = None
    try:
        alpha = connect_client(PORT)
        beta = connect_client(PORT)
        carol = connect_client(PORT)

        register_and_login(alpha, "100000001", "Alpha123", "alpha")
        register_and_login(beta, "100000002", "Beta123", "beta")
        register_and_login(carol, "100000003", "Carol123", "carol")

        make_friends(alpha, beta, "alpha", "beta", "a-to-b")
        make_friends(alpha, carol, "alpha", "carol", "a-to-c")

        send_line(alpha, "GROUP_CREATE online beta")
        assert receive_line(alpha) == "OK group 1 created"
        assert receive_line(beta) == "EVENT GROUP_INVITED 1|100000001|online"
        beta_rows = collect_notifications(beta)
        online_invite = find_notification(beta_rows, "GROUP_INVITED", 1)
        assert online_invite["read"] == "0", beta_rows

        send_line(beta, f"MARK_READ {online_invite['id']}")
        assert receive_line(beta) == "OK notification read"
        beta_rows = collect_notifications(beta)
        assert find_notification(beta_rows, "GROUP_INVITED", 1)["read"] == "1"

        send_line(carol, "LOGOUT")
        assert receive_line(carol) == "OK logged out"
        send_line(alpha, "GROUP_CREATE offline carol")
        assert receive_line(alpha) == "OK group 2 created"
        login_existing(carol, "carol", "Carol123", "100000003")
        carol_rows = collect_notifications(carol)
        offline_invite = find_notification(carol_rows, "GROUP_INVITED", 2)
        assert offline_invite["read"] == "0", carol_rows

        send_line(alpha, "BBS_CREATE hello|body")
        assert receive_line(alpha) == "OK post 1 created"
        assert receive_line(beta).startswith("EVENT BBS_POST_CREATED 1|alpha|hello")
        assert receive_line(carol).startswith("EVENT BBS_POST_CREATED 1|alpha|hello")

        send_line(alpha, "LOGOUT")
        assert receive_line(alpha) == "OK logged out"
        send_line(beta, "BBS_REPLY 1|reply-body")
        assert receive_line(carol).startswith("EVENT BBS_REPLY_CREATED 1|1|beta")
        assert receive_line(beta) == "OK reply 1 created"
        login_existing(alpha, "alpha", "Alpha123", "100000001")
        alpha_rows = collect_notifications(alpha)
        reply_notice = find_notification(alpha_rows, "BBS_REPLY_CREATED", 1)
        assert reply_notice["read"] == "0", alpha_rows

        send_line(carol, "MARK_READ_ALL")
        assert receive_line(carol) == "OK notifications read"
        carol_rows = collect_notifications(carol)
        assert carol_rows, carol_rows
        assert all(row["read"] == "1" for row in carol_rows), carol_rows

    finally:
        for conn in (alpha, beta, carol):
            if conn is not None:
                try:
                    send_line(conn, "QUIT")
                    receive_line(conn)
                except Exception:
                    pass
                conn.close()
        server.terminate()
        try:
            server.wait(timeout=2)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        shutil.rmtree(tempdir)


if __name__ == "__main__":
    main()
