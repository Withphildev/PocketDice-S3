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
  elements.total.textContent = roll.total ?? "—";
  elements.breakdown.textContent = roll.naturalTotal == null ? "No rolls yet" : `${roll.naturalTotal} ${signed(roll.modifierTotal ?? 0)}`;
  elements.rollButton.disabled = !canRoll;
  elements.nextTurn.hidden = demoMode || state.myPlayerId === 0 || state.myPlayerId !== state.hostPlayerId;
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

