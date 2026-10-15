# PocketDice-S3

PocketDice-S3 turns an M5StickS3 into a pocket tabletop dice console and a local multiplayer hub. Up to six players join the device's Wi-Fi access point, open the WebUI, and share the same authoritative dice, action, and turn state.

This is an original project for the Pocket S3 family. It borrows product lessons—not source code or artwork—from other dice tools.

## Current milestone

- Hardware-independent C++ dice engine with `d4`, `d6`, `d8`, `d10`, `d12`, `d20`, `d100`, custom dice counts, and flat modifiers.
- Six-player turn/session model with revision numbers and duplicate-command rejection.
- M5StickS3 controls: blue face button rolls immediately; top M5 button plays a shake-roll animation.
- Shaded pseudo-3D dice on the StickS3 and responsive Canvas dice in the WebUI for every supported die type.
- Responsive WebUI prototype with an active-player highlight, action/power-up cards, combined totals, and dungeon-theme JSON import.
- Open `PocketDice-XXXX` SoftAP with captive-portal discovery and room for eight Wi-Fi stations.
- Realtime WebSocket table for six player seats, browser reconnect tokens, authoritative rolls, and host-controlled turns.
- Versioned JSON schemas for realtime messages and themes.
- Host-side tests that do not require the device.

The firmware and LittleFS WebUI both compile for the M5StickS3. Durable session persistence and validated theme uploads are the next device-integration milestone.

## Repository map

```text
core/PocketDiceCore/  Dice, modifier, and multiplayer session logic
firmware/             M5StickS3 firmware entry point
web/                  Device-hosted multiplayer WebUI
shared/               JSON protocol and theme schemas
tests/                Native host tests
docs/                 Architecture and delivery roadmap
tools/                Developer scripts
```

## Test the core

```bash
./tools/test-host.sh
```

## Build the firmware

Install [PlatformIO](https://platformio.org/), then run:

```bash
pio run
pio run -t buildfs
```

Upload the firmware and WebUI filesystem with:

```bash
pio run -t upload
pio run -t uploadfs
```

The partition map targets the M5StickS3's 8 MB flash and keeps application, WebUI, durable state, and uploaded theme assets separate.

After boot, join the SSID shown on the device (`PocketDice-XXXX`) and open `http://192.168.4.1/` if the captive portal does not appear automatically. The first player becomes the host and receives the **Next turn** control.

## Preview the WebUI

Serve the repository root (opening the page through HTTP allows the reconnect behavior to be exercised):

```bash
python3 -m http.server 8080
```

Then open `http://localhost:8080/web/?demo=1`. Demo mode exercises the dice and modifier interface without a device.

## Product rules

- The M5StickS3 is authoritative: browsers request a roll; the device generates and broadcasts its result.
- Only the active player may issue a normal roll. The host may change/skip turns.
- Every mutation carries a unique command ID; every broadcast carries a monotonically increasing revision.
- Reconnecting players reclaim their seat with an opaque token rather than creating a duplicate player.
- Uploaded themes are data only. Filenames, sizes, image dimensions, and schema are validated before an atomic save.

## License

MIT

