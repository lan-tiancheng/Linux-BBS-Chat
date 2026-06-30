let session = "";
let cursor = 0;

const $ = (id) => document.getElementById(id);
const enc = new TextEncoder();

function log(line, kind = "line") {
  const div = document.createElement("div");
  div.textContent = `[${new Date().toLocaleTimeString()}] ${line}`;
  if (kind === "error") div.style.color = "#ffb4ab";
  $("consoleLog").appendChild(div);
  $("consoleLog").scrollTop = $("consoleLog").scrollHeight;
}

async function api(path, payload = {}) {
  const res = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  const data = await res.json();
  if (!res.ok || data.ok === false) throw new Error(data.error || "request failed");
  return data;
}

async function send(command) {
  log(`> ${command}`);
  await api("/api/command", { session, command });
}

function safeText(value) {
  return String(value || "").replace(/[|\r\n]/g, " ").trim();
}

function parseBbsPost(line) {
  const body = line.replace(/^BBS_POST /, "");
  const parts = body.split("|");
  return { id: parts[0], author: parts[1], title: parts[2], content: parts[3], attachment: parts[4], time: parts[5] };
}

function parseBbsReply(line) {
  const body = line.replace(/^BBS_REPLY /, "");
  const parts = body.split("|");
  return { id: parts[0], postId: parts[1], author: parts[2], content: parts[3], attachment: parts[4], time: parts[5] };
}

function addChat(line) {
  const item = document.createElement("div");
  item.className = "item";
  item.textContent = line;
  $("chatFeed").prepend(item);
}

function addDownload(event) {
  const bytes = Uint8Array.from(atob(event.data), (c) => c.charCodeAt(0));
  const blob = new Blob([bytes]);
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.className = "download-link";
  link.href = url;
  link.download = event.filename;
  link.textContent = `下载 ${event.filename}`;
  $("downloads").prepend(link);
}

let collectingPosts = false;
let collectingDetail = false;
let detailLines = [];

function handleLine(line) {
  log(`< ${line}`);
  if (line.startsWith("MSG ") || line.startsWith("PMSG ") || line.startsWith("HMSG ") || line.startsWith("HPMSG ") || line.startsWith("ONLINE ")) {
    addChat(line);
  }
  if (line === "BBS_POSTS_BEGIN") {
    collectingPosts = true;
    $("postList").innerHTML = "";
    return;
  }
  if (line === "BBS_POSTS_END") {
    collectingPosts = false;
    return;
  }
  if (collectingPosts && line.startsWith("BBS_POST ")) {
    const post = parseBbsPost(line);
    const item = document.createElement("div");
    item.className = "item";
    item.innerHTML = `<strong>#${post.id} ${post.title}</strong><br>${post.content}<br><small>${post.author} · ${post.time} · 附件: ${post.attachment}</small>`;
    item.addEventListener("click", () => {
      $("viewPostId").value = post.id;
      send(`BBS_VIEW ${post.id}`).catch((err) => log(err.message, "error"));
    });
    $("postList").appendChild(item);
  }
  if (line === "BBS_POST_BEGIN") {
    collectingDetail = true;
    detailLines = [];
    return;
  }
  if (line === "BBS_POST_END") {
    collectingDetail = false;
    renderDetail();
    return;
  }
  if (collectingDetail && (line.startsWith("BBS_POST ") || line.startsWith("BBS_REPLY "))) {
    detailLines.push(line);
  }
}

function renderDetail() {
  $("postDetail").innerHTML = "";
  for (const line of detailLines) {
    const item = document.createElement("div");
    item.className = "item";
    if (line.startsWith("BBS_POST ")) {
      const post = parseBbsPost(line);
      item.innerHTML = `<strong>帖子 #${post.id}: ${post.title}</strong><br>${post.content}<br><small>${post.author} · ${post.time} · 附件: ${post.attachment}</small>`;
    } else {
      const reply = parseBbsReply(line);
      item.innerHTML = `<strong>回复 #${reply.id}</strong><br>${reply.content}<br><small>${reply.author} · ${reply.time} · 附件: ${reply.attachment}</small>`;
    }
    $("postDetail").appendChild(item);
  }
}

async function poll() {
  if (!session) return;
  try {
    const data = await api("/api/events", { session, cursor });
    cursor = data.cursor;
    for (const event of data.events) {
      if (event.type === "file") {
        log(`< ${event.line}`);
        addDownload(event);
      } else {
        handleLine(event.line);
      }
    }
  } catch (err) {
    setStatus(false, err.message);
  }
}

function setStatus(ok, text) {
  $("statusDot").className = ok ? "ok" : "bad";
  $("statusText").textContent = text;
}

function bindForm(id, handler) {
  $(id).addEventListener("submit", (event) => {
    event.preventDefault();
    handler(new FormData(event.currentTarget)).catch((err) => log(err.message, "error"));
  });
}

async function upload(command, file) {
  const data = new Uint8Array(await file.arrayBuffer());
  let binary = "";
  for (let i = 0; i < data.length; i += 8192) {
    binary += String.fromCharCode(...data.slice(i, i + 8192));
  }
  log(`> ${command} <${data.length} bytes>`);
  await api("/api/upload", {
    session,
    command,
    data: btoa(binary),
  });
}

document.querySelectorAll(".nav").forEach((button) => {
  button.addEventListener("click", () => {
    document.querySelectorAll(".nav").forEach((b) => b.classList.remove("active"));
    document.querySelectorAll(".panel").forEach((p) => p.classList.remove("active"));
    button.classList.add("active");
    $(button.dataset.tab).classList.add("active");
    $("pageTitle").textContent = button.textContent;
  });
});

$("refreshBtn").addEventListener("click", poll);
$("whoBtn").addEventListener("click", () => send("WHO"));
$("historyBtn").addEventListener("click", () => send("HISTORY"));
$("listPostsBtn").addEventListener("click", () => send("BBS_LIST"));
$("viewPostBtn").addEventListener("click", () => send(`BBS_VIEW ${safeText($("viewPostId").value)}`));

bindForm("registerForm", (f) => send(`REGISTER ${safeText(f.get("username"))} ${safeText(f.get("password"))}`));
bindForm("loginForm", (f) => send(`LOGIN ${safeText(f.get("username"))} ${safeText(f.get("password"))}`));
bindForm("groupForm", (f) => send(`GROUP ${safeText(f.get("message"))}`));
bindForm("privateForm", (f) => send(`PRIVATE ${safeText(f.get("target"))} ${safeText(f.get("message"))}`));
bindForm("postForm", (f) => send(`BBS_CREATE ${safeText(f.get("title"))}|${safeText(f.get("content"))}`));
bindForm("replyForm", (f) => send(`BBS_REPLY ${safeText(f.get("postId"))}|${safeText(f.get("content"))}`));
bindForm("downloadForm", (f) => send(`DOWNLOAD ${safeText(f.get("filename"))}`));
bindForm("bbsDownloadForm", (f) => {
  const kind = f.get("kind") === "reply" ? "BBS_DOWNLOAD_REPLY" : "BBS_DOWNLOAD_POST";
  return send(`${kind} ${safeText(f.get("id"))}`);
});
bindForm("backupForm", (f) => send(`BACKUP ${safeText(f.get("label")) || "web"}`));
bindForm("chatUploadForm", async (f) => {
  const file = f.get("file");
  await upload(`UPLOAD ${safeText(f.get("target"))} ${safeText(file.name)} ${file.size}`, file);
});
bindForm("bbsUploadForm", async (f) => {
  const file = f.get("file");
  const command = f.get("kind") === "reply" ? "BBS_UPLOAD_REPLY" : "BBS_UPLOAD_POST";
  await upload(`${command} ${safeText(f.get("id"))} ${safeText(file.name)} ${file.size}`, file);
});

(async function init() {
  try {
    const data = await api("/api/session");
    session = data.session;
    setStatus(true, "已连接后端");
    log(`< ${data.greeting}`);
    setInterval(poll, 600);
  } catch (err) {
    setStatus(false, "连接失败");
    log(err.message, "error");
  }
})();
