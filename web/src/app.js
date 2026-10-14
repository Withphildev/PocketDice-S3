const state = {
  revision: 1,
  players: [
    { id: 1, name: "Aria", connected: true },
    { id: 2, name: "Bram", connected: true },
    { id: 3, name: "Cora", connected: true },
    { id: 4, name: "Dex", connected: true },
    { id: 5, name: "Ember", connected: true },
    { id: 6, name: "Finn", connected: true },
  ],
  activePlayerId: 1,
  sides: 20,
  modifiers: [],
  result: 20,
};

let socket;

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
};

const modifierTotal = () => state.modifiers.reduce((sum, id) => sum + actions.find((item) => item.id === id).value, 0);
const signed = (value) => `${value >= 0 ? "+" : ""}${value}`;

function render() {
  const active = state.players.find((player) => player.id === state.activePlayerId);
  elements.players.innerHTML = state.players.map((player) => `
    <article class="player-card ${player.id === state.activePlayerId ? "active" : ""}">
      <span class="avatar">${player.name[0]}</span>
      <span class="player-meta"><strong>${player.name}</strong><small>${player.id === state.activePlayerId ? "Rolling now" : "Ready"}</small></span>
    </article>`).join("");
  elements.actions.innerHTML = actions.map((action) => `
    <button class="action-card ${state.modifiers.includes(action.id) ? "selected" : ""}" data-action="${action.id}">
      <span>${action.detail}</span><b>${action.name}</b><strong>${signed(action.value)}</strong>
    </button>`).join("");
  elements.board.classList.add("active");
  elements.turnName.textContent = `${active?.name ?? "Player"}'s turn`;
  elements.formula.textContent = `1d${state.sides}`;
  elements.rollFormula.textContent = `1d${state.sides}${modifierTotal() ? signed(modifierTotal()) : ""}`;
  elements.modifierTotal.textContent = signed(modifierTotal());
  elements.result.textContent = state.result;
  elements.total.textContent = state.result + modifierTotal();
  elements.breakdown.textContent = `${state.result} ${signed(modifierTotal())}`;
}

function toggleAction(id) {
  state.modifiers = state.modifiers.includes(id) ? state.modifiers.filter((item) => item !== id) : [...state.modifiers, id];
  render();
}

function localRoll() {
  state.result = Math.floor(Math.random() * state.sides) + 1;
  elements.die.classList.remove("rolling");
  void elements.die.offsetWidth;
  elements.die.classList.add("rolling");
  render();
}

function connect() {
  if (location.protocol === "file:") return;
  const protocol = location.protocol === "https:" ? "wss" : "ws";
  socket = new WebSocket(`${protocol}://${location.host}/ws`);
  socket.addEventListener("open", () => { elements.connection.classList.add("live"); elements.connectionLabel.textContent = "Live on PocketDice"; });
  socket.addEventListener("message", ({ data }) => {
    const message = JSON.parse(data);
    if (message.revision <= state.revision) return;
    if (message.type === "session.snapshot") Object.assign(state, message.session, { revision: message.revision });
    if (message.type === "roll.result") Object.assign(state, message.result, { revision: message.revision });
    render();
  });
  socket.addEventListener("close", () => { elements.connection.classList.remove("live"); elements.connectionLabel.textContent = "Reconnecting…"; setTimeout(connect, 1500); });
}

function requestRoll() {
  if (!socket || socket.readyState !== WebSocket.OPEN) return localRoll();
  socket.send(JSON.stringify({ type: "roll.request", commandId: crypto.randomUUID(), formula: `1d${state.sides}`, modifierIds: state.modifiers }));
}

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
document.querySelector("#rollButton").addEventListener("click", requestRoll);
elements.themeInput.addEventListener("change", async () => {
  const file = elements.themeInput.files?.[0];
  if (!file || file.size > 64 * 1024) return alert("Theme JSON must be 64 KB or smaller.");
  try {
    const theme = JSON.parse(await file.text());
    for (const key of ["background", "panel", "accent", "text"]) document.documentElement.style.setProperty(`--${key === "background" ? "bg" : key}`, theme.colors[key]);
  } catch { alert("That theme JSON could not be loaded."); }
});

render();
connect();

