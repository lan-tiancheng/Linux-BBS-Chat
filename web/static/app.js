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
let collectingPrivateHistory = false;
let collectingGroupHistory = false;
let loadingConversationKey = "";
let activeConversationKey = "";
let conversations = {};
let friends = [];
let requests = [];
let sentRequests = [];
let groups = [];

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
  $("postList").innerHTML = "";
  $("postDetail").innerHTML = `<div class="empty">选择帖子查看详情。</div>`;
  $("chatFeed").innerHTML = `<div class="empty">暂无消息。</div>`;
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
  const text = error.message || String(error);
  setLoginStatus(text);
  showStatus(text, false);
  log(text, "error");
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
    feed.innerHTML = `<div class="empty">${box && !box.loaded ? "正在加载会话..." : "暂无消息。"}</div>`;
    return;
  }
  box.messages.forEach((item) => {
    const div = document.createElement("div");
    div.className = `bubble ${item.kind}${item.mine ? " me" : ""}`;
    div.innerHTML = `<span class="tag">${item.title}</span>${item.time ? `<span class="time"> ${item.time}</span>` : ""}<br>${item.text}`;
    feed.appendChild(div);
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
  button.innerHTML = `<strong>${person.nickname}</strong><span>${person.account}</span><span>${actionText}</span>`;
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
    $("friendList").innerHTML = `<div class="empty">暂无好友。</div>`;
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
    $("groupList").innerHTML = `<div class="empty">暂无群聊。</div>`;
    return;
  }
  groups.forEach((group) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "group-row";
    button.innerHTML = `<strong>${group.name}</strong><span>ID ${group.id}</span><span>点击进入群聊</span>`;
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
    const title = post.title.length > 24 ? `${post.title.slice(0, 24)}...` : post.title;
    const body = post.content.length > 42 ? `${post.content.slice(0, 42)}...` : post.content;
    const file = post.attachment && post.attachment !== "none" ? " · 附件" : "";
    card.innerHTML = `<strong>${title}</strong><span>${body}</span><small>#${post.id} · ${post.author}${file}</small>`;
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
  postBlock.className = "bubble";
  const postLink =
    post.attachment !== "none"
      ? `<br><a href="#" data-download-post="${post.id}">下载帖子附件：${post.attachment}</a>`
      : "";
  postBlock.innerHTML = `<b>#${post.id} ${post.title}</b><span class="time"> ${authorLink(post.author)} · ${post.time}</span><br>${post.content}${postLink}`;
  $("postDetail").appendChild(postBlock);

  const replies = detailRows.filter((line) => line.startsWith("BBS_REPLY ")).map(parseBbsReply);
  if (replies.length === 0) {
    $("postDetail").insertAdjacentHTML("beforeend", `<div class="empty">暂无回复。</div>`);
  } else {
    replies.forEach((reply) => {
      const div = document.createElement("div");
      div.className = "bubble history";
      const link =
        reply.attachment !== "none"
          ? `<br><a href="#" data-download-reply="${reply.id}">下载回复附件：${reply.attachment}</a>`
          : "";
      div.innerHTML = `<b>回复 #${reply.id}</b><span class="time"> ${authorLink(reply.author)} · ${reply.time}</span><br>${reply.content}${link}`;
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
    showStatus(line, false);
    setLoginStatus(line);
  } else if (line.startsWith("OK ")) {
    showStatus(line);
  }
  if (line === "OK private message sent" || line === "OK private request sent") {
    send("FRIENDS").catch(reportError);
    send("REQUESTS").catch(reportError);
    send("SENT_REQUESTS").catch(reportError);
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
  setTimeout(() => send("BBS_LIST").catch(reportError), 150);
}

$("showRegisterBtn").addEventListener("click", () => showAuth("register"));
$("showLoginBtn").addEventListener("click", () => showAuth("login"));

$("registerForm").addEventListener("submit", async (event) => {
  event.preventDefault();
  const data = new FormData(event.currentTarget);
  const account = clean(data.get("account"));
  const nickname = clean(data.get("nickname"));
  const password = clean(data.get("password"));
  if (!/^\d{9}$/.test(account)) {
    setLoginStatus("账号必须是9位数字。");
    return;
  }
  if (!/^(?=.*[A-Za-z])(?=.*\d).{7,}$/.test(password)) {
    setLoginStatus("密码必须同时包含数字和字母，并且长度大于6位。");
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
  const login = clean(data.get("login"));
  const password = clean(data.get("password"));
  if (!login || !password) {
    setLoginStatus("请填写账号或昵称和密码。");
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
  const name = clean($("groupName").value);
  const members = Array.from($("groupFriendChecks").querySelectorAll("input:checked")).map((input) => input.value);
  if (!name || members.length === 0) {
    showStatus("请填写群名并至少选择一位好友。", false);
    return;
  }
  $("groupName").value = "";
  send(`GROUP_CREATE ${name} ${members.join(",")}`).catch(reportError);
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
    showStatus("请先选择一个帖子。", false);
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

function viewPost(id) {
  send(`BBS_VIEW ${id}`).catch(reportError);
}

$("chatUploadBtn").addEventListener("click", async () => {
  const target = clean($("chatFileTarget").value);
  const file = selectedFile("chatFile");
  if (!target || !file) {
    showStatus("请填写接收方并选择文件。", false);
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
