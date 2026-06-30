let session = "";
let cursor = 0;
let currentUser = "";
let currentPostId = 0;
let selectedPostAttachment = null;
let selectedReplyAttachment = null;
let pendingPostAttachment = null;
let pendingReplyAttachment = null;

const $ = (id) => document.getElementById(id);

function clean(value) {
  return String(value || "").replace(/[|\r\n]/g, " ").trim();
}

function setLoginStatus(text) {
  $("loginStatus").textContent = text;
}

function showStatus(text, ok = true) {
  $("appStatus").textContent = text;
  $("statusDot").className = ok ? "ok" : "bad";
}

function log(line, kind = "line") {
  const div = document.createElement("div");
  div.textContent = `[${new Date().toLocaleTimeString()}] ${line}`;
  if (kind === "error") div.style.color = "#b3261e";
  $("consoleLog").appendChild(div);
  $("consoleLog").scrollTop = $("consoleLog").scrollHeight;
}

async function api(path, payload = {}) {
  const response = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  const data = await response.json();
  if (!response.ok || data.ok === false) {
    throw new Error(data.error || "请求失败");
  }
  return data;
}

async function ensureSession() {
  if (session) return;
  const data = await api("/api/session");
  session = data.session;
  setLoginStatus("服务器已连接，可以登录或注册。");
  log(`< ${data.greeting}`);
}

async function send(command) {
  await ensureSession();
  log(`> ${command}`);
  await api("/api/command", { session, command });
}

async function upload(command, file) {
  await ensureSession();
  const bytes = new Uint8Array(await file.arrayBuffer());
  let binary = "";
  for (let i = 0; i < bytes.length; i += 8192) {
    binary += String.fromCharCode(...bytes.slice(i, i + 8192));
  }
  log(`> ${command} <${bytes.length} bytes>`);
  await api("/api/upload", {
    session,
    command,
    data: btoa(binary),
  });
}

function parseBbsPost(line) {
  const parts = line.replace(/^BBS_POST /, "").split("|");
  return {
    id: Number(parts[0]),
    author: parts[1] || "",
    title: parts[2] || "",
    content: parts[3] || "",
    attachment: parts[4] || "none",
    time: parts[5] || "",
  };
}

function parseBbsReply(line) {
  const parts = line.replace(/^BBS_REPLY /, "").split("|");
  return {
    id: Number(parts[0]),
    postId: Number(parts[1]),
    author: parts[2] || "",
    content: parts[3] || "",
    attachment: parts[4] || "none",
    time: parts[5] || "",
  };
}

function activateShell(username) {
  currentUser = username;
  $("loginShell").classList.add("hidden");
  $("appShell").classList.remove("hidden");
  $("sessionText").textContent = `已登录：${username}`;
  showStatus(`已登录：${username}`);
  send("WHO").catch(reportError);
  send("HISTORY").catch(reportError);
  setTimeout(() => send("BBS_LIST").catch(reportError), 180);
}

function reportError(error) {
  const text = error.message || String(error);
  setLoginStatus(text);
  showStatus(text, false);
  log(text, "error");
}

function appendChat(kind, title, message, time = "") {
  const empty = $("chatFeed").querySelector(".empty");
  if (empty) empty.remove();
  const div = document.createElement("div");
  div.className = `bubble ${kind}`;
  div.innerHTML = `<span class="tag">${title}</span>${time ? `<span class="time"> ${time}</span>` : ""}<br>${message}`;
  $("chatFeed").appendChild(div);
  $("chatFeed").scrollTop = $("chatFeed").scrollHeight;
}

function updateOnlineUsers(line) {
  const parts = line.split(" ");
  const users = parts.slice(2);
  $("onlineList").innerHTML = "";
  users.forEach((user) => {
    const div = document.createElement("div");
    div.className = "user-pill";
    div.textContent = user === currentUser ? `${user}  我` : user;
    div.addEventListener("dblclick", () => {
      if (user === currentUser) {
        showStatus("不能私聊自己。", false);
        return;
      }
      $("privateTarget").value = user;
      showStatus(`私聊对象已设为：${user}`);
    });
    $("onlineList").appendChild(div);
  });
}

function renderPostCard(post) {
  const card = document.createElement("button");
  card.className = "post-card";
  card.type = "button";
  card.dataset.id = String(post.id);
  const title = post.title.length > 22 ? `${post.title.slice(0, 22)}...` : post.title;
  const body = post.content.length > 38 ? `${post.content.slice(0, 38)}...` : post.content;
  const time = post.time.length > 16 ? post.time.slice(0, 16) : post.time;
  const attachment = post.attachment && post.attachment !== "none" ? " · 附件" : "";
  card.innerHTML = `<strong>${title}</strong><span>${body}</span><small>#${post.id} · ${post.author} · ${time}${attachment}</small>`;
  card.addEventListener("click", () => viewPost(post.id));
  return card;
}

let collectingPosts = false;
let postRows = [];
let collectingDetail = false;
let detailRows = [];

function renderPosts() {
  $("postList").innerHTML = "";
  if (postRows.length === 0) {
    $("postList").innerHTML = `<div class="empty">暂无帖子。</div>`;
    return;
  }
  postRows.map(parseBbsPost).forEach((post) => $("postList").appendChild(renderPostCard(post)));
}

function renderDetail() {
  $("postDetail").innerHTML = "";
  const postLine = detailRows.find((line) => line.startsWith("BBS_POST "));
  if (!postLine) {
    $("postDetail").innerHTML = `<div class="empty">没有找到帖子详情。</div>`;
    return;
  }
  const post = parseBbsPost(postLine);
  currentPostId = post.id;
  document.querySelectorAll(".post-card").forEach((card) => {
    card.classList.toggle("active", Number(card.dataset.id) === currentPostId);
  });
  const postBlock = document.createElement("div");
  postBlock.className = "bubble";
  const postLink = post.attachment !== "none" ? `<br><a href="#" data-download-post="${post.id}">下载帖子附件：${post.attachment}</a>` : "";
  postBlock.innerHTML = `<b>#${post.id} ${post.title}</b><span class="time"> ${post.author} · ${post.time}</span><br>${post.content}${postLink}`;
  $("postDetail").appendChild(postBlock);
  const replies = detailRows.filter((line) => line.startsWith("BBS_REPLY ")).map(parseBbsReply);
  if (replies.length === 0) {
    const empty = document.createElement("div");
    empty.className = "empty";
    empty.textContent = "暂无回复。";
    $("postDetail").appendChild(empty);
  } else {
    replies.forEach((reply) => {
      const div = document.createElement("div");
      div.className = "bubble history";
      const link = reply.attachment !== "none" ? `<br><a href="#" data-download-reply="${reply.id}">下载回复附件：${reply.attachment}</a>` : "";
      div.innerHTML = `<b>回复 #${reply.id}</b><span class="time"> ${reply.author} · ${reply.time}</span><br>${reply.content}${link}`;
      $("postDetail").appendChild(div);
    });
  }
  $("postDetail").querySelectorAll("[data-download-post]").forEach((link) => {
    link.addEventListener("click", (event) => {
      event.preventDefault();
      send(`BBS_DOWNLOAD_POST ${event.currentTarget.dataset.downloadPost}`).catch(reportError);
    });
  });
  $("postDetail").querySelectorAll("[data-download-reply]").forEach((link) => {
    link.addEventListener("click", (event) => {
      event.preventDefault();
      send(`BBS_DOWNLOAD_REPLY ${event.currentTarget.dataset.downloadReply}`).catch(reportError);
    });
  });
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
  showStatus(`文件已准备下载：${event.filename}`);
}

async function maybeUploadPendingAttachment(line) {
  const postMatch = /^OK post (\d+) created/.exec(line);
  if (postMatch) {
    const file = pendingPostAttachment;
    pendingPostAttachment = null;
    if (file) {
      await upload(`BBS_UPLOAD_POST ${postMatch[1]} ${clean(file.name)} ${file.size}`, file);
    }
    await send("BBS_LIST");
    return;
  }
  const replyMatch = /^OK reply (\d+) created/.exec(line);
  if (replyMatch) {
    const file = pendingReplyAttachment;
    pendingReplyAttachment = null;
    if (file) {
      await upload(`BBS_UPLOAD_REPLY ${replyMatch[1]} ${clean(file.name)} ${file.size}`, file);
    }
    if (currentPostId > 0) await send(`BBS_VIEW ${currentPostId}`);
  }
}

function handleLine(line) {
  log(`< ${line}`);
  if (line.startsWith("OK logged in ")) {
    activateShell(line.replace("OK logged in ", "").trim());
    return;
  }
  if (line === "OK registered") {
    setLoginStatus("注册成功，现在可以登录。");
  }
  if (line.startsWith("ERR ")) {
    showStatus(line, false);
    setLoginStatus(line);
  } else if (line.startsWith("OK ")) {
    showStatus(line);
  }
  if (line.startsWith("ONLINE ")) updateOnlineUsers(line);
  if (line.startsWith("MSG ")) {
    const [, sender, ...message] = line.split(" ");
    appendChat("group", `【群聊】${sender}`, message.join(" "), new Date().toLocaleTimeString());
  }
  if (line.startsWith("PMSG ")) {
    const [, sender, ...message] = line.split(" ");
    appendChat("private", `【私聊】${sender} -> 我`, message.join(" "), new Date().toLocaleTimeString());
  }
  if (line.startsWith("HMSG ")) {
    const data = line.replace(/^HMSG /, "").split("|");
    appendChat("history", `【历史群聊】${data[1]}`, data.slice(2).join("|"), data[0]);
  }
  if (line.startsWith("HPMSG ")) {
    const data = line.replace(/^HPMSG /, "").split("|");
    appendChat("history private", `【历史私聊】${data[1]} -> ${data[2]}`, data.slice(3).join("|"), data[0]);
  }
  if (line === "BBS_POSTS_BEGIN") {
    collectingPosts = true;
    postRows = [];
    return;
  }
  if (line === "BBS_POSTS_END") {
    collectingPosts = false;
    renderPosts();
    return;
  }
  if (collectingPosts && line.startsWith("BBS_POST ")) {
    postRows.push(line);
    return;
  }
  if (line === "BBS_POST_BEGIN") {
    collectingDetail = true;
    detailRows = [];
    return;
  }
  if (line === "BBS_POST_END") {
    collectingDetail = false;
    renderDetail();
    enableDetailPane();
    return;
  }
  if (collectingDetail && (line.startsWith("BBS_POST ") || line.startsWith("BBS_REPLY "))) {
    detailRows.push(line);
  }
  maybeUploadPendingAttachment(line).catch(reportError);
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
  } catch (error) {
    reportError(error);
  }
}

function switchPage(pageId) {
  document.querySelectorAll(".tab").forEach((tab) => tab.classList.toggle("active", tab.dataset.page === pageId));
  document.querySelectorAll(".page").forEach((page) => page.classList.toggle("active", page.id === pageId));
}

function switchWorkPane(paneId) {
  document.querySelectorAll(".work-tab").forEach((tab) => tab.classList.toggle("active", tab.dataset.work === paneId));
  document.querySelectorAll(".work-pane").forEach((pane) => pane.classList.toggle("active", pane.id === paneId));
}

function enableDetailPane() {
  $("detailTab").disabled = false;
  switchWorkPane("detailPane");
}

function viewPost(id) {
  send(`BBS_VIEW ${id}`).catch(reportError);
}

function selectedFile(inputId) {
  return $(inputId).files && $(inputId).files[0] ? $(inputId).files[0] : null;
}

$("connectBtn").addEventListener("click", () => ensureSession().catch(reportError));

$("registerBtn").addEventListener("click", async () => {
  const data = new FormData($("loginForm"));
  const username = clean(data.get("username"));
  const password = clean(data.get("password"));
  if (!username || !password) {
    setLoginStatus("请填写用户名和密码。");
    return;
  }
  setLoginStatus("正在注册...");
  await send(`REGISTER ${username} ${password}`).catch(reportError);
});

$("loginForm").addEventListener("submit", async (event) => {
  event.preventDefault();
  const data = new FormData(event.currentTarget);
  const username = clean(data.get("username"));
  const password = clean(data.get("password"));
  if (!username || !password) {
    setLoginStatus("请填写用户名和密码。");
    return;
  }
  setLoginStatus("正在登录...");
  await send(`LOGIN ${username} ${password}`).catch(reportError);
});

document.querySelectorAll(".tab").forEach((tab) => {
  tab.addEventListener("click", () => switchPage(tab.dataset.page));
});

document.querySelectorAll(".work-tab").forEach((tab) => {
  tab.addEventListener("click", () => {
    if (!tab.disabled) switchWorkPane(tab.dataset.work);
  });
});

$("refreshUsersBtn").addEventListener("click", () => send("WHO").catch(reportError));
$("historyBtn").addEventListener("click", () => send("HISTORY").catch(reportError));
$("refreshPostsBtn").addEventListener("click", () => send("BBS_LIST").catch(reportError));

$("groupMessage").addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    $("sendGroupBtn").click();
  }
});

$("sendGroupBtn").addEventListener("click", () => {
  const message = clean($("groupMessage").value);
  if (!message) return;
  $("groupMessage").value = "";
  send(`GROUP ${message}`).catch(reportError);
});

$("sendPrivateBtn").addEventListener("click", () => {
  const target = clean($("privateTarget").value);
  const message = clean($("groupMessage").value);
  if (!target) {
    showStatus("请先填写私聊对象，或双击右侧在线用户。", false);
    return;
  }
  if (!message) {
    showStatus("请输入要发送的消息内容。", false);
    return;
  }
  $("groupMessage").value = "";
  send(`PRIVATE ${target} ${message}`).catch(reportError);
});

$("postAttachment").addEventListener("change", () => {
  selectedPostAttachment = selectedFile("postAttachment");
  $("postAttachmentName").value = selectedPostAttachment ? selectedPostAttachment.name : "";
});

$("replyAttachment").addEventListener("change", () => {
  selectedReplyAttachment = selectedFile("replyAttachment");
  $("replyAttachmentName").value = selectedReplyAttachment ? selectedReplyAttachment.name : "";
});

$("clearPostAttachmentBtn").addEventListener("click", () => {
  selectedPostAttachment = null;
  $("postAttachment").value = "";
  $("postAttachmentName").value = "";
});

$("clearReplyAttachmentBtn").addEventListener("click", () => {
  selectedReplyAttachment = null;
  $("replyAttachment").value = "";
  $("replyAttachmentName").value = "";
});

$("createPostBtn").addEventListener("click", async () => {
  const title = clean($("postTitle").value);
  const content = clean($("postContent").value);
  if (!title || !content) {
    showStatus("标题和内容不能为空。", false);
    return;
  }
  pendingPostAttachment = selectedPostAttachment;
  $("postTitle").value = "";
  $("postContent").value = "";
  selectedPostAttachment = null;
  $("postAttachment").value = "";
  $("postAttachmentName").value = "";
  await send(`BBS_CREATE ${title}|${content}`).catch(reportError);
});

$("replyBtn").addEventListener("click", async () => {
  if (currentPostId <= 0) {
    showStatus("请先在左侧选择一个帖子。", false);
    return;
  }
  const content = clean($("replyContent").value);
  if (!content) {
    showStatus("请填写回复内容。", false);
    return;
  }
  pendingReplyAttachment = selectedReplyAttachment;
  $("replyContent").value = "";
  selectedReplyAttachment = null;
  $("replyAttachment").value = "";
  $("replyAttachmentName").value = "";
  await send(`BBS_REPLY ${currentPostId}|${content}`).catch(reportError);
});

$("chatUploadBtn").addEventListener("click", async () => {
  const target = clean($("chatFileTarget").value);
  const file = selectedFile("chatFile");
  if (!target || !file) {
    showStatus("请填写接收用户并选择文件。", false);
    return;
  }
  await upload(`UPLOAD ${target} ${clean(file.name)} ${file.size}`, file).catch(reportError);
});

$("downloadBtn").addEventListener("click", () => {
  const filename = clean($("downloadName").value);
  if (filename) send(`DOWNLOAD ${filename}`).catch(reportError);
});

$("bbsDownloadBtn").addEventListener("click", () => {
  const id = clean($("bbsDownloadId").value);
  const command = $("bbsDownloadKind").value === "reply" ? "BBS_DOWNLOAD_REPLY" : "BBS_DOWNLOAD_POST";
  if (id) send(`${command} ${id}`).catch(reportError);
});

$("backupBtn").addEventListener("click", () => {
  send(`BACKUP ${clean($("backupLabel").value) || "web"}`).catch(reportError);
});

$("clearLogBtn").addEventListener("click", () => {
  $("consoleLog").innerHTML = "";
});

ensureSession().catch(reportError);
setInterval(poll, 600);
