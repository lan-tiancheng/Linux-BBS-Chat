const SESSION_KEY = "linux-bbs-chat-session";

let session = "";
let cursor = 0;
let currentAccount = "";
let currentNickname = "";
let currentMode = "none";
let currentTarget = "";
let currentTitle = "";
let currentGroupId = "";
let currentPostId = 0;
let selectedPostAttachment = null;
let selectedReplyAttachment = null;
let pendingPostAttachment = null;
let pendingReplyAttachment = null;
let collectingPosts = false;
let postRows = [];
let collectingDetail = false;
let detailRows = [];
let collectingFriends = false;
let collectingRequests = false;
let collectingSentRequests = false;
let collectingGroups = false;
let collectingNotifications = false;
let collectingPrivateHistory = false;
let collectingGroupHistory = false;
let loadingConversationKey = "";
let activeConversationKey = "";
let conversations = {};
let friends = [];
let requests = [];
let sentRequests = [];
let groups = [];
let notifications = [];

const $ = (id) => document.getElementById(id);

function clean(value) {
  return String(value || "").replace(/[|\r\n]/g, " ").trim();
}

function escapeHtml(value) {
  return String(value || "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}

function avatarText(value) {
  const text = String(value || "?").trim();
  return (text[0] || "?").toUpperCase();
}

function humanizeServerMessage(message) {
  const text = String(message || "").trim();
  const normalized = text.replace(/\s+/g, " ");
  const messages = {
    "ERR invalid account, password, or nickname": "账号、密码或昵称格式不正确",
    "ERR bad password": "密码错误，请重新输入",
    "ERR user not found": "未找到该用户，请检查账号或昵称",
    "ERR user already logged in": "该账号已在其他窗口保持登录，请先退出原会话，或重启演示容器后再登录",
    "ERR already logged in": "当前会话已经登录，请先退出后再切换账号",
    "ERR login required": "请先登录后再操作",
    "ERR account or nickname already exists": "账号或昵称已存在",
    "ERR group needs at least one friend": "创建群聊至少需要选择一位好友",
    "ERR group not found": "未找到该群聊，可能你不是群成员",
    "ERR no incoming request from this user": "对方还没有向你发送私聊请求",
    "ERR recipient does not exist": "接收方不存在",
    "ERR invalid file size": "文件大小不合法",
    "ERR invalid filename": "文件名不合法",
    "ERR attachment not found": "附件不存在或已失效",
    "ERR post not found": "帖子不存在或已被删除",
    "ERR invalid post id": "帖子编号不合法",
    "ERR invalid BBS title/content": "标题或正文包含不支持的字符，请去掉竖线 | 或换行",
    "ERR invalid BBS reply content": "回复内容包含不支持的字符，请去掉竖线 | 或换行",
    "ERR invalid": "输入内容不合法，请检查后重试",
    "invalid": "输入内容不合法，请检查后重试",
  };
  if (messages[normalized]) return messages[normalized];
  if (normalized.startsWith("ERR ")) return "操作没有成功，请检查输入后重试";
  return text;
}

function rawValue(id) {
  return String($(id).value || "").trim();
}

function isValidAccount(value) {
  return /^\d{9}$/.test(value);
}

function isValidPassword(value) {
  return /^(?=.*[A-Za-z])(?=.*\d).{7,}$/.test(value);
}

function isValidNickname(value) {
  return Boolean(value) && !/[\s|\r\n]/.test(value);
}

function hasBbsIllegalChars(value) {
  return /[|\r\n]/.test(String(value || ""));
}

function isValidFilename(value) {
  return Boolean(value) && !/[\s\\/:*?"<>|\r\n]/.test(value);
}

function showFormError(message) {
  const text = humanizeServerMessage(message);
  setLoginStatus(text);
  showStatus(text, false);
  showToast("请检查输入", text, "error");
}

function setLoginStatus(text) {
  $("loginStatus").textContent = humanizeServerMessage(text);
}

function showStatus(text, ok = true) {
  $("appStatus").textContent = humanizeServerMessage(text);
  $("statusDot").className = ok ? "ok" : "bad";
}

function log(line, kind = "line") {
  const div = document.createElement("div");
  div.textContent = `[${new Date().toLocaleTimeString()}] ${line}`;
  if (kind === "error") div.style.color = "#b3261e";
  $("consoleLog").appendChild(div);
  $("consoleLog").scrollTop = $("consoleLog").scrollHeight;
}

function showToast(title, message = "", type = "info") {
  const stack = $("toastStack");
  if (!stack) return;
  const toast = document.createElement("div");
  const heading = document.createElement("strong");
  toast.className = `toast ${type}`;
  heading.textContent = humanizeServerMessage(title);
  toast.appendChild(heading);
  if (message) {
    const body = document.createElement("span");
    body.textContent = humanizeServerMessage(message);
    toast.appendChild(body);
  }
  stack.prepend(toast);
  setTimeout(() => toast.remove(), 4200);
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
  const saved = localStorage.getItem(SESSION_KEY) || "";
  const data = await api("/api/session", saved ? { session: saved } : {});
  session = data.session;
  localStorage.setItem(SESSION_KEY, session);
  setLoginStatus(data.resumed ? "已恢复浏览器会话。" : "已连接后端。");
  if (data.greeting) log(`< ${data.greeting}`);
  if (data.currentAccount) {
    activateShell(data.currentAccount, data.currentNickname || data.currentAccount, true);
  }
}

async function send(command) {
  await ensureSession();
  log(`> ${command}`);
  await api("/api/command", { session, command });
}

async function resetBackendSession(logout = true) {
  if (session) {
    try {
      await api("/api/close", { session, logout });
    } catch (error) {
      log(`reset session: ${error.message || error}`, "error");
    }
  }
  session = "";
  cursor = 0;
  localStorage.removeItem(SESSION_KEY);
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

function parseIdentity(text) {
  const [account = "", nickname = ""] = String(text || "").split("|");
  return { account, nickname: nickname || account };
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
    replies: parts[6] || "",
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
    replies: parts[6] || "",
  };
}

function parseNotification(line) {
  const parts = line.replace('NOTIFICATION ', '').split('|');
  return {
    id: parts[0] || '',
    type: parts[1] || '',
    target: parts[2] || '',
    message: parts[3] || '',
    createdAt: parts[4] || '',
    read: parts[5] === '1',
  };
}

function notificationLabel(type) {
  const labels = {
    GROUP_INVITED: '群聊邀请',
    BBS_POST_CREATED: '新帖子',
    BBS_REPLY_CREATED: '帖子新回复',
    FILE_READY: '文件已到达',
    PRIVATE_REQUEST: '私信请求',
    PRIVATE_MESSAGE: '私信消息',
  };
  return labels[type] || type;
}

function renderNotifications() {
  const unread = notifications.filter((item) => !item.read).length;
  const badge = document.getElementById('notificationBadge');
  badge.textContent = String(unread);
  badge.classList.toggle('hidden', unread === 0);

  const list = document.getElementById('notificationList');
  list.innerHTML = '';
  if (notifications.length === 0) {
    const empty = document.createElement('div');
    empty.className = 'empty';
    empty.textContent = '暂无新通知';
    list.appendChild(empty);
    return;
  }
  notifications.slice().reverse().forEach((item) => {
    const button = document.createElement('button');
    const body = document.createElement('div');
    const title = document.createElement('strong');
    const message = document.createElement('span');
    const time = document.createElement('small');
    const state = document.createElement('span');

    button.type = 'button';
    button.className = 'notification-row' + (item.read ? '' : ' unread');
    title.textContent = notificationLabel(item.type);
    message.textContent = item.message || item.target || '-';
    time.textContent = item.createdAt;
    state.className = 'notification-state';
    state.textContent = item.read ? '已读' : '未读';
    body.appendChild(title);
    body.appendChild(message);
    body.appendChild(time);
    button.appendChild(body);
    button.appendChild(state);
    button.addEventListener('click', () => openNotification(item));
    list.appendChild(button);
  });
}

function openNotification(item) {
  if (!item.read && item.id) {
    item.read = true;
    renderNotifications();
    send('MARK_READ ' + item.id).catch(reportError);
  }
  if (item.type === 'GROUP_INVITED') {
    switchPage('chatPage');
    send('GROUPS').catch(reportError);
    const group = groups.find((entry) => entry.id === item.target);
    if (group) selectGroup(group.id, group.name);
    return;
  }
  if (item.type === 'BBS_REPLY_CREATED' || item.type === 'BBS_POST_CREATED') {
    switchPage('bbsPage');
    send('BBS_LIST').catch(reportError);
    if (item.target) send('BBS_VIEW ' + item.target).catch(reportError);
    return;
  }
  if (item.type === 'FILE_READY') {
    switchPage('toolsPage');
    if (item.target) document.getElementById('downloadName').value = item.target;
    document.getElementById('downloadName').focus();
    return;
  }
  if (item.type === 'PRIVATE_REQUEST' || item.type === 'PRIVATE_MESSAGE') {
    switchPage('chatPage');
    send('FRIENDS').catch(reportError);
    send('REQUESTS').catch(reportError);
    if (item.target) selectPrivate(item.target, item.target, item.type === 'PRIVATE_REQUEST' ? 'reply' : 'start');
  }
}

function activateShell(account, nickname, resumed = false) {
  currentAccount = account;
  currentNickname = nickname || account;
  $("loginShell").classList.add("hidden");
  $("appShell").classList.remove("hidden");
  $("sessionText").textContent = `${currentNickname} (${currentAccount})`;
  showStatus(resumed ? `已恢复登录：${currentNickname}` : `已登录：${currentNickname}`);
  refreshAll();
}

function resetAppState() {
  currentMode = "none";
  currentTarget = "";
  currentTitle = "";
  currentGroupId = "";
  currentPostId = 0;
  activeConversationKey = "";
  loadingConversationKey = "";
  conversations = {};
  friends = [];
  requests = [];
  sentRequests = [];
  groups = [];
  notifications = [];
  postRows = [];
  detailRows = [];
  selectedPostAttachment = null;
  selectedReplyAttachment = null;
  pendingPostAttachment = null;
  pendingReplyAttachment = null;
  document.querySelectorAll("input, textarea").forEach((field) => {
    if (field.type === "file") {
      field.value = "";
    } else if (!field.disabled) {
      field.value = "";
    }
  });
  $("searchResult").innerHTML = "";
  $("requestList").innerHTML = "";
  $("friendList").innerHTML = "";
  $("groupFriendChecks").innerHTML = "";
  $("groupList").innerHTML = "";
  document.getElementById("notificationList").innerHTML = "";
  document.getElementById("notificationBadge").classList.add("hidden");
  $("postList").innerHTML = "";
  $("postDetail").innerHTML = `<div class="empty">选择帖子查看详情。</div>`;
  $("chatFeed").innerHTML = `<div class="empty">选择一个好友或群聊开始聊天</div>`;
  $("conversationTitle").textContent = "选择会话";
  $("conversationHint").textContent = "可从搜索结果、私信请求、好友或群列表进入会话。";
}

function resetToLogin(text) {
  currentAccount = "";
  currentNickname = "";
  session = "";
  cursor = 0;
  localStorage.removeItem(SESSION_KEY);
  resetAppState();
  $("loginForm").reset();
  $("registerForm").reset();
  $("appShell").classList.add("hidden");
  $("loginShell").classList.remove("hidden");
  setLoginStatus(text);
}

function reportError(error) {
  const text = humanizeServerMessage(error.message || String(error));
  setLoginStatus(text);
  showStatus(text, false);
  showToast("操作失败", text, "error");
  log(error.message || String(error), "error");
}

function conversationKey(type, id) {
  return `${type}:${id}`;
}

function ensureConversation(key, title = "", hint = "") {
  if (!conversations[key]) {
    conversations[key] = { title, hint, messages: [], loaded: false };
  }
  if (title) conversations[key].title = title;
  if (hint) conversations[key].hint = hint;
  return conversations[key];
}

function messageId(item) {
  return `${item.sender}|${item.text}|${item.kind.replace(/^history\s+/, "")}`;
}

function addConversationMessage(key, item) {
  const box = ensureConversation(key);
  const id = messageId(item);
  if (box.messages.some((message) => messageId(message) === id)) {
    return;
  }
  box.messages.push(item);
  if (box.messages.length > 300) {
    box.messages.splice(0, box.messages.length - 300);
  }
  if (activeConversationKey === key) {
    renderActiveConversation();
  }
}

function renderActiveConversation() {
  const feed = $("chatFeed");
  const box = activeConversationKey ? ensureConversation(activeConversationKey) : null;
  feed.innerHTML = "";
  if (!box || box.messages.length === 0) {
    feed.innerHTML = `<div class="empty">${box && !box.loaded ? "正在加载会话..." : "选择一个好友或群聊开始聊天"}</div>`;
    return;
  }
  box.messages.forEach((item) => {
    const row = document.createElement("div");
    const avatar = document.createElement("div");
    const bubble = document.createElement("div");
    const meta = document.createElement("div");
    const text = document.createElement("div");
    row.className = `message-row ${item.mine ? "me" : "peer"}`;
    avatar.className = "message-avatar";
    avatar.textContent = avatarText(item.title);
    bubble.className = `bubble ${item.kind}`;
    meta.className = "message-meta";
    meta.innerHTML = `<span>${escapeHtml(item.title)}</span>${item.time ? `<time>${escapeHtml(item.time)}</time>` : ""}`;
    text.className = "message-text";
    text.textContent = item.text;
    bubble.appendChild(meta);
    bubble.appendChild(text);
    row.appendChild(avatar);
    row.appendChild(bubble);
    feed.appendChild(row);
  });
  feed.scrollTop = feed.scrollHeight;
}

function selectPrivate(account, nickname, mode = "start") {
  currentMode = mode === "reply" ? "private-reply" : "private";
  currentTarget = account;
  currentTitle = nickname || account;
  currentGroupId = "";
  activeConversationKey = conversationKey("private", account);
  ensureConversation(
    activeConversationKey,
    `私聊：${currentTitle}`,
    currentMode === "private-reply"
      ? "回复后双方会自动成为好友，之后可以继续私聊。"
      : "非好友可先发送一条私信，对方回复后成为好友。"
  );
  $("conversationTitle").textContent = `私聊：${currentTitle}`;
  $("conversationHint").textContent =
    currentMode === "private-reply"
      ? "回复后双方会自动成为好友，之后可以继续私聊。"
      : "非好友可先发送一条私信，对方回复后成为好友。";
  renderActiveConversation();
  loadingConversationKey = activeConversationKey;
  send(`HISTORY_PRIVATE ${account}`).catch(reportError);
  $("messageInput").focus();
}

function selectGroup(id, name) {
  currentMode = "group";
  currentGroupId = id;
  currentTarget = "";
  currentTitle = name;
  activeConversationKey = conversationKey("group", id);
  ensureConversation(activeConversationKey, `群聊：${name}`, `群 ID：${id}`);
  $("conversationTitle").textContent = `群聊：${name}`;
  $("conversationHint").textContent = `群 ID：${id}`;
  renderActiveConversation();
  loadingConversationKey = activeConversationKey;
  send(`HISTORY_GROUP ${id}`).catch(reportError);
  $("messageInput").focus();
}

function personButton(person, actionText, onClick) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "person-row";
  button.innerHTML = `
    <span class="row-avatar">${escapeHtml(avatarText(person.nickname || person.account))}</span>
    <span class="row-main">
      <strong>${escapeHtml(person.nickname)}</strong>
      <span>${escapeHtml(actionText)}</span>
    </span>
    <span class="row-meta">${escapeHtml(person.account)}</span>`;
  button.addEventListener("click", onClick);
  return button;
}

function nicknameFor(account) {
  if (account === currentAccount) return currentNickname || "我";
  const user = [...friends, ...requests, ...sentRequests].find((item) => item.account === account);
  return user ? user.nickname : account;
}

function renderFriends() {
  $("friendList").innerHTML = "";
  $("groupFriendChecks").innerHTML = "";
  if (friends.length === 0) {
    $("friendList").innerHTML = `<div class="empty">还没有好友，搜索账号或昵称开始私聊</div>`;
    $("groupFriendChecks").innerHTML = `<div class="empty">先通过私聊互相回复成为好友。</div>`;
    return;
  }
  friends.forEach((friend) => {
    $("friendList").appendChild(personButton(friend, "点击进入私聊", () => selectPrivate(friend.account, friend.nickname)));

    const label = document.createElement("label");
    label.innerHTML = `<input type="checkbox" value="${friend.account}"><span>${friend.nickname}</span>`;
    $("groupFriendChecks").appendChild(label);
  });
}

function renderSentRequests() {
  let section = $("sentRequestList");
  if (!section) {
    section = document.createElement("div");
    section.id = "sentRequestList";
    section.className = "list-stack";
    $("searchResult").after(section);
  }
  section.innerHTML = "";
  if (sentRequests.length === 0) {
    return;
  }
  const title = document.createElement("h4");
  title.textContent = "等待回复";
  section.appendChild(title);
  sentRequests.forEach((request) => {
    const key = conversationKey("private", request.account);
    const box = ensureConversation(key, `私聊：${request.nickname}`, "已发送首条消息，等待对方回复。");
    if (!box.messages.some((message) => message.sender === currentAccount && message.text === request.message)) {
      box.messages.push({
        kind: "private",
        title: "我",
        sender: currentAccount,
        text: request.message,
        time: "",
        mine: true,
      });
      box.loaded = true;
    }
    section.appendChild(
      personButton(request, `等待回复：${request.message}`, () => selectPrivate(request.account, request.nickname))
    );
  });
}

function renderRequests() {
  $("requestList").innerHTML = "";
  if (requests.length === 0) {
    $("requestList").innerHTML = `<div class="empty">暂无私信请求。</div>`;
    return;
  }
  requests.forEach((request) => {
    $("requestList").appendChild(
      personButton(request, `新消息：${request.message}`, () => selectPrivate(request.account, request.nickname, "reply"))
    );
  });
}

function renderGroups() {
  $("groupList").innerHTML = "";
  if (groups.length === 0) {
    $("groupList").innerHTML = `<div class="empty">还没有群聊，可以先和好友互相回复后创建群聊</div>`;
    return;
  }
  groups.forEach((group) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "group-row";
    button.innerHTML = `
      <span class="row-avatar group-avatar">${escapeHtml(avatarText(group.name))}</span>
      <span class="row-main">
        <strong>${escapeHtml(group.name)}</strong>
        <span>点击进入群聊</span>
      </span>
      <span class="row-meta">#${escapeHtml(group.id)}</span>`;
    button.addEventListener("click", () => selectGroup(group.id, group.name));
    $("groupList").appendChild(button);
  });
}

function renderPosts() {
  $("postList").innerHTML = "";
  if (postRows.length === 0) {
    $("postList").innerHTML = `<div class="empty">暂无帖子。</div>`;
    return;
  }
  postRows.map(parseBbsPost).forEach((post) => {
    const card = document.createElement("button");
    card.type = "button";
    card.className = "post-card";
    card.dataset.id = String(post.id);
    const title = post.title.length > 30 ? `${post.title.slice(0, 30)}...` : post.title;
    const body = post.content.length > 58 ? `${post.content.slice(0, 58)}...` : post.content;
    const hasAttachment = post.attachment && post.attachment !== "none";
    const replies = post.replies ? `${post.replies} 回复` : "回复 --";
    card.innerHTML = `
      <span class="post-card-top">
        <span class="row-avatar post-avatar">${escapeHtml(avatarText(post.author))}</span>
        <span class="post-title-wrap">
          <strong class="post-title">${escapeHtml(title)}</strong>
          <span class="post-meta">#${post.id} · ${escapeHtml(post.author || "unknown")} · ${escapeHtml(post.time || "时间未知")}</span>
        </span>
      </span>
      <span class="post-excerpt">${escapeHtml(body || "无正文预览")}</span>
      <span class="post-badges">
        <span class="pill primary">${replies}</span>
        <span class="pill ${hasAttachment ? "primary" : "muted"}">${hasAttachment ? "有附件" : "无附件"}</span>
      </span>`;
    card.addEventListener("click", () => viewPost(post.id));
    $("postList").appendChild(card);
  });
}

function authorLink(name) {
  return `<button class="text-btn author-link" data-author="${name}">${name}</button>`;
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
  postBlock.className = "detail-post";
  const postLink =
    post.attachment !== "none"
      ? `<br><a href="#" data-download-post="${post.id}">下载帖子附件：${post.attachment}</a>`
      : "";
  postBlock.innerHTML = `
    <div class="detail-head">
      <span class="row-avatar post-avatar">${escapeHtml(avatarText(post.author))}</span>
      <div>
        <b>#${post.id} ${escapeHtml(post.title)}</b>
        <span class="time">${authorLink(post.author)} · ${escapeHtml(post.time)}</span>
      </div>
    </div>
    <div class="detail-body">${escapeHtml(post.content)}</div>
    ${postLink}`;
  $("postDetail").appendChild(postBlock);

  const replies = detailRows.filter((line) => line.startsWith("BBS_REPLY ")).map(parseBbsReply);
  if (replies.length === 0) {
    $("postDetail").insertAdjacentHTML("beforeend", `<div class="empty">暂无回复。</div>`);
  } else {
    replies.forEach((reply) => {
      const div = document.createElement("div");
      div.className = "reply-item";
      const link =
        reply.attachment !== "none"
          ? `<br><a href="#" data-download-reply="${reply.id}">下载回复附件：${reply.attachment}</a>`
          : "";
      div.innerHTML = `
        <div class="detail-head">
          <span class="row-avatar">${escapeHtml(avatarText(reply.author))}</span>
          <div>
            <b>回复 #${reply.id}</b>
            <span class="time">${authorLink(reply.author)} · ${escapeHtml(reply.time)}</span>
          </div>
        </div>
        <div class="detail-body">${escapeHtml(reply.content)}</div>
        ${link}`;
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
  $("postDetail").querySelectorAll("[data-author]").forEach((button) => {
    button.addEventListener("click", () => {
      const nickname = button.dataset.author;
      if (nickname === currentNickname) {
        showStatus("不能给自己发私信。", false);
        return;
      }
      send(`SEARCH_USER ${nickname}`).catch(reportError);
      switchPage("chatPage");
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
  showToast("文件已准备", event.filename, "success");
}

async function maybeUploadPendingAttachment(line) {
  const postMatch = /^OK post (\d+) created/.exec(line);
  if (postMatch) {
    const file = pendingPostAttachment;
    pendingPostAttachment = null;
    if (file) await upload(`BBS_UPLOAD_POST ${postMatch[1]} ${clean(file.name)} ${file.size}`, file);
    await send("BBS_LIST");
    return;
  }
  const replyMatch = /^OK reply (\d+) created/.exec(line);
  if (replyMatch) {
    const file = pendingReplyAttachment;
    pendingReplyAttachment = null;
    if (file) await upload(`BBS_UPLOAD_REPLY ${replyMatch[1]} ${clean(file.name)} ${file.size}`, file);
    if (currentPostId > 0) await send(`BBS_VIEW ${currentPostId}`);
  }
}

function handleLine(line) {
  log(`< ${line}`);
  if (line.startsWith("OK logged in ")) {
    const identity = parseIdentity(line.replace("OK logged in ", "").trim());
    activateShell(identity.account, identity.nickname);
    return;
  }
  if (line === "OK logged out") {
    resetToLogin("已退出登录。");
    return;
  }
  if (line === "OK registered") {
    setLoginStatus("注册成功，现在可以登录。");
    showAuth("login");
  }
  if (line.startsWith("ERR ")) {
    const text = humanizeServerMessage(line);
    showStatus(text, false);
    setLoginStatus(text);
    showToast("操作失败", text, "error");
  } else if (line.startsWith("OK ")) {
    showStatus(line);
  }
  if (line === "OK private message sent" || line === "OK private request sent") {
    send("FRIENDS").catch(reportError);
    send("REQUESTS").catch(reportError);
    send("SENT_REQUESTS").catch(reportError);
  }
  if (line === 'OK notification read' || line === 'OK notifications read') {
    send('NOTIFICATIONS').catch(reportError);
  }

  if (line === "FRIENDS_BEGIN") {
    collectingFriends = true;
    friends = [];
    return;
  }
  if (line === "FRIENDS_END") {
    collectingFriends = false;
    renderFriends();
    return;
  }
  if (collectingFriends && line.startsWith("FRIEND ")) {
    friends.push(parseIdentity(line.replace("FRIEND ", "")));
    return;
  }

  if (line === "REQUESTS_BEGIN") {
    collectingRequests = true;
    requests = [];
    return;
  }
  if (line === "REQUESTS_END") {
    collectingRequests = false;
    renderRequests();
    return;
  }
  if (collectingRequests && line.startsWith("REQUEST ")) {
    const [account, nickname, ...message] = line.replace("REQUEST ", "").split("|");
    requests.push({ account, nickname: nickname || account, message: message.join("|") });
    return;
  }

  if (line === "SENT_REQUESTS_BEGIN") {
    collectingSentRequests = true;
    sentRequests = [];
    return;
  }
  if (line === "SENT_REQUESTS_END") {
    collectingSentRequests = false;
    renderSentRequests();
    return;
  }
  if (collectingSentRequests && line.startsWith("SENT_REQUEST ")) {
    const [account, nickname, ...message] = line.replace("SENT_REQUEST ", "").split("|");
    sentRequests.push({ account, nickname: nickname || account, message: message.join("|") });
    return;
  }

  if (line === "GROUPS_BEGIN") {
    collectingGroups = true;
    groups = [];
    return;
  }
  if (line === "GROUPS_END") {
    collectingGroups = false;
    renderGroups();
    return;
  }
  if (collectingGroups && line.startsWith("GROUP_ITEM ")) {
    const [id, owner, name] = line.replace("GROUP_ITEM ", "").split("|");
    groups.push({ id, owner, name });
    return;
  }

  if (line === 'NOTIFICATIONS_BEGIN') {
    collectingNotifications = true;
    notifications = [];
    return;
  }
  if (line === 'NOTIFICATIONS_END') {
    collectingNotifications = false;
    renderNotifications();
    return;
  }
  if (collectingNotifications && line.startsWith('NOTIFICATION ')) {
    notifications.push(parseNotification(line));
    return;
  }

  if (line.startsWith("USER ")) {
    const identity = parseIdentity(line.replace("USER ", ""));
    $("searchResult").innerHTML = "";
    $("searchResult").appendChild(
      personButton(identity, "点击发起私信", () => selectPrivate(identity.account, identity.nickname))
    );
    return;
  }

  if (line === "PRIVATE_HISTORY_BEGIN") {
    collectingPrivateHistory = true;
    const box = ensureConversation(loadingConversationKey || activeConversationKey);
    box.messages = [];
    box.loaded = false;
    renderActiveConversation();
    return;
  }
  if (line === "PRIVATE_HISTORY_END") {
    collectingPrivateHistory = false;
    const box = ensureConversation(loadingConversationKey || activeConversationKey);
    box.loaded = true;
    renderActiveConversation();
    return;
  }
  if (line === "GROUP_HISTORY_BEGIN") {
    collectingGroupHistory = true;
    const box = ensureConversation(loadingConversationKey || activeConversationKey);
    box.messages = [];
    box.loaded = false;
    renderActiveConversation();
    return;
  }
  if (line === "GROUP_HISTORY_END") {
    collectingGroupHistory = false;
    const box = ensureConversation(loadingConversationKey || activeConversationKey);
    box.loaded = true;
    renderActiveConversation();
    return;
  }

  if (line.startsWith('EVENT GROUP_INVITED ')) {
    showToast('新群聊邀请', line.replace('EVENT GROUP_INVITED ', ''), 'info');
    send('GROUPS').catch(reportError);
    send('NOTIFICATIONS').catch(reportError);
    return;
  }
  if (line.startsWith('EVENT BBS_POST_CREATED ') || line.startsWith('EVENT BBS_REPLY_CREATED ')) {
    showToast('BBS 有新动态', line.replace(/^EVENT /, ''), 'info');
    send('BBS_LIST').catch(reportError);
    send('NOTIFICATIONS').catch(reportError);
    if (currentPostId > 0) send('BBS_VIEW ' + currentPostId).catch(reportError);
    return;
  }
  if (line.startsWith('FILE_READY ')) {
    showToast('文件已到达', line.replace('FILE_READY ', ''), 'success');
    send('NOTIFICATIONS').catch(reportError);
  }

  if (line.startsWith("PMSG ")) {
    const [, sender, ...message] = line.split(" ");
    const key = conversationKey("private", sender);
    const mine = sender === currentAccount;
    addConversationMessage(key, {
      kind: "private",
      title: mine ? "我" : nicknameFor(sender),
      sender,
      text: message.join(" "),
      time: new Date().toLocaleTimeString(),
      mine,
    });
    send("REQUESTS").catch(reportError);
    send("SENT_REQUESTS").catch(reportError);
    send("FRIENDS").catch(reportError);
  }
  if (line.startsWith("GMSG ")) {
    const [, groupId, sender, ...message] = line.split(" ");
    const key = conversationKey("group", groupId);
    const mine = sender === currentAccount;
    addConversationMessage(key, {
      kind: "group",
      title: mine ? "我" : nicknameFor(sender),
      sender,
      text: message.join(" "),
      time: new Date().toLocaleTimeString(),
      mine,
    });
  }
  if (line.startsWith("HMSG ")) {
    const data = line.replace(/^HMSG /, "").split("|");
    const groupText = data[1] || "group-0";
    const groupId = groupText.replace(/^group-/, "");
    addConversationMessage(conversationKey("group", groupId), {
      kind: "history group",
      title: data[2] === currentAccount ? "我" : nicknameFor(data[2] || ""),
      sender: data[2] || "",
      text: data.slice(3).join("|"),
      time: data[0],
      mine: data[2] === currentAccount,
    });
  }
  if (line.startsWith("HPMSG ")) {
    const data = line.replace(/^HPMSG /, "").split("|");
    const peer = data[1] === currentAccount ? data[2] : data[1];
    const key = collectingPrivateHistory ? loadingConversationKey : conversationKey("private", peer);
    const mine = data[1] === currentAccount;
    addConversationMessage(key, {
      kind: "history private",
      title: mine ? "我" : nicknameFor(data[1]),
      sender: data[1],
      text: data.slice(3).join("|"),
      time: data[0],
      mine,
    });
  }
  if (line.startsWith("HGMSG ")) {
    const data = line.replace(/^HGMSG /, "").split("|");
    const key = collectingGroupHistory ? loadingConversationKey : conversationKey("group", data[1]);
    const mine = data[2] === currentAccount;
    addConversationMessage(key, {
      kind: "history group",
      title: mine ? "我" : nicknameFor(data[2]),
      sender: data[2],
      text: data.slice(3).join("|"),
      time: data[0],
      mine,
    });
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

function showAuth(which) {
  $("loginForm").classList.toggle("active", which === "login");
  $("registerForm").classList.toggle("active", which === "register");
}

function switchPage(pageId) {
  document.querySelectorAll(".tab").forEach((tab) => tab.classList.toggle("active", tab.dataset.page === pageId));
  document.querySelectorAll(".page").forEach((page) => page.classList.toggle("active", page.id === pageId));
}

function selectedFile(inputId) {
  return $(inputId).files && $(inputId).files[0] ? $(inputId).files[0] : null;
}

function refreshAll() {
  send("FRIENDS").catch(reportError);
  send("REQUESTS").catch(reportError);
  send("SENT_REQUESTS").catch(reportError);
  send("GROUPS").catch(reportError);
  send("NOTIFICATIONS").catch(reportError);
  setTimeout(() => send("BBS_LIST").catch(reportError), 150);
}

$("showRegisterBtn").addEventListener("click", () => showAuth("register"));
$("showLoginBtn").addEventListener("click", () => showAuth("login"));

$("registerForm").addEventListener("submit", async (event) => {
  event.preventDefault();
  const data = new FormData(event.currentTarget);
  const account = String(data.get("account") || "").trim();
  const nickname = String(data.get("nickname") || "").trim();
  const password = String(data.get("password") || "");
  if (!isValidAccount(account)) {
    showFormError("账号必须是9位数字");
    return;
  }
  if (!isValidNickname(nickname)) {
    showFormError("昵称不能包含空格、竖线 | 或换行");
    return;
  }
  if (!isValidPassword(password)) {
    showFormError("密码至少7位，并且需要同时包含字母和数字");
    return;
  }
  setLoginStatus("正在注册...");
  await resetBackendSession(true);
  await ensureSession();
  await send(`REGISTER ${account} ${password} ${nickname}`).catch(reportError);
});

$("loginForm").addEventListener("submit", async (event) => {
  event.preventDefault();
  const data = new FormData(event.currentTarget);
  const login = String(data.get("login") || "").trim();
  const password = String(data.get("password") || "");
  if (!login) {
    showFormError("请填写账号或昵称");
    return;
  }
  if (/^\d+$/.test(login) && !isValidAccount(login)) {
    showFormError("账号必须是9位数字");
    return;
  }
  if (!/^\d+$/.test(login) && !isValidNickname(login)) {
    showFormError("昵称不能包含空格、竖线 | 或换行");
    return;
  }
  if (!isValidPassword(password)) {
    showFormError("密码至少7位，并且需要同时包含字母和数字");
    return;
  }
  setLoginStatus("正在登录...");
  await resetBackendSession(true);
  await ensureSession();
  await send(`LOGIN ${login} ${password}`).catch(reportError);
});
$("logoutBtn").addEventListener("click", async () => {
  try {
    if (session) await send("LOGOUT");
    if (session) await api("/api/close", { session, logout: false });
  } catch (error) {
    reportError(error);
  } finally {
    resetToLogin("已退出登录。");
  }
});

document.querySelectorAll(".tab").forEach((tab) => {
  tab.addEventListener("click", () => switchPage(tab.dataset.page));
});

$("refreshSocialBtn").addEventListener("click", () => {
  send("FRIENDS").catch(reportError);
  send("REQUESTS").catch(reportError);
  send("SENT_REQUESTS").catch(reportError);
});
$("refreshGroupsBtn").addEventListener("click", () => send("GROUPS").catch(reportError));
$("historyBtn").addEventListener("click", () => {
  if (currentMode === "private" || currentMode === "private-reply") {
    loadingConversationKey = activeConversationKey;
    send(`HISTORY_PRIVATE ${currentTarget}`).catch(reportError);
  } else if (currentMode === "group") {
    loadingConversationKey = activeConversationKey;
    send(`HISTORY_GROUP ${currentGroupId}`).catch(reportError);
  } else {
    showStatus("请先选择一个会话。", false);
  }
});
$("refreshPostsBtn").addEventListener("click", () => send("BBS_LIST").catch(reportError));
document.getElementById('refreshNotificationsBtn').addEventListener('click', () => {
  send('NOTIFICATIONS').catch(reportError);
});
document.getElementById('markAllNotificationsBtn').addEventListener('click', () => {
  send('MARK_READ_ALL').catch(reportError);
});

$("searchUserBtn").addEventListener("click", () => {
  const login = clean($("userSearch").value);
  if (login) send(`SEARCH_USER ${login}`).catch(reportError);
});

$("sendMessageBtn").addEventListener("click", () => {
  const message = clean($("messageInput").value);
  if (!message) return;
  $("messageInput").value = "";
  if (currentMode === "private") {
    addConversationMessage(activeConversationKey, {
      kind: "private",
      title: "我",
      sender: currentAccount,
      text: message,
      time: new Date().toLocaleTimeString(),
      mine: true,
    });
    if (!friends.some((friend) => friend.account === currentTarget) &&
        !sentRequests.some((request) => request.account === currentTarget)) {
      sentRequests.push({
        account: currentTarget,
        nickname: currentTitle,
        message,
      });
      renderSentRequests();
    }
    send(`PRIVATE_START ${currentTarget} ${message}`).catch(reportError);
  } else if (currentMode === "private-reply") {
    addConversationMessage(activeConversationKey, {
      kind: "private",
      title: "我",
      sender: currentAccount,
      text: message,
      time: new Date().toLocaleTimeString(),
      mine: true,
    });
    send(`PRIVATE_REPLY ${currentTarget} ${message}`).catch(reportError);
  } else if (currentMode === "group") {
    addConversationMessage(activeConversationKey, {
      kind: "group",
      title: "我",
      sender: currentAccount,
      text: message,
      time: new Date().toLocaleTimeString(),
      mine: true,
    });
    send(`GROUP_SEND ${currentGroupId} ${message}`).catch(reportError);
  } else {
    showStatus("请先选择私聊或群聊。", false);
  }
});

$("messageInput").addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    $("sendMessageBtn").click();
  }
});

$("createGroupBtn").addEventListener("click", () => {
  const name = rawValue("groupName");
  const members = Array.from($("groupFriendChecks").querySelectorAll("input:checked")).map((input) => input.value);
  if (!name) {
    showFormError("群名不能为空");
    return;
  }
  if (members.length === 0) {
    showFormError("ERR group needs at least one friend");
    return;
  }
  $("groupName").value = "";
  send(`GROUP_CREATE ${clean(name)} ${members.join(",")}`).catch(reportError);
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
  const title = rawValue("postTitle");
  const content = rawValue("postContent");
  if (!title || !content) {
    showFormError("标题和正文不能为空");
    return;
  }
  if (hasBbsIllegalChars(title) || hasBbsIllegalChars(content)) {
    showFormError("ERR invalid BBS title/content");
    return;
  }
  if (selectedPostAttachment && !isValidFilename(selectedPostAttachment.name)) {
    showFormError("文件名不能包含空格、竖线 |、换行或系统保留字符");
    return;
  }
  pendingPostAttachment = selectedPostAttachment;
  $("postTitle").value = "";
  $("postContent").value = "";
  selectedPostAttachment = null;
  $("postAttachment").value = "";
  $("postAttachmentName").value = "";
  await send(`BBS_CREATE ${clean(title)}|${clean(content)}`).catch(reportError);
});

$("replyBtn").addEventListener("click", async () => {
  if (currentPostId <= 0) {
    showFormError("请先选择一个帖子");
    return;
  }
  const content = rawValue("replyContent");
  if (!content) {
    showFormError("请填写回复内容");
    return;
  }
  if (hasBbsIllegalChars(content)) {
    showFormError("ERR invalid BBS reply content");
    return;
  }
  if (selectedReplyAttachment && !isValidFilename(selectedReplyAttachment.name)) {
    showFormError("文件名不能包含空格、竖线 |、换行或系统保留字符");
    return;
  }
  pendingReplyAttachment = selectedReplyAttachment;
  $("replyContent").value = "";
  selectedReplyAttachment = null;
  $("replyAttachment").value = "";
  $("replyAttachmentName").value = "";
  await send(`BBS_REPLY ${currentPostId}|${clean(content)}`).catch(reportError);
});
function viewPost(id) {
  send(`BBS_VIEW ${id}`).catch(reportError);
}

$("chatUploadBtn").addEventListener("click", async () => {
  const target = rawValue("chatFileTarget");
  const file = selectedFile("chatFile");
  if (!target) {
    showFormError("请填写接收方账号或昵称");
    return;
  }
  if (/^\d+$/.test(target) && !isValidAccount(target)) {
    showFormError("账号必须是9位数字");
    return;
  }
  if (!/^\d+$/.test(target) && !isValidNickname(target)) {
    showFormError("昵称不能包含空格、竖线 | 或换行");
    return;
  }
  if (!file) {
    showFormError("请选择要上传的文件");
    return;
  }
  if (!isValidFilename(file.name)) {
    showFormError("文件名不能包含空格、竖线 |、换行或系统保留字符");
    return;
  }
  await upload(`UPLOAD ${target} ${clean(file.name)} ${file.size}`, file).catch(reportError);
});

$("downloadBtn").addEventListener("click", () => {
  const filename = rawValue("downloadName");
  if (!filename) {
    showFormError("请填写文件名");
    return;
  }
  if (!isValidFilename(filename)) {
    showFormError("ERR invalid filename");
    return;
  }
  send(`DOWNLOAD ${clean(filename)}`).catch(reportError);
});

$("bbsDownloadBtn").addEventListener("click", () => {
  const id = rawValue("bbsDownloadId");
  const command = $("bbsDownloadKind").value === "reply" ? "BBS_DOWNLOAD_REPLY" : "BBS_DOWNLOAD_POST";
  if (!/^\d+$/.test(id)) {
    showFormError("ERR invalid post id");
    return;
  }
  send(`${command} ${id}`).catch(reportError);
});
$("backupBtn").addEventListener("click", () => {
  send(`BACKUP ${clean($("backupLabel").value) || "web"}`).catch(reportError);
});

$("clearLogBtn").addEventListener("click", () => {
  $("consoleLog").innerHTML = "";
});

ensureSession().catch(reportError);
setInterval(poll, 600);





