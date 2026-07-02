#!/usr/bin/env python3
import os
import shutil
import subprocess
import tempfile

from protocol_test_utils import (
    choose_test_port,
    connect_client,
    quit_client,
    receive_line,
    register_and_login,
    send_line,
)


PORT = choose_test_port()


def start_server(tempdir):
    env = os.environ.copy()
    env["BBS_DATA_DIR"] = os.path.join(tempdir, "data")
    env["BBS_LOG_DIR"] = os.path.join(tempdir, "logs")
    env["BBS_UPLOAD_DIR"] = os.path.join(tempdir, "uploads", "chat")
    env["BBS_DOWNLOAD_DIR"] = os.path.join(tempdir, "downloads")
    env["BBS_BACKUP_DIR"] = os.path.join(tempdir, "backup")

    return subprocess.Popen(
        ["./bin/server", str(PORT)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
    )


def parse_created_id(line, kind):
    assert line.startswith(f"OK {kind} "), line
    parts = line.split()
    assert len(parts) == 4 and parts[0] == "OK" and parts[1] == kind, line
    assert parts[3] == "created", line
    return parts[2]


def collect_bbs_list(conn):
    send_line(conn, "BBS_LIST")
    assert receive_line(conn) == "BBS_POSTS_BEGIN"
    posts = []
    while True:
        line = receive_line(conn)
        if line == "BBS_POSTS_END":
            return posts
        posts.append(line)


def split_bbs_post(line):
    assert line.startswith("BBS_POST "), line
    fields = line[len("BBS_POST ") :].split("|")
    assert len(fields) == 6, f"BBS_POST field structure broken: {fields!r}"
    return fields


def split_bbs_reply(line):
    assert line.startswith("BBS_REPLY "), line
    fields = line[len("BBS_REPLY ") :].split("|")
    assert len(fields) == 6, f"BBS_REPLY field structure broken: {fields!r}"
    return fields


def assert_contains_fragments(value, fragments, label):
    for fragment in fragments:
        assert fragment in value, f"{label} missing {fragment!r}: {value!r}"


def read_bbs_view(conn, post_id):
    send_line(conn, f"BBS_VIEW {post_id}")
    assert receive_line(conn) == "BBS_POST_BEGIN"
    post_line = receive_line(conn)
    assert receive_line(conn) == "BBS_REPLIES_BEGIN"
    replies = []
    while True:
        line = receive_line(conn)
        if line == "BBS_REPLIES_END":
            break
        replies.append(line)
    assert receive_line(conn) == "BBS_POST_END"
    return post_line, replies


def assert_error(line):
    assert line.startswith("ERR ") or line == "BBS_NOT_FOUND", line


def main():
    tempdir = tempfile.mkdtemp(prefix="bbs-content-edges-")
    server = start_server(tempdir)
    alpha = beta = gamma = None
    try:
        alpha = connect_client(PORT)
        beta = connect_client(PORT)
        gamma = connect_client(PORT)

        register_and_login(alpha, "100000001", "Alpha123", "alpha")
        register_and_login(beta, "100000002", "Beta123", "beta")
        register_and_login(gamma, '100000003', 'Gamma123', 'gamma')
        quit_client(gamma)
        gamma = None

        send_line(alpha, "BBS_CREATE |valid content")
        assert receive_line(alpha) == "ERR invalid BBS title"
        send_line(alpha, "BBS_CREATE valid title|")
        assert receive_line(alpha) == "ERR invalid BBS content"
        send_line(alpha, "BBS_CREATE title with | pipe|valid content")
        assert receive_line(alpha) == "ERR invalid BBS content"
        send_line(alpha, "BBS_CREATE valid title|content with | pipe")
        assert receive_line(alpha) == "ERR invalid BBS content"

        title = (
            "Title 中文 https://ex.com/a?x=1 100% "
            "many   spaces"
        )
        body = (
            "Body with spaces 中文 https://example.com/post?q=a%20b&flag=1 "
            "50% pipe marker many    spaces"
        )
        send_line(alpha, f"BBS_CREATE {title}|{body}")
        post_id = parse_created_id(receive_line(alpha), "post")
        assert receive_line(beta).startswith(f'EVENT BBS_POST_CREATED {post_id}|alpha|')

        posts = collect_bbs_list(alpha)
        matching_posts = [line for line in posts if line.startswith(f"BBS_POST {post_id}|")]
        assert len(matching_posts) == 1, posts
        post_fields = split_bbs_post(matching_posts[0])
        assert post_fields[0] == post_id, post_fields
        assert post_fields[1] == "alpha", post_fields
        assert_contains_fragments(
            post_fields[2],
            ["Title", "中文", "https://ex.com/a?x=1", "100%", "many   spaces"],
            "BBS list title",
        )
        assert_contains_fragments(
            post_fields[3],
            ["Body with spaces", "中文", "https://example.com/post?q=a%20b&flag=1", "50%", "pipe marker", "many    spaces"],
            "BBS list body",
        )

        post_line, replies = read_bbs_view(beta, post_id)
        viewed_post_fields = split_bbs_post(post_line)
        assert viewed_post_fields[:4] == post_fields[:4], viewed_post_fields
        assert replies == [], replies

        send_line(beta, f"BBS_REPLY {post_id}|")
        assert receive_line(beta) == "ERR invalid BBS reply content"
        send_line(beta, f"BBS_REPLY {post_id}|reply with | pipe")
        assert receive_line(beta) == "ERR invalid BBS reply content"

        reply = (
            "Reply with spaces 中文 https://example.com/reply?a=1%202 "
            "75% pipe marker many     spaces"
        )
        send_line(beta, f"BBS_REPLY {post_id}|{reply}")
        reply_id = parse_created_id(receive_line(beta), "reply")
        assert receive_line(alpha) == f'EVENT BBS_REPLY_CREATED {post_id}|{reply_id}|beta'

        post_line, replies = read_bbs_view(alpha, post_id)
        split_bbs_post(post_line)
        assert len(replies) == 1, replies
        reply_fields = split_bbs_reply(replies[0])
        assert reply_fields[0] == reply_id, reply_fields
        assert reply_fields[1] == post_id, reply_fields
        assert reply_fields[2] == "beta", reply_fields
        assert_contains_fragments(
            reply_fields[3],
            ["Reply with spaces", "中文", "https://example.com/reply?a=1%202", "75%", "pipe marker", "many     spaces"],
            "BBS reply content",
        )

        gamma = connect_client(PORT)
        send_line(gamma, 'LOGIN gamma Gamma123')
        assert receive_line(gamma) == 'OK logged in 100000003|gamma'
        offline_post, offline_replies = read_bbs_view(gamma, post_id)
        split_bbs_post(offline_post)
        assert any(line.startswith(f'BBS_REPLY {reply_id}|{post_id}|beta|') for line in offline_replies), offline_replies
        send_line(alpha, "BBS_VIEW 999999")
        assert_error(receive_line(alpha))
        send_line(alpha, "BBS_REPLY 999999|hello")
        assert_error(receive_line(alpha))

        send_line(alpha, "BBS_VIEW abc")
        assert_error(receive_line(alpha))
        send_line(alpha, "BBS_REPLY abc|hello")
        assert_error(receive_line(alpha))

        quit_client(alpha)
        alpha = None
        quit_client(beta)
        beta = None
        quit_client(gamma)
        gamma = None

        print("bbs content edge test passed")
        return 0
    finally:
        for conn in (alpha, beta, gamma):
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