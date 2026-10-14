# Roadmap

## 0.1 — Foundation (this baseline)

- Dice/formula/modifier core and host tests.
- Six-seat session and turn state.
- Face-button and M5-button rolling on-device.
- Multiplayer WebUI shell and shared schemas.

## 0.2 — Connected table

- SoftAP with an explicit eight-station allowance and captive portal.
- LittleFS-hosted WebUI and WebSocket endpoint.
- Join/reconnect flow, host role, active-turn enforcement, and live broadcasts.
- Browser/device integration tests for stale revisions and duplicate commands.

## 0.3 — Dungeon kits

- Campaign action library: attacks, damage, healing, power-ups, and penalties.
- Staged theme uploads with schema, filename, file-size, and image validation.
- Per-dungeon theme selection plus export/import.
- Alternating checksummed state snapshots and recovery tests.

## 0.4 — Table polish

- Initiative editor and turn order controls.
- Physical shake detection as an optional input.
- Roll history and replay-safe audit trail.
- Device/browser animation and sound cues driven by the same committed event.

