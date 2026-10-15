const demoMode = location.protocol === "file:" || new URLSearchParams(location.search).has("demo");
const identityKey = `pocketdice.identity.${location.host || "demo"}`;

const state = {
  revision: -1,
  players: [],
  activePlayerId: 0,
  hostPlayerId: 0,
  myPlayerId: 0,
  sides: 20,
  modifiers: [],
  lastRoll: { sides: 20, dice: [20], naturalTotal: 20, modifierTotal: 0, total: 20 },
};

let socket;
let reconnectTimer;
let toastTimer;

const actions = [
  { id: "weapon", name: "Weapon damage", detail: "Longsword", value: 4 },
  { id: "power", name: "Power-up", detail: "Arcane charge", value: 3 },
  { id: "blessing", name: "Blessing", detail: "Party buff", value: 2 },
  { id: "wound", name: "Wounded", detail: "Condition", value: -1 },
];

const elements = {
  players: document.querySelector("#players"), actions: document.querySelector("#actions"),
  board: document.querySelector("#diceBoard"), die: document.querySelector("#die"),
  diceCanvas: document.querySelector("#diceCanvas"), dieKind: document.querySelector("#dieKind"),
  result: document.querySelector("#result"), total: document.querySelector("#total"),
  breakdown: document.querySelector("#breakdown"), turnName: document.querySelector("#turnName"),
  formula: document.querySelector("#formulaLabel"), rollFormula: document.querySelector("#rollFormula"),
  modifierTotal: document.querySelector("#modifierTotal"), connection: document.querySelector(".connection"),
  connectionLabel: document.querySelector("#connectionLabel"), themeInput: document.querySelector("#themeInput"),
  joinDialog: document.querySelector("#joinDialog"), joinForm: document.querySelector("#joinForm"),
  playerName: document.querySelector("#playerName"), rollButton: document.querySelector("#rollButton"),
  nextTurn: document.querySelector("#nextTurn"), toast: document.querySelector("#toast"),
};

const modifierTotal = () => state.modifiers.reduce((sum, id) => sum + (actions.find((item) => item.id === id)?.value ?? 0), 0);
const signed = (value) => `${value >= 0 ? "+" : ""}${value}`;
const isMyTurn = () => demoMode || (state.myPlayerId !== 0 && state.myPlayerId === state.activePlayerId);
const escapeHtml = (value) => String(value).replace(/[&<>'"]/g, (character) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;" })[character]);

let animationFrame;
let diceAngle = 0.38;

function themeColor(name, fallback) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || fallback;
}

function mixColor(hex, amount) {
  const color = hex.match(/^#([0-9a-f]{6})$/i)?.[1];
  if (!color) return hex;
  const channel = (offset) => Math.max(0, Math.min(255, parseInt(color.slice(offset, offset + 2), 16) + amount));
  return `rgb(${channel(0)},${channel(2)},${channel(4)})`;
}

function polygon(context, points, fill, stroke = "#ffffff55") {
  context.beginPath();
  points.forEach(([x, y], index) => index ? context.lineTo(x, y) : context.moveTo(x, y));
  context.closePath();
  context.fillStyle = fill;
  context.fill();
  context.strokeStyle = stroke;
  context.lineWidth = 3;
  context.stroke();
}

function ringPoints(cx, cy, radius, count, angle, yScale = 1) {
  return Array.from({ length: count }, (_, index) => {
    const phase = angle + index * Math.PI * 2 / count;
    return [cx + Math.cos(phase) * radius, cy + Math.sin(phase) * radius * yScale];
  });
}

function drawD20(context, cx, cy, radius, angle, light, mid, dark) {
  const phi = (1 + Math.sqrt(5)) / 2;
  const vertices = [
    [-1, phi, 0], [1, phi, 0], [-1, -phi, 0], [1, -phi, 0],
    [0, -1, phi], [0, 1, phi], [0, -1, -phi], [0, 1, -phi],
    [phi, 0, -1], [phi, 0, 1], [-phi, 0, -1], [-phi, 0, 1],
  ];
  const faces = [
    [0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
    [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
    [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
    [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1],
  ];
  const angleX = -.31 + Math.sin(angle * .6) * .12;
  const cosY = Math.cos(angle);
  const sinY = Math.sin(angle);
  const cosX = Math.cos(angleX);
  const sinX = Math.sin(angleX);
  const scale = radius / 1.83;
  const points = vertices.map(([x, y, z]) => {
    const rotatedX = x * cosY + z * sinY;
    const rotatedZ = -x * sinY + z * cosY;
    const rotatedY = y * cosX - rotatedZ * sinX;
    return [cx + rotatedX * scale, cy + rotatedY * scale, y * sinX + rotatedZ * cosX];
  });
  faces.map((face) => ({
    face,
    depth: face.reduce((sum, index) => sum + points[index][2], 0) / 3,
  })).sort((a, b) => a.depth - b.depth).forEach(({ face, depth }) => {
    polygon(context, face.map((index) => points[index]), depth > .65 ? light : depth > -.2 ? mid : dark, "#d8ceff88");
  });
}

function drawDie(angle = diceAngle) {
  const canvas = elements.diceCanvas;
  const context = canvas.getContext("2d");
  const scale = devicePixelRatio || 1;
  const size = canvas.clientWidth || 230;
  canvas.width = Math.round(size * scale);
  canvas.height = Math.round(size * scale);
  context.setTransform(scale, 0, 0, scale, 0, 0);
  context.clearRect(0, 0, size, size);

  const cx = size / 2;
  const cy = size / 2 - 5;
  const radius = size * .39;
  const accent = themeColor("--accent", "#7c5cff");
  const light = mixColor(accent, 54);
  const dark = mixColor(accent, -56);
  const mid = mixColor(accent, -14);
  const sides = state.lastRoll?.sides || state.sides;

  if (sides === 6) {
    const wobble = Math.sin(angle) * radius * .22;
    const top = [cx, cy - radius];
    const left = [cx - radius * .78, cy - radius * .42 + wobble];
    const right = [cx + radius * .78, cy - radius * .42 - wobble];
    const center = [cx, cy - radius * .02];
    const bottomLeft = [cx - radius * .78, cy + radius * .55];
    const bottomRight = [cx + radius * .78, cy + radius * .55];
    polygon(context, [top, right, center, left], light);
    polygon(context, [left, center, bottomRight, bottomLeft], mid);
    polygon(context, [center, right, bottomRight], dark);
  } else if (sides === 4) {
    const points = ringPoints(cx, cy + radius * .12, radius, 3, angle - Math.PI / 2, .9);
    const center = [cx + Math.sin(angle * 1.7) * radius * .15, cy + radius * .08];
    polygon(context, [points[0], points[1], center], light);
    polygon(context, [points[1], points[2], center], dark);
    polygon(context, [points[2], points[0], center], mid);
  } else if (sides === 20) {
    drawD20(context, cx, cy, radius, angle, light, mid, dark);
  } else if (sides === 100) {
    const gradient = context.createRadialGradient(cx - radius * .35, cy - radius * .45, 4, cx, cy, radius);
    gradient.addColorStop(0, light); gradient.addColorStop(.55, accent); gradient.addColorStop(1, dark);
    context.beginPath(); context.arc(cx, cy, radius, 0, Math.PI * 2); context.fillStyle = gradient; context.fill();
    context.strokeStyle = "#ffffff66"; context.lineWidth = 3; context.stroke();
    for (let band = -2; band <= 2; band++) {
      context.beginPath();
      context.ellipse(cx, cy + band * radius * .26, radius * Math.cos(band * .24), radius * .16, angle * .15, 0, Math.PI * 2);
      context.strokeStyle = "#ffffff2e"; context.stroke();
    }
  } else {
    const count = sides === 8 ? 4 : sides === 10 ? 5 : sides === 12 ? 6 : 5;
    const equator = ringPoints(cx, cy, radius * .9, count, angle, .48);
    const top = [cx + Math.sin(angle * 1.3) * radius * .13, cy - radius];
    const bottom = [cx - Math.sin(angle * 1.3) * radius * .13, cy + radius];
    equator.forEach((point, index) => {
      const next = equator[(index + 1) % equator.length];
      polygon(context, [top, point, next], index % 2 ? mid : light);
      polygon(context, [bottom, next, point], index % 2 ? dark : mid);
    });
  }
}

function animateDie() {
  cancelAnimationFrame(animationFrame);
  const started = performance.now();
  const duration = 720;
  const frame = (now) => {
    const progress = Math.min(1, (now - started) / duration);
    diceAngle += .17 + (1 - progress) * .32;
    drawDie(diceAngle);
    if (progress < 1) animationFrame = requestAnimationFrame(frame);
  };
  animationFrame = requestAnimationFrame(frame);
}

function showToast(message) {
  clearTimeout(toastTimer);
  elements.toast.textContent = message;
  elements.toast.hidden = false;
  toastTimer = setTimeout(() => { elements.toast.hidden = true; }, 4200);
}

function render() {
  const active = state.players.find((player) => player.id === state.activePlayerId);
  elements.players.innerHTML = state.players.length ? state.players.map((player) => {
    const labels = [];
    if (player.id === state.myPlayerId) labels.push("You");
    if (player.id === state.hostPlayerId) labels.push("Host");
    if (!player.connected) labels.push("Offline");
    if (player.id === state.activePlayerId) labels.push("Rolling now");
    return `<article class="player-card ${player.id === state.activePlayerId ? "active" : ""} ${player.connected ? "" : "disconnected"}">
      <span class="avatar">${escapeHtml(player.name[0] || "?")}</span>
      <span class="player-meta"><strong>${escapeHtml(player.name)}</strong><small>${labels.join(" · ") || "Ready"}</small></span>
    </article>`;
  }).join("") : '<article class="player-card"><span class="avatar">…</span><span class="player-meta"><strong>Waiting for players</strong><small>Six seats available</small></span></article>';

  const canRoll = isMyTurn();
  elements.actions.innerHTML = actions.map((action) => `
    <button class="action-card ${state.modifiers.includes(action.id) ? "selected" : ""}" data-action="${action.id}" ${canRoll ? "" : "disabled"}>
      <span>${escapeHtml(action.detail)}</span><b>${escapeHtml(action.name)}</b><strong>${signed(action.value)}</strong>
    </button>`).join("");

  elements.board.classList.toggle("active", canRoll);
  elements.turnName.textContent = active ? `${active.name}'s turn${canRoll ? " — you are up" : ""}` : "Waiting for the table";
  elements.formula.textContent = `1d${state.sides}`;
  elements.rollFormula.textContent = `1d${state.sides}${modifierTotal() ? signed(modifierTotal()) : ""}`;
  elements.modifierTotal.textContent = signed(modifierTotal());

  const roll = state.lastRoll;
  const face = roll.dice?.length === 1 ? roll.dice[0] : roll.naturalTotal;
  elements.result.textContent = face ?? "—";
  elements.dieKind.textContent = `d${roll.sides || state.sides}`;
  elements.die.setAttribute("aria-label", `d${roll.sides || state.sides} result ${face ?? "unknown"}`);
  elements.total.textContent = roll.total ?? "—";
  elements.breakdown.textContent = roll.naturalTotal == null ? "No rolls yet" : `${roll.naturalTotal} ${signed(roll.modifierTotal ?? 0)}`;
  elements.rollButton.disabled = !canRoll;
  elements.nextTurn.hidden = demoMode || state.myPlayerId === 0 || state.myPlayerId !== state.hostPlayerId;
  drawDie();
}

function toggleAction(id) {
  if (!isMyTurn()) return;
  state.modifiers = state.modifiers.includes(id) ? state.modifiers.filter((item) => item !== id) : [...state.modifiers, id];
  render();
}

function animateResult() {
  elements.die.classList.remove("rolling");
  void elements.die.offsetWidth;
  elements.die.classList.add("rolling");
  animateDie();
}

function localRoll() {
  const naturalTotal = Math.floor(Math.random() * state.sides) + 1;
  state.lastRoll = { sides: state.sides, dice: [naturalTotal], naturalTotal, modifierTotal: modifierTotal(), total: naturalTotal + modifierTotal() };
  animateResult();
  render();
}

function storedIdentity() {
  try { return JSON.parse(localStorage.getItem(identityKey)); } catch { return null; }
}

function sendJoin() {
  const identity = storedIdentity();
  if (identity?.reconnectToken) {
    socket.send(JSON.stringify({ type: "player.join", name: identity.name, reconnectToken: identity.reconnectToken }));
  } else if (!elements.joinDialog.open) {
    elements.joinDialog.showModal();
    elements.playerName.focus();
  }
}

function handleMessage(message) {
  if (message.type === "server.hello") {
    sendJoin();
    return;
  }
  if (message.type === "player.welcome") {
    const name = elements.playerName.value.trim() || storedIdentity()?.name || "Player";
    state.myPlayerId = message.playerId;
    localStorage.setItem(identityKey, JSON.stringify({ playerId: message.playerId, reconnectToken: message.reconnectToken, name }));
    if (elements.joinDialog.open) elements.joinDialog.close();
    return;
  }
  if (message.type === "error") {
    if (message.code === "session-full") localStorage.removeItem(identityKey);
    showToast(message.message || "PocketDice rejected that request.");
    return;
  }
  if (typeof message.revision === "number" && message.revision <= state.revision) return;
  if (message.type === "session.snapshot") {
    const localModifiers = state.modifiers;
    Object.assign(state, message.session, { revision: message.revision, modifiers: localModifiers });
  } else if (message.type === "roll.result") {
    state.lastRoll = message.result;
    state.revision = message.revision;
    animateResult();
  }
  render();
}

function connect() {
  if (demoMode) return;
  clearTimeout(reconnectTimer);
  const protocol = location.protocol === "https:" ? "wss" : "ws";
  socket = new WebSocket(`${protocol}://${location.host}/ws`);
  socket.addEventListener("open", () => {
    elements.connection.classList.add("live");
    elements.connectionLabel.textContent = "Live on PocketDice";
  });
  socket.addEventListener("message", ({ data }) => {
    try { handleMessage(JSON.parse(data)); } catch { showToast("PocketDice sent an unreadable update."); }
  });
  socket.addEventListener("close", () => {
    elements.connection.classList.remove("live");
    elements.connectionLabel.textContent = "Reconnecting…";
    reconnectTimer = setTimeout(connect, 1500);
  });
  socket.addEventListener("error", () => socket.close());
}

function makeCommandId() {
  if (crypto.randomUUID) return crypto.randomUUID();
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}-${Math.random().toString(16).slice(2)}`;
}

function requestRoll() {
  if (demoMode) return localRoll();
  if (!socket || socket.readyState !== WebSocket.OPEN) return showToast("PocketDice is reconnecting.");
  socket.send(JSON.stringify({ type: "roll.request", commandId: makeCommandId(), formula: `1d${state.sides}`, modifierIds: state.modifiers }));
}

elements.joinForm.addEventListener("submit", (event) => {
  event.preventDefault();
  if (!socket || socket.readyState !== WebSocket.OPEN) return showToast("PocketDice is still connecting.");
  socket.send(JSON.stringify({ type: "player.join", name: elements.playerName.value.trim() }));
});
elements.joinDialog.addEventListener("cancel", (event) => event.preventDefault());
elements.nextTurn.addEventListener("click", () => socket?.send(JSON.stringify({ type: "turn.advance" })));

document.addEventListener("click", (event) => {
  const dieButton = event.target.closest("[data-sides]");
  if (dieButton) {
    state.sides = Number(dieButton.dataset.sides);
    document.querySelectorAll("[data-sides]").forEach((button) => button.classList.toggle("selected", button === dieButton));
    state.lastRoll = { sides: state.sides, dice: [], naturalTotal: null, modifierTotal: 0, total: null };
    render();
  }
  const actionButton = event.target.closest("[data-action]");
  if (actionButton) toggleAction(actionButton.dataset.action);
});

document.querySelector("#clearModifiers").addEventListener("click", () => { state.modifiers = []; render(); });
elements.rollButton.addEventListener("click", requestRoll);
elements.themeInput.addEventListener("change", async () => {
  const file = elements.themeInput.files?.[0];
  if (!file || file.size > 64 * 1024) return showToast("Theme JSON must be 64 KB or smaller.");
  try {
    const theme = JSON.parse(await file.text());
    for (const key of ["background", "panel", "accent", "text"]) document.documentElement.style.setProperty(`--${key === "background" ? "bg" : key}`, theme.colors[key]);
    drawDie();
  } catch { showToast("That theme JSON could not be loaded."); }
});

if (demoMode) {
  Object.assign(state, {
    players: ["Aria", "Bram", "Cora", "Dex", "Ember", "Finn"].map((name, index) => ({ id: index + 1, name, connected: true })),
    activePlayerId: 1, hostPlayerId: 1, myPlayerId: 1, revision: 1,
  });
  elements.connectionLabel.textContent = "Demo session";
}

render();
connect();
window.addEventListener("resize", () => drawDie());

