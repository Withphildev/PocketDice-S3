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
let diceOrientation;

function themeColor(name, fallback) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || fallback;
}

function polygon(context, points, fill, stroke = "#ffffff55", width = 3) {
  context.beginPath();
  points.forEach(([x, y], index) => index ? context.lineTo(x, y) : context.moveTo(x, y));
  context.closePath();
  context.fillStyle = fill;
  context.fill();
  context.strokeStyle = stroke;
  context.lineWidth = width;
  context.stroke();
}

const phi = (1 + Math.sqrt(5)) / 2;
const icoVertices = [
  [-1, phi, 0], [1, phi, 0], [-1, -phi, 0], [1, -phi, 0],
  [0, -1, phi], [0, 1, phi], [0, -1, -phi], [0, 1, -phi],
  [phi, 0, -1], [phi, 0, 1], [-phi, 0, -1], [-phi, 0, 1],
];
const icoFaces = [
    [0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
    [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
    [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
    [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1],
];

const vectorLength = ([x, y, z]) => Math.hypot(x, y, z);
const normalize = (vector) => {
  const length = vectorLength(vector);
  return vector.map((component) => component / length);
};
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];

function makeDodecahedron() {
  const vertices = icoFaces.map((face) => normalize(face.reduce((sum, vertexIndex) =>
    sum.map((value, axis) => value + icoVertices[vertexIndex][axis]), [0, 0, 0])));
  const faces = icoVertices.map((axis, vertexIndex) => {
    const normal = normalize(axis);
    const reference = normalize(cross(normal, Math.abs(normal[1]) < .9 ? [0, 1, 0] : [1, 0, 0]));
    const tangent = cross(normal, reference);
    return icoFaces.map((face, faceIndex) => face.includes(vertexIndex) ? faceIndex : -1)
      .filter((faceIndex) => faceIndex >= 0)
      .sort((a, b) => Math.atan2(dot(vertices[a], tangent), dot(vertices[a], reference)) -
                      Math.atan2(dot(vertices[b], tangent), dot(vertices[b], reference)));
  });
  return { vertices, faces, radius: 1 };
}

function makeD100() {
  const segments = 10;
  const ringCount = 9;
  const vertices = [[0, 1, 0]];
  for (let ring = 0; ring < ringCount; ring++) {
    const latitude = Math.PI * (ring + 1) / (ringCount + 1);
    const ringRadius = Math.sin(latitude);
    const y = Math.cos(latitude);
    const phase = ring % 2 ? Math.PI / segments : 0;
    for (let segment = 0; segment < segments; segment++) {
      const longitude = phase + segment * Math.PI * 2 / segments;
      vertices.push([Math.cos(longitude) * ringRadius, y, Math.sin(longitude) * ringRadius]);
    }
  }
  const bottom = vertices.push([0, -1, 0]) - 1;
  const faces = [];
  for (let segment = 0; segment < segments; segment++) {
    faces.push([0, 1 + segment, 1 + (segment + 1) % segments]);
  }
  for (let ring = 0; ring < ringCount - 1; ring++) {
    const current = 1 + ring * segments;
    const next = current + segments;
    for (let segment = 0; segment < segments; segment++) {
      faces.push([current + segment, next + segment, next + (segment + 1) % segments, current + (segment + 1) % segments]);
    }
  }
  const lastRing = 1 + (ringCount - 1) * segments;
  for (let segment = 0; segment < segments; segment++) {
    faces.push([bottom, lastRing + (segment + 1) % segments, lastRing + segment]);
  }
  return { vertices, faces, radius: 1 };
}

function makeD10() {
  const sourceVertices = [];
  const height = .62;
  for (let index = 0; index < 5; index++) {
    const angle = index * Math.PI * 2 / 5;
    sourceVertices.push([Math.cos(angle), height, Math.sin(angle)]);
  }
  for (let index = 0; index < 5; index++) {
    const angle = Math.PI / 5 + index * Math.PI * 2 / 5;
    sourceVertices.push([Math.cos(angle), -height, Math.sin(angle)]);
  }
  const sourceFaces = [[0, 1, 2, 3, 4], [9, 8, 7, 6, 5]];
  for (let index = 0; index < 5; index++) {
    sourceFaces.push([index, 5 + index, (index + 1) % 5]);
    sourceFaces.push([index, 5 + (index + 4) % 5, 5 + index]);
  }
  const vertices = sourceFaces.map((face) => normalize(face.reduce((sum, vertexIndex) =>
    sum.map((value, axis) => value + sourceVertices[vertexIndex][axis]), [0, 0, 0])));
  const faces = sourceVertices.map((axis, vertexIndex) => {
    const normal = normalize(axis);
    const reference = normalize(cross(normal, Math.abs(normal[1]) < .9 ? [0, 1, 0] : [1, 0, 0]));
    const tangent = cross(normal, reference);
    return sourceFaces.map((face, faceIndex) => face.includes(vertexIndex) ? faceIndex : -1)
      .filter((faceIndex) => faceIndex >= 0)
      .sort((a, b) => Math.atan2(dot(vertices[a], tangent), dot(vertices[a], reference)) -
                      Math.atan2(dot(vertices[b], tangent), dot(vertices[b], reference)));
  });
  return { vertices, faces, radius: 1 };
}

const dieMeshes = {
  4: {
    vertices: [[1, 1, 1], [-1, -1, 1], [-1, 1, -1], [1, -1, -1]],
    faces: [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]], radius: Math.sqrt(3),
  },
  6: {
    vertices: [[-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1], [-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]],
    faces: [[0, 1, 2, 3], [4, 7, 6, 5], [0, 4, 5, 1], [1, 5, 6, 2], [2, 6, 7, 3], [4, 0, 3, 7]], radius: Math.sqrt(3),
  },
  8: {
    vertices: [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]],
    faces: [[2, 0, 4], [2, 4, 1], [2, 1, 5], [2, 5, 0], [3, 4, 0], [3, 1, 4], [3, 5, 1], [3, 0, 5]], radius: 1,
  },
  10: makeD10(),
  12: makeDodecahedron(),
  20: { vertices: icoVertices, faces: icoFaces, radius: Math.sqrt(1 + phi * phi) },
  100: makeD100(),
};

const add = (a, b) => a.map((value, index) => value + b[index]);
const subtract = (a, b) => a.map((value, index) => value - b[index]);
const multiply = (vector, scalar) => vector.map((value) => value * scalar);

function faceNormal(mesh, face) {
  const normal = normalize(cross(subtract(mesh.vertices[face[1]], mesh.vertices[face[0]]), subtract(mesh.vertices[face[2]], mesh.vertices[face[0]])));
  const center = multiply(face.reduce((sum, index) => add(sum, mesh.vertices[index]), [0, 0, 0]), 1 / face.length);
  return dot(normal, center) < 0 ? multiply(normal, -1) : normal;
}

function quatNormalize([x, y, z, w]) {
  const length = Math.hypot(x, y, z, w);
  return [x / length, y / length, z / length, w / length];
}

function quatMultiply([ax, ay, az, aw], [bx, by, bz, bw]) {
  return [
    aw * bx + ax * bw + ay * bz - az * by,
    aw * by - ax * bz + ay * bw + az * bx,
    aw * bz + ax * by - ay * bx + az * bw,
    aw * bw - ax * bx - ay * by - az * bz,
  ];
}

function quatAxisAngle(axis, angle) {
  const half = angle / 2;
  return [...multiply(normalize(axis), Math.sin(half)), Math.cos(half)];
}

function quatBetween(from, to) {
  const a = normalize(from);
  const b = normalize(to);
  const cosine = dot(a, b);
  if (cosine < -.9999) return quatAxisAngle(Math.abs(a[0]) < .8 ? cross(a, [1, 0, 0]) : cross(a, [0, 1, 0]), Math.PI);
  return quatNormalize([...cross(a, b), 1 + cosine]);
}

function rotateByQuat(vector, [qx, qy, qz, qw]) {
  const q = [qx, qy, qz];
  const t = multiply(cross(q, vector), 2);
  return add(vector, add(multiply(t, qw), cross(q, t)));
}

function slerpQuat(from, to, amount) {
  let cosine = dot(from, to);
  let target = to;
  if (cosine < 0) {
    cosine = -cosine;
    target = multiply(to, -1);
  }
  if (cosine > .9995) return quatNormalize(from.map((value, index) => value + amount * (target[index] - value)));
  const angle = Math.acos(Math.max(-1, Math.min(1, cosine)));
  const denominator = Math.sin(angle);
  const a = Math.sin((1 - amount) * angle) / denominator;
  const b = Math.sin(amount * angle) / denominator;
  return from.map((value, index) => value * a + target[index] * b);
}

function tumbleOrientation(turns) {
  return quatNormalize(quatMultiply(quatAxisAngle([0.72, 1, .24], turns * 1.13), quatAxisAngle([1, -.18, .6], turns * .77)));
}

function restingOrientation(mesh, value) {
  const result = Math.max(1, Number(value) || 1);
  const face = mesh.faces[(result - 1) % mesh.faces.length];
  const target = normalize([-.28, -.3, .91]);
  const align = quatBetween(faceNormal(mesh, face), target);
  const twist = quatAxisAngle(target, ((result * 137.508) % 360) * Math.PI / 180);
  return quatNormalize(quatMultiply(twist, align));
}

function colorChannels(color) {
  const context = document.createElement("canvas").getContext("2d");
  context.fillStyle = color;
  context.fillRect(0, 0, 1, 1);
  return [...context.getImageData(0, 0, 1, 1).data.slice(0, 3)];
}

function shadeColor(channels, intensity) {
  const adjusted = channels.map((value) => Math.round(Math.max(0, Math.min(255, value * intensity))));
  return `rgb(${adjusted.join(",")})`;
}

function drawTableShadow(context, cx, cy, radius) {
  context.save();
  context.translate(cx + radius * .14, cy + radius * .76);
  context.scale(1, .29);
  const shadow = context.createRadialGradient(0, 0, radius * .12, 0, 0, radius * .83);
  shadow.addColorStop(0, "rgba(0,0,0,.48)");
  shadow.addColorStop(.55, "rgba(0,0,0,.24)");
  shadow.addColorStop(1, "rgba(0,0,0,0)");
  context.fillStyle = shadow;
  context.beginPath();
  context.arc(0, 0, radius, 0, Math.PI * 2);
  context.fill();
  context.restore();
}

function drawMesh(context, cx, cy, radius, orientation, mesh, accent, result) {
  const camera = [0, 0, 3.7];
  const lightDirection = normalize([-.65, -.82, 1.35]);
  const base = colorChannels(accent);
  const transformed = mesh.vertices.map((vertex) => multiply(rotateByQuat(vertex, orientation), 1 / mesh.radius));
  const projected = transformed.map(([x, y, z]) => {
    const perspective = camera[2] / (camera[2] - z);
    return [cx + x * radius * perspective, cy + y * radius * perspective, z];
  });
  const selectedFace = Math.max(0, ((Number(result) || 1) - 1) % mesh.faces.length);
  const visible = [];
  mesh.faces.forEach((face, faceIndex) => {
    let normal = normalize(cross(subtract(transformed[face[1]], transformed[face[0]]), subtract(transformed[face[2]], transformed[face[0]])));
    const center = multiply(face.reduce((sum, index) => add(sum, transformed[index]), [0, 0, 0]), 1 / face.length);
    if (dot(normal, center) < 0) normal = multiply(normal, -1);
    const view = normalize(subtract(camera, center));
    if (dot(normal, view) <= .015) return;
    const diffuse = Math.max(0, dot(normal, lightDirection));
    const halfway = normalize(add(lightDirection, view));
    const specular = Math.pow(Math.max(0, dot(normal, halfway)), 28);
    visible.push({ face, faceIndex, normal, diffuse, specular, depth: center[2] });
  });
  const lineWidth = mesh.faces.length >= 80 ? .7 : mesh.faces.length >= 20 ? 1.35 : 2;
  visible.sort((a, b) => a.depth - b.depth).forEach(({ face, faceIndex, diffuse, specular }) => {
    const intensity = .36 + diffuse * .68 + specular * .42 + (faceIndex === selectedFace ? .09 : 0);
    const points = face.map((index) => projected[index]);
    const boundsX = points.map((point) => point[0]);
    const boundsY = points.map((point) => point[1]);
    const gradient = context.createLinearGradient(Math.min(...boundsX), Math.min(...boundsY), Math.max(...boundsX), Math.max(...boundsY));
    gradient.addColorStop(0, shadeColor(base, intensity + .13));
    gradient.addColorStop(1, shadeColor(base, Math.max(.2, intensity - .11)));
    polygon(context, points, gradient, faceIndex === selectedFace ? "rgba(255,255,255,.78)" : "rgba(224,218,255,.34)", lineWidth + (faceIndex === selectedFace ? .8 : 0));
  });

  context.save();
  context.beginPath();
  visible.forEach(({ face }) => {
    face.map((index) => projected[index]).forEach(([x, y], index) => index ? context.lineTo(x, y) : context.moveTo(x, y));
    context.closePath();
  });
  context.clip();
  context.globalCompositeOperation = "screen";
  const gloss = context.createRadialGradient(cx - radius * .38, cy - radius * .48, 0, cx - radius * .38, cy - radius * .48, radius * .58);
  gloss.addColorStop(0, "rgba(255,255,255,.2)");
  gloss.addColorStop(1, "rgba(255,255,255,0)");
  context.fillStyle = gloss;
  context.beginPath();
  context.arc(cx, cy, radius * 1.08, 0, Math.PI * 2);
  context.fill();
  context.restore();
}

function currentRestingOrientation() {
  const sides = state.lastRoll?.sides || state.sides;
  const mesh = dieMeshes[sides] || dieMeshes[10];
  const result = state.lastRoll?.dice?.[0] ?? state.lastRoll?.naturalTotal ?? 1;
  return restingOrientation(mesh, result);
}

function drawDie(orientation = diceOrientation || currentRestingOrientation()) {
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
  const sides = state.lastRoll?.sides || state.sides;
  const mesh = dieMeshes[sides] || dieMeshes[10];
  const result = state.lastRoll?.dice?.[0] ?? state.lastRoll?.naturalTotal ?? 1;
  drawTableShadow(context, cx, cy, radius);
  drawMesh(context, cx, cy, radius, orientation, mesh, accent, result);
}

function animateDie() {
  cancelAnimationFrame(animationFrame);
  const started = performance.now();
  const duration = 720;
  const start = tumbleOrientation((started % 10000) / 360);
  const target = currentRestingOrientation();
  const frame = (now) => {
    const progress = Math.min(1, (now - started) / duration);
    const turns = progress * (Math.PI * 5.5 + (state.lastRoll?.naturalTotal || 1) * .07);
    const tumbling = quatMultiply(tumbleOrientation(turns), start);
    const settleProgress = Math.max(0, (progress - .58) / .42);
    const eased = 1 - Math.pow(1 - settleProgress, 3);
    diceOrientation = slerpQuat(tumbling, target, eased);
    drawDie(diceOrientation);
    if (progress < 1) animationFrame = requestAnimationFrame(frame);
    else diceOrientation = target;
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
  diceOrientation = currentRestingOrientation();
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
    diceOrientation = currentRestingOrientation();
  } else if (message.type === "roll.result") {
    state.lastRoll = message.result;
    state.revision = message.revision;
    diceOrientation = currentRestingOrientation();
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
    diceOrientation = undefined;
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

