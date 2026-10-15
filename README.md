# SGP MESHTASTIC

[![Hardware & store](https://img.shields.io/badge/Hardware%20%26%20store-sgpstore.net-39ff9e?style=for-the-badge)](https://www.sgpstore.net)
[![License](https://img.shields.io/badge/License-GPL--3.0-27d1ff?style=for-the-badge)](LICENSE)
[![Board](https://img.shields.io/badge/Board-ESP32--S3%20%2B%20SX1262-ffb43a?style=for-the-badge)](https://www.sgpstore.net)

> **Turn an ESP32‑S3 card into a real [Meshtastic](https://meshtastic.org) LoRa mesh node** — chat, live node list, regions & presets, AES channels, the official Meshtastic phone app over BLE, image‑over‑LoRa and an SOS beacon, all from a phone.

> 🛒 **Get the hardware:** the **SGP Card Mini** (ESP32‑S3 + SX1262) and other RF/maker boards are at **[sgpstore.net](https://www.sgpstore.net)**.

A **Meshtastic-compatible** firmware for the **SGP Card Mini** (ESP32-S3 + NiceRF
LoRa1262 / Semtech SX1262). It turns the card into a real node on a Meshtastic
LoRa mesh: send/receive text, show up in the node list with a name, relay
packets (repeater), change region / modem preset / channel / PSK / frequency,
and scan the band — all from your phone over WiFi.

> Separate, standalone project. It **does not touch** `SGP_HACKER12`, `Lora`,
> `SGPCard_Mini` or any other sketch.

---

## What it does

- **Chat** — broadcast text messages on the mesh; incoming messages from other
  Meshtastic nodes appear with sender name, RSSI and hop count.
- **Channels** — the first (primary) channel sets the frequency slot. Join the
  public mesh with primary `LongFast` + the default key, or add private channels
  with your own passphrase (AES-128 default key, or AES-256 from a passphrase).
- **Nodes** — live list of every node heard, with name, short name, hops,
  RSSI/SNR and "last heard". We also broadcast our own NodeInfo so we appear in
  other people's node lists.
- **Radio** — pick region (US, EU_868, EU_433, ANZ, CN, JP, IN, …) and modem
  preset (LongFast, MediumFast, ShortFast, LongSlow, ShortTurbo, …), set TX
  power, RX boost, repeater/relay mode, or force an exact frequency.
- **Scan** — RSSI sweep across the current region band, with the operating
  frequency marked.
- **Image over LoRa** (📷 IMAGE tab) — send a very low-res picture across the
  mesh, split into many small LoRa packets (like a slow-scan / the old
  send-file-over-sound trick). Pick an image, it is shrunk (32/48/64 px) and
  reduced to 4-bit grayscale or 1-bit B/W so it fits in a handful of packets,
  then streamed one packet per chunk. The receiving node switches to
  **receive mode automatically** when the header packet arrives and rebuilds
  the picture live (missing pieces show as dim blocks until they land).
- **Super Listen (monitor)** — intercepts Meshtastic traffic and tries to
  decrypt every packet with all known keys (the public LongFast keys + your
  channels). *Default slot* stays on the main frequency (catches nearly all
  normal traffic); *Hop all slots* sweeps every channel slot in the region to
  discover other meshes (one radio listens to one frequency at a time, so it
  hops). Live intercept log with freq, from→to, hops, RSSI/SNR, decrypted text
  or `[encrypted]`.
- **Settings** — Wi-Fi network (change the AP name + optional password, saved
  permanently), buzzer+LED feedback toggle, node name, diagnostics, re-init.

**Feedback**: the buzzer chirps and the LED blinks **cyan when you send** a
message and **green when one arrives** (toggle off in Settings).

**Responsive**: mobile-first, and on a computer/Mac it widens to a centered
frame with two-column layouts (and the chat as a centered column).

## How it joins a real Meshtastic mesh

To talk to other Meshtastic devices, **all nodes must match**:

1. **Region** (e.g. `EU_868` in Europe, `US` in North America)
2. **Modem preset** (e.g. `LongFast` — the global default)
3. **Primary channel name + key** (e.g. `LongFast` + the public default key)

Set those three to the same values as the mesh you want to join and you're on.

### Protocol (verified byte-for-byte against the official Meshtastic library)

- **Channel hash** = `xorHash(name) ^ xorHash(key)` → `LongFast` = `0x08`
- **Frequency slot** = `fStart + bw/2000 + ch·(bw/1000)`, `ch = djb2(name) % N`
  → EU_868/LongFast = **869.525 MHz**, US/LongFast = **906.875 MHz**
- **Sync word** `0x2B`, preamble 16, CRC on
- **16-byte header**: `to, from, id` (LE) · `flags` · `channelHash` · `nextHop` · `relayNode`
  (flags = hop_limit[0-2] | want_ack[3] | via_mqtt[4] | hop_start[5-7])
- **Encryption**: AES-CTR, nonce = `packetId(8 LE) + fromNode(4 LE) + 0000`
- **Payload**: protobuf `Data` → `08<portnum> 12<len><payload>`
  (text = portnum 1, node info = portnum 4)

## Hardware (SGP Card Mini PCB v3.7, schematic JLCPCB-002)

| Signal | Pin | | Signal | Pin |
|---|---|---|---|---|
| SPI SCK | 36 | | SX1262 NSS | 38 |
| SPI MISO | 33 | | SX1262 RST | 47 |
| SPI MOSI | 35 | | SX1262 DIO1 | 48 |
| RGB WS2812 | 5 | | SX1262 BUSY | 26 |
| Buzzer | 1 | | 3V3 rail CE | 6 (HIGH) |

CC1101 CS (34) and SD CS (10) are held HIGH so they don't disturb the shared SPI
bus. DIO2 is the internal RF switch, DIO3 the internal TCXO (auto-probed at boot).

## Build & flash

- **Board**: ESP32-S3
- **Partition Scheme**: `Huge APP (3MB No OTA/1MB SPIFFS)`
- **Flash Size**: 8MB
- **Libraries**: RadioLib, Adafruit NeoPixel, **NimBLE-Arduino** (for the app)

The protobuf layer (`*.pb.*`, `pb_*`) ships inside this folder, so just open the
sketch in the Arduino IDE and compile — no extra include paths needed.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,FlashSize=8M .
arduino-cli upload  --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,FlashSize=8M -p /dev/cu.usbmodemXXXX .
```

Firmware size: ~1.3 MB (41% flash), 24% RAM (web UI + LoRa + super listen + BLE app).

## Use

1. Power the card. It opens a WiFi AP **`SGP_MESH_xxxx`** (open).
2. Join it with your phone → browse to **http://192.168.4.1**.
3. In **Settings** give yourself a name. In **Radio** pick your region/preset.
   In **Channels** make sure the primary is the mesh you want (e.g. `LongFast`).
4. Chat. Other Meshtastic nodes in range will see you and vice-versa.

## Works with the official Meshtastic app (BLE)

This firmware also exposes the **Meshtastic BLE Client API**, so the official
Meshtastic app (Android / iOS / desktop) can connect to the card over Bluetooth
and use it like any Meshtastic node — see it, read its config, and send/receive
messages.

- GATT service `6ba1b218-15a8-461f-9fa8-5dcae273eafd` with the standard
  ToRadio / FromRadio / FromNum characteristics.
- Pairing mode **NO_PIN** (just-works) — no passkey needed on a headless card.
- On connect it runs the real config handshake (`MyNodeInfo`, `DeviceMetadata`,
  our `NodeInfo`, the channels, the `Config` blocks incl. LoRa region/preset,
  then `config_complete`) using the **official nanopb-generated protobufs**
  (`*.pb.h` / `*.pb.cpp` in this folder + the `pb_*` nanopb runtime), so the
  wire format matches the real firmware byte-for-byte.
- Messages you send in the app are transmitted over LoRa; messages heard over
  LoRa (that we can decrypt) are forwarded up to the app.

**To connect**: open the Meshtastic app → add device → Bluetooth → pick
**"SGP Mesh xxxx"**. BLE and the WiFi web UI run at the same time.

> The protobuf handshake was verified against the official `meshtastic` Python
> library (all messages valid + round-trip). The live BLE connection still needs
> testing on the physical card with the app.

## Files

- `SGP_Meshtastic.ino` — radio driver, Meshtastic LoRa protocol, web server
- `mtble.ino` — Meshtastic BLE Client API (phone-app support)
- `page.h` — the web UI (one PROGMEM HTML string; kept in a `.h` so Arduino's
  ctags prototype generator doesn't choke on the embedded JavaScript)
- `*.pb.h` / `*.pb.cpp` — official nanopb-generated Meshtastic protobufs
- `pb.h`, `pb_common.*`, `pb_encode.*`, `pb_decode.*` — nanopb 0.4.9.1 runtime

## Notes & limitations

- This is a lightweight client/repeater: it implements text + node-info over the
  standard channel (PSK) encryption and basic flood relaying. It does **not**
  implement PKI direct messages (Curve25519), position/telemetry modules, MQTT,
  admin, or store-and-forward.
- Meshtastic occasionally tweaks wire details between major versions; if a future
  firmware changes the header or default channel, update the constants at the top
  of `SGP_Meshtastic.ino`.
- **Status: compiles OK, UI verified in-browser with a mocked backend. Pending:
  flash on the real card and test against a live Meshtastic device.**

## 🆘 Super SOS (emergency beacon)

A distress mode (SOS tab) that broadcasts **SOS** across **every frequency slot
and every modem preset** of the current region, on the public Meshtastic
channel so any node can hear it. Options:

- Add an optional **emergency message**.
- Add your **current location** (from the phone/browser GPS) — included as
  coordinates + a Google Maps link.
- **Infinite transmission** — it keeps sweeping and sending until you press stop
  or power the card off.
- LED flashes **red** and the buzzer chirps on each burst.

Use only in a real emergency. It transmits within the region's band (set the
correct region first).

---

## Licensing

This project is **GPL‑3.0** (see [`LICENSE`](LICENSE)) because it includes the
Meshtastic protobuf sources and interoperates with the Meshtastic protocol.
Full third‑party attribution — Meshtastic, nanopb, RadioLib, NimBLE‑Arduino,
Adafruit NeoPixel — is in [`NOTICE.md`](NOTICE.md).

> *Meshtastic® is a registered trademark of Meshtastic LLC. This is an
> independent, unofficial firmware, **not affiliated with or endorsed by
> Meshtastic LLC**.*

---

<div align="center">

### Built by **SGP** · [**sgpstore.net**](https://www.sgpstore.net)

The **SGP Card Mini** (ESP32‑S3 + SX1262) that runs this firmware — and other
maker and RF gadgets — are available at **[sgpstore.net](https://www.sgpstore.net)**.

[![Shop the hardware](https://img.shields.io/badge/Shop%20the%20hardware-sgpstore.net-39ff9e?style=for-the-badge)](https://www.sgpstore.net)

</div>
