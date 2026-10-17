# Roadmap

## 0.1 — Foundation (this baseline)

- Dice/formula/modifier core and host tests.
- Six-seat session and turn state.
- Face-button and M5-button rolling on-device.
- Multiplayer WebUI shell and shared schemas.

## 0.2 — Connected table (implemented)

- SoftAP with an explicit eight-station allowance and captive portal. ✓
- LittleFS-hosted WebUI and WebSocket endpoint. ✓
- Join/reconnect flow, host role, active-turn enforcement, and live broadcasts. ✓
- Browser/device integration tests for stale revisions and duplicate commands. Next hardware-validation task.

## 0.2.8 — Visual dice (implemented; device visual check pending)

- Original polyhedral d4, d6, d8, d10, d12, d20, and d100 meshes on the StickS3.
- Matching responsive Canvas renderer in the WebUI with back-face culling, perspective,
  fixed-light shading, highlights, table shadows, and committed-result animation.
- Every result deterministically selects and turns its corresponding mesh face toward the viewer.
- The WebUI d4 uses a three-axis tumble, the d10 uses a pointed pentagonal
  trapezohedron, and the d100 uses 100 planar, equal-area facets.
- Dungeon-theme accent colors drive the browser dice material.
- Die changes clear stale results so impossible faces are never shown.

## 0.3 — Dungeon kits

- Campaign action library: attacks, damage, healing, power-ups, and penalties.
- Staged theme uploads with schema, filename, file-size, and image validation.
- Per-dungeon theme selection plus export/import.
- Alternating checksummed state snapshots and recovery tests.

## 0.4 — Table polish

- Initiative editor and turn order controls.
- Physical shake detection as an optional input.
- Roll history and replay-safe audit trail.
- Sound cues driven by the same committed event.

