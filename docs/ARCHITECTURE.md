# PocketDice architecture

## Runtime shape

The M5StickS3 owns the game session. Browsers are synchronized views and input surfaces; they never decide a roll result or advance the authoritative revision.

```text
Blue face button ─┐
Top M5 button ────┼──> command queue ──> SessionState ──> revisioned event
WebSocket clients ┘                          │                    │
                                             └── durable state    └── broadcast to 1–6 players
```

HTTP serves the WebUI, current session snapshot, and upload endpoints. WebSockets carry small realtime commands and events. This avoids repeated full-state polling while keeping reconnect straightforward: fetch or receive one snapshot, then apply only events with a newer revision.

## Ownership boundaries

- `core/PocketDiceCore`: no Arduino, Wi-Fi, display, filesystem, or JSON dependencies. It is deterministic under an injected random source and testable on a desktop.
- `firmware`: device buttons, motion, display, access point, captive portal, WebSockets, serialization, persistence, and theme storage.
- `web`: presentation and player input. It can animate a committed result but cannot substitute a locally generated result for a device result.
- `shared`: contracts used by firmware and browser integration tests.

## Multiplayer protocol

1. A player joins the `PocketDice-XXXX` AP and the captive portal opens `/`.
2. `player.join` returns a numeric player ID and opaque reconnect token. The token is stored only in that browser and as a hash on-device.
3. The server sends `session.snapshot` with revision `N`.
4. The active player sends `roll.request` with a UUID command ID, formula, and selected modifier IDs.
5. The server validates turn ownership, command uniqueness, formula bounds, and referenced actions.
6. The device rolls, commits revision `N+1`, and broadcasts one `roll.result` to every client.
7. A reconnect presents its token and receives the newest snapshot. Events at or below the browser's revision are ignored.

The access point should explicitly request eight stations: six players plus headroom for the host and development. Session seats remain capped at six.

## Physical controls

- Blue face button / `BtnA`: roll the current formula immediately.
- Top M5 button / `BtnB`: run the shake animation, then roll the current formula.
- A future long press on the M5 button may open connection/session information; short-press behavior remains unchanged.

Physical rolls are attributed to the active player and broadcast exactly like WebUI rolls.

## Persistence

NVS stores small settings such as active dungeon and AP preferences. A dedicated state partition stores two alternating, checksummed session snapshots; the newest valid generation wins after restart. Theme assets live in a separate partition so a bad/large upload cannot consume session-state space.

Uploads use a staged name, enforce a 64 KB JSON limit and a configurable image limit, validate schema/dimensions, then rename atomically. The previous theme remains usable until the replacement commits.

## Theme package

Milestone one accepts a theme JSON containing a stable ID, display name, four required colors, and an optional safe asset filename. Later theme bundles may include a constrained background image and sound set. HTML, JavaScript, SVG scripts, external URLs, and executable content are not accepted as skin data.

