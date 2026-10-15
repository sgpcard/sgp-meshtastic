/*
 * ═══════════════════════════════════════════════════════════════════════════
 *   SGP MESHTASTIC  —  Meshtastic-compatible node for the SGP Card Mini
 *   ESP32-S3 + NiceRF LoRa1262 (Semtech SX1262)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *   A standalone firmware that lets the SGP Card Mini join a real Meshtastic
 *   LoRa mesh: send and receive text messages, appear in the node list with a
 *   name, relay packets (repeater), change region / modem preset / channel /
 *   PSK, scan the band and fine-tune the radio — all from a phone over WiFi.
 *
 *   Protocol verified byte-for-byte against the official Meshtastic library:
 *     · channel hash  = xorHash(name) ^ xorHash(key)       (LongFast = 0x08)
 *     · frequency slot = fStart + bw/2000 + ch*(bw/1000), ch = djb2(name)%N
 *     · AES-128-CTR, nonce = packetId(8 LE) + fromNode(4 LE) + 0000
 *     · 16-byte header: to,from,id (LE) · flags · channelHash · nextHop · relay
 *     · protobuf Data: 08<portnum> 12<len><payload>
 *
 *   DOES NOT touch SGP_HACKER12 / Lora / SGPCard_Mini — fully separate project.
 *
 *   ── Hardware (SGP Card Mini PCB v3.7, schematic JLCPCB-002) ──
 *     SPI   SCK=36  MISO=33  MOSI=35
 *     SX1262  NSS=38  RST=47  DIO1=48  BUSY=26   (DIO2=RF switch, DIO3=TCXO int.)
 *     CC1101 CS=34 and SD CS=10 held HIGH (share the SPI bus)
 *     RGB WS2812=5   Buzzer=1   Sensor rail CE=6 (HIGH = power the 3V3 rail)
 *
 *   Requires libraries: RadioLib, Adafruit_NeoPixel.
 *   Build: ESP32-S3, Partition "Huge APP (3MB)", Flash 8MB.
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <RadioLib.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "esp_random.h"

// ── Meshtastic BLE Client API (official app support) ──
#include <NimBLEDevice.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "mesh.pb.h"

#define FW_VERSION "SGP MESHTASTIC v1.0"

// Web page (big PROGMEM HTML string). Kept in a .h so Arduino's ctags-based
// prototype generator does not scan its embedded JavaScript.
#include "page.h"

// BLE bridge (implemented in mtble.ino, concatenated after this file)
void bleSetup();
void bleLoop();
void ble_forwardPacket(uint32_t from, uint32_t to, uint8_t chIdx, uint8_t port,
                       const uint8_t* payload, int plen, int rssi, float snr, uint8_t hops, uint32_t id);
extern bool bleConnected;

// ───────────────────────── PINS ─────────────────────────
#define PIN_MOSI       35
#define PIN_MISO       33
#define PIN_SCK        36
#define PIN_LORA_CS    38
#define PIN_LORA_RST   47
#define PIN_LORA_DIO1  48
#define PIN_LORA_BUSY  26
#define PIN_CC1101_CS  34
#define PIN_SD_CS      10
#define PIN_RGB_LED     5
#define PIN_BUZZER      1
#define PIN_SENSOR_CE   6

#define RGB_BRIGHTNESS 150

// TCXO reference voltages probed at boot (the NiceRF module uses ~1.8 V)
const float TCXO_OPTIONS[] = { 1.8f, 3.3f, 0.0f, 1.6f, 2.4f, 3.0f };

// ───────────────────────── MESHTASTIC CONSTANTS ─────────────────────────
#define MESH_SYNC_WORD    0x2B   // Meshtastic LoRa sync word
#define MESH_PREAMBLE     16
#define BROADCAST_ADDR    0xFFFFFFFFUL
#define MAX_LORA_PAYLOAD  255
#define HEADER_LEN        16

// Packet-header flag layout (byte 12 of the header)
#define FLAG_HOP_LIMIT_MASK   0x07
#define FLAG_WANT_ACK         0x08
#define FLAG_VIA_MQTT         0x10
#define FLAG_HOP_START_SHIFT  5

// PortNums we care about
#define PORT_TEXT      1
#define PORT_POSITION  3
#define PORT_NODEINFO  4
#define PORT_ROUTING   5
#define PORT_TELEMETRY 67
#define PORT_IMG       256   // our own "image over LoRa" private app port

// Default PSK for the "LongFast" / primary channel (AES-128)
// base64: 1PG7OiApB1nwvP+rz05pAQ==
const uint8_t DEFAULT_PSK[16] = {
  0xd4,0xf1,0xbb,0x3a,0x20,0x29,0x07,0x59,0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69,0x01
};

// ── 16-byte Meshtastic packet header ──
struct __attribute__((packed)) MeshHeader {
  uint32_t to;
  uint32_t from;
  uint32_t id;
  uint8_t  flags;
  uint8_t  channel;    // channel hash
  uint8_t  nextHop;
  uint8_t  relayNode;
};

// ───────────────────────── REGIONS ─────────────────────────
// freqStart, freqEnd (MHz), spacing (MHz), powerLimit (dBm)
struct Region { const char* name; float fStart; float fEnd; float spacing; uint8_t power; };
const Region REGIONS[] = {
  { "US",      902.0f, 928.0f,   0.0f, 30 },
  { "EU_868",  869.4f, 869.65f,  0.0f, 27 },
  { "EU_433",  433.0f, 434.0f,   0.0f, 10 },
  { "ANZ",     915.0f, 928.0f,   0.0f, 30 },
  { "CN",      470.0f, 510.0f,   0.0f, 19 },
  { "JP",      920.5f, 923.5f,   0.0f, 13 },
  { "KR",      920.0f, 923.0f,   0.0f, 23 },
  { "TW",      920.0f, 925.0f,   0.0f, 27 },
  { "RU",      868.7f, 869.2f,   0.0f, 20 },
  { "IN",      865.0f, 867.0f,   0.0f, 30 },
  { "NZ_865",  864.0f, 868.0f,   0.0f, 36 },
  { "TH",      920.0f, 925.0f,   0.0f, 27 },
  { "UA_868",  868.0f, 868.6f,   0.0f, 14 },
  { "MY_919",  919.0f, 924.0f,   0.0f, 27 },
  { "SG_923",  917.0f, 925.0f,   0.0f, 20 },
  { "BR_902",  902.0f, 907.5f,   0.0f, 30 },
};
const int REGION_COUNT = sizeof(REGIONS) / sizeof(REGIONS[0]);

// ───────────────────────── MODEM PRESETS ─────────────────────────
// name, bandwidth (kHz), spreading factor, coding rate
struct Preset { const char* name; float bw; uint8_t sf; uint8_t cr; };
const Preset PRESETS[] = {
  { "LongFast",   250.0f, 11, 5 },   // default
  { "LongSlow",   125.0f, 12, 8 },
  { "LongMod",    125.0f, 11, 8 },
  { "MediumFast", 250.0f,  9, 5 },
  { "MediumSlow", 250.0f, 10, 5 },
  { "ShortFast",  250.0f,  7, 5 },
  { "ShortSlow",  250.0f,  8, 5 },
  { "ShortTurbo", 500.0f,  7, 5 },
  { "LongTurbo",  500.0f, 11, 8 },
};
const int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

// ───────────────────────── OBJECTS ─────────────────────────
SX1262 radio = new Module(PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);
WebServer server(80);
DNSServer dnsServer;
Adafruit_NeoPixel rgbLED(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);
Preferences prefs;

// ───────────────────────── RADIO STATE ─────────────────────────
bool   loraInitialized = false;
int    loraStatus = -999;
float  loraTcxoV = 1.8f;
String loraInitError = "not started";

int     regionIdx = 1;    // EU_868 by default (change in Settings)
int     presetIdx = 0;    // LongFast
float   overrideFreq = 0; // 0 = use the channel-slot frequency
uint8_t txPower = 22;
bool    rxBoost = true;
bool    repeater = false; // relay other nodes' packets

float   curFreq = 869.525f;
float   curBw = 250.0f;
uint8_t curSf = 11, curCr = 5;

uint32_t nodeNum;             // our node number (from MAC)
String   nodeId;              // "!xxxxxxxx"
String   longName, shortName;

uint32_t txCount = 0, rxCount = 0, rxBad = 0, relayCount = 0;
int      lastRSSI = 0;
float    lastSNR = 0;

bool soundOn = true;   // buzzer + LED feedback on TX / RX

String apSsid = "";    // WiFi AP name (empty = default SGP_MESH_xxxx)
String apPass = "";    // WiFi AP password (empty = open; needs >=8 chars for WPA2)
volatile unsigned long rebootAt = 0;   // when !=0 and passed, reboot from loop()

volatile bool rxFlag = false;

// ───────────────────────── CHANNELS ─────────────────────────
// channel 0 is the PRIMARY (its name sets the frequency slot).
#define MAX_CH 4
struct Chan { String name; uint8_t key[32]; uint8_t keyLen; };
Chan chans[MAX_CH];
int chanCount = 0;
int activeChan = 0;   // which channel new messages are sent on

// ───────────────────────── CHAT ─────────────────────────
#define CHAT_MAX 60
struct Msg { uint8_t ch; String from; String text; int rssi; float snr; bool mine; uint8_t hops; unsigned long ts; };
Msg chat[CHAT_MAX];
int chatCount = 0;

// ───────────────────────── NODES ─────────────────────────
#define MAX_NODES 40
struct MNode {
  uint32_t num; String longName; String shortName;
  int rssi; float snr; uint8_t hops; unsigned long lastHeard; bool hasUser;
};
MNode mnodes[MAX_NODES];
int nodeCountN = 0;

// ───────────────────────── DEDUP ─────────────────────────
#define DEDUP_MAX 48
struct Seen { uint32_t from; uint32_t id; };
Seen seen[DEDUP_MAX];
int seenHead = 0;

// ───────────────────────── SCAN ─────────────────────────
#define SCAN_MAX 64
struct ScanPt { float f; int rssi; };
ScanPt scanPts[SCAN_MAX];
int scanCount = 0;

// ───────────────────────── SUPER LISTEN (monitor) ─────────────────────────
// Intercepts Meshtastic traffic by hopping the region's channel slots and
// trying to decrypt every packet with all known keys (public + our channels).
bool     monitorOn = false;
bool     monitorHop = true;      // true = sweep all slots, false = default slot only
int      monitorPreset = 0;      // modem preset used while monitoring
int      monSlot = 0, monSlots = 1;
unsigned long monHopAt = 0;
uint16_t monDwell = 400;         // ms per slot when hopping
uint32_t monTotal = 0, monDecoded = 0;
#define MON_MAX 50
struct Interc {
  float f; uint32_t from; uint32_t to; uint8_t chHash; uint8_t hops;
  int rssi; float snr; uint8_t port; bool dec; String text; unsigned long ts;
};
Interc mlog[MON_MAX];
int mlogCount = 0;

// ───────────────────────── IMAGE OVER LoRa (low-res, chunked) ─────────────────────────
// Send a tiny image as many LoRa packets. The receiver auto-enters image mode
// on the header packet and rebuilds the picture live.
#define IMG_BUF    4096
#define IMG_CHUNKS 64
struct ImgRx {
  bool active; uint8_t id; uint8_t w, h, fmt, nChunks, chunkSize;
  uint16_t total; uint8_t buf[IMG_BUF]; bool got[IMG_CHUNKS]; uint8_t gotCount;
  uint32_t from; unsigned long lastTs;
};
ImgRx imgRx = {0};

// ───────────────────────── SUPER SOS (emergency beacon) ─────────────────────────
// Broadcasts an SOS across every channel slot AND every modem preset of the
// region, on the PUBLIC LongFast channel so any Meshtastic node can read it.
bool     sosOn = false;
String   sosMsg = "";          // optional extra text
String   sosLoc = "";          // "lat,lon" from the phone GPS (optional)
int      sosPreset = 0, sosSlot = 0, sosPhase = 0;
uint32_t sosCount = 0;
unsigned long sosLastAt = 0;
uint16_t sosGap = 550;         // ms between transmissions
float    sosCurFreq = 0;

bool wifiStarted = false;

// RX interrupt flag setter (defined after all structs so Arduino's auto-generated
// prototypes, which are inserted before the first function, see every type above)
void IRAM_ATTR onRx() { rxFlag = true; }

// ═══════════════════════════════════════════════════════════════════════════
// BUZZER + LED
// ═══════════════════════════════════════════════════════════════════════════
void beep(uint16_t f, uint16_t ms) {
  if (!f) return;
  ledcAttach(PIN_BUZZER, f, 8); ledcWrite(PIN_BUZZER, 128);
  delay(ms);
  ledcWrite(PIN_BUZZER, 0); ledcDetach(PIN_BUZZER);
  pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);
}
bool rgbReady = false;
void ledInit() {
  pinMode(PIN_RGB_LED, OUTPUT); digitalWrite(PIN_RGB_LED, LOW); delayMicroseconds(100);
  rgbLED.begin(); rgbLED.setBrightness(RGB_BRIGHTNESS); rgbLED.clear(); rgbLED.show();
  rgbReady = true;
}
void ledColor(uint8_t r, uint8_t g, uint8_t b) {
  if (!rgbReady) ledInit();
  rgbLED.setPixelColor(0, rgbLED.Color(r, g, b));
  if (wifiStarted) WiFi.setSleep(WIFI_PS_MAX_MODEM);
  rgbLED.show();
  if (wifiStarted) WiFi.setSleep(WIFI_PS_NONE);
}
void ledOff() { ledColor(0, 0, 0); }
void notify(uint8_t r, uint8_t g, uint8_t b, uint16_t f1, uint16_t f2) {
  ledColor(r, g, b); if (soundOn) beep(f1, 50); else delay(50); ledOff(); delay(40);
  ledColor(r, g, b); if (soundOn) beep(f2, 70); else delay(60); ledOff();
}
// Feedback when WE send a message (cyan blink + rising chirp)
void txNotify() {
  ledColor(0, 200, 255); if (soundOn) beep(2600, 45); else delay(45); ledOff(); delay(35);
  ledColor(0, 200, 255); if (soundOn) beep(3200, 55); else delay(55); ledOff();
}
// Quick LED-only blip for an intercepted packet in monitor mode (no beep)
void monBlip(bool decoded) {
  ledColor(decoded ? 180 : 50, decoded ? 120 : 30, decoded ? 0 : 70);
  delay(8); ledOff();
}

// ═══════════════════════════════════════════════════════════════════════════
// CRYPTO (mbedtls)
// ═══════════════════════════════════════════════════════════════════════════
void sha256(const uint8_t* in, size_t len, uint8_t out[32]) {
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), in, len, out);
}
// AES-CTR: encryption and decryption are the same operation
void aesCtr(const uint8_t* key, int keyBits, const uint8_t nonce[16],
            const uint8_t* in, uint8_t* out, size_t len) {
  mbedtls_aes_context ctx; mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, keyBits);
  uint8_t nc[16]; memcpy(nc, nonce, 16);
  uint8_t stream[16]; size_t off = 0;
  mbedtls_aes_crypt_ctr(&ctx, len, &off, nc, stream, in, out);
  mbedtls_aes_free(&ctx);
}
uint8_t xorHash(const uint8_t* p, size_t n) { uint8_t h = 0; for (size_t i = 0; i < n; i++) h ^= p[i]; return h; }

// Meshtastic nonce: packetId (8 bytes LE, id zero-extended) + from (4 LE) + 0000
void meshNonce(uint32_t pid, uint32_t from, uint8_t nonce[16]) {
  memset(nonce, 0, 16);
  nonce[0] = pid; nonce[1] = pid >> 8; nonce[2] = pid >> 16; nonce[3] = pid >> 24;
  // bytes 4..7 remain 0 (upper 32 bits of the 64-bit packet id)
  nonce[8] = from; nonce[9] = from >> 8; nonce[10] = from >> 16; nonce[11] = from >> 24;
}

// ═══════════════════════════════════════════════════════════════════════════
// CHANNELS — hash, key expansion, frequency slot
// ═══════════════════════════════════════════════════════════════════════════
// djb2 hash (used by Meshtastic for the frequency slot)
uint32_t djb2(const char* s) { uint32_t h = 5381; int c; while ((c = *s++)) h = ((h << 5) + h) + (uint8_t)c; return h; }

uint8_t chanHash(const Chan& c) {
  uint8_t h = xorHash((const uint8_t*)c.name.c_str(), c.name.length());
  h ^= xorHash(c.key, c.keyLen);
  return h;
}
int chanFindByHash(uint8_t hash) {
  for (int i = 0; i < chanCount; i++) if (chanHash(chans[i]) == hash) return i;
  return -1;
}

// Set a channel's key from a passphrase.  Empty = default LongFast PSK on the
// primary; a 1-char "psk index" style is not used here — we accept either a
// full passphrase (hashed to AES-256) or the literal "default".
void chanSetKey(Chan& c, const String& pass) {
  if (pass.length() == 0 || pass.equalsIgnoreCase("default")) {
    memcpy(c.key, DEFAULT_PSK, 16); c.keyLen = 16;
  } else if (pass.equalsIgnoreCase("none") || pass.equalsIgnoreCase("open")) {
    c.keyLen = 0;
  } else {
    uint8_t h[32]; sha256((const uint8_t*)pass.c_str(), pass.length(), h);
    memcpy(c.key, h, 32); c.keyLen = 32;   // AES-256 from the passphrase
  }
}

void channelsDefault() {
  chans[0].name = "LongFast";
  memcpy(chans[0].key, DEFAULT_PSK, 16); chans[0].keyLen = 16;
  chanCount = 1; activeChan = 0;
}

// Primary channel name decides the frequency slot
float computeFreq() {
  if (overrideFreq > 0) return overrideFreq;
  const Region& r = REGIONS[regionIdx];
  float bwMHz = curBw / 1000.0f;
  int numCh = (int)floorf((r.fEnd - r.fStart) / (r.spacing + bwMHz));
  if (numCh < 1) numCh = 1;
  uint32_t ch = djb2(chans[0].name.c_str()) % numCh;
  return r.fStart + (curBw / 2000.0f) + ch * bwMHz;
}

// ═══════════════════════════════════════════════════════════════════════════
// PROTOBUF (hand-rolled varint — only the fields we use)
// ═══════════════════════════════════════════════════════════════════════════
int pbVarint(uint8_t* o, uint32_t v) { int n = 0; while (v > 0x7f) { o[n++] = (v & 0x7f) | 0x80; v >>= 7; } o[n++] = v; return n; }
int pbKey(uint8_t* o, int field, int wire) { return pbVarint(o, (field << 3) | wire); }
int pbString(uint8_t* o, int field, const String& s) {
  int n = pbKey(o, field, 2); n += pbVarint(o + n, s.length());
  memcpy(o + n, s.c_str(), s.length()); n += s.length(); return n;
}

// Data message: portnum (field 1 varint) + payload (field 2 bytes)
int encodeData(uint8_t port, const uint8_t* payload, int len, uint8_t* out) {
  int n = 0;
  n += pbKey(out + n, 1, 0); n += pbVarint(out + n, port);
  n += pbKey(out + n, 2, 2); n += pbVarint(out + n, len);
  memcpy(out + n, payload, len); n += len;
  return n;
}
bool decodeData(const uint8_t* buf, int len, uint8_t* port, uint8_t* payload, int* plen) {
  int i = 0; *port = 0; *plen = 0;
  while (i < len) {
    uint8_t tag = buf[i++]; int field = tag >> 3, wire = tag & 7;
    if (wire == 0) {
      uint32_t v = 0; int sh = 0;
      while (i < len && (buf[i] & 0x80)) { v |= (uint32_t)(buf[i] & 0x7f) << sh; sh += 7; i++; }
      if (i < len) { v |= (uint32_t)(buf[i] & 0x7f) << sh; i++; }
      if (field == 1) *port = v;
    } else if (wire == 2) {
      uint32_t l = 0; int sh = 0;
      while (i < len && (buf[i] & 0x80)) { l |= (uint32_t)(buf[i] & 0x7f) << sh; sh += 7; i++; }
      if (i < len) { l |= (uint32_t)(buf[i] & 0x7f) << sh; i++; }
      if (field == 2) { int cp = (int)l; if (cp > 240) cp = 240; memcpy(payload, buf + i, cp); *plen = cp; }
      i += l;
    } else if (wire == 5) i += 4;
    else if (wire == 1) i += 8;
    else break;
  }
  return true;
}

// User (NodeInfo) message
int encodeUser(uint8_t* out) {
  int n = 0;
  n += pbString(out + n, 1, nodeId);        // id
  n += pbString(out + n, 2, longName);      // long_name
  n += pbString(out + n, 3, shortName);     // short_name
  n += pbKey(out + n, 5, 0); n += pbVarint(out + n, 255);  // hw_model = PRIVATE_HW
  n += pbKey(out + n, 7, 0); n += pbVarint(out + n, 0);    // role = CLIENT
  return n;
}
void decodeUser(const uint8_t* buf, int len, String& ln, String& sn) {
  int i = 0;
  while (i < len) {
    uint8_t tag = buf[i++]; int field = tag >> 3, wire = tag & 7;
    if (wire == 2) {
      uint32_t l = 0; int sh = 0;
      while (i < len && (buf[i] & 0x80)) { l |= (uint32_t)(buf[i] & 0x7f) << sh; sh += 7; i++; }
      if (i < len) { l |= (uint32_t)(buf[i] & 0x7f) << sh; i++; }
      String s; for (uint32_t k = 0; k < l && i < len; k++) s += (char)buf[i++];
      if (field == 2) ln = s; else if (field == 3) sn = s;
    } else if (wire == 0) {
      while (i < len && (buf[i] & 0x80)) i++; if (i < len) i++;
    } else if (wire == 5) i += 4; else if (wire == 1) i += 8; else break;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// CHAT / NODES helpers
// ═══════════════════════════════════════════════════════════════════════════
String sanitize(String s) { s.replace("\\", ""); s.replace("\"", "'"); s.replace("\n", " "); return s; }
String jsonEsc(const String& s) {
  String o; o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) { char c = s[i]; if (c == '"' || c == '\\') { o += '\\'; o += c; } else if ((uint8_t)c >= 0x20) o += c; }
  return o;
}
void chatAdd(uint8_t ch, const String& from, const String& text, int rssi, float snr, bool mine, uint8_t hops) {
  if (chatCount >= CHAT_MAX) { for (int i = 0; i < CHAT_MAX - 1; i++) chat[i] = chat[i + 1]; chatCount = CHAT_MAX - 1; }
  chat[chatCount++] = { ch, from, text, rssi, snr, mine, hops, millis() / 1000 };
}
MNode* nodeGet(uint32_t num) {
  for (int i = 0; i < nodeCountN; i++) if (mnodes[i].num == num) return &mnodes[i];
  if (nodeCountN < MAX_NODES) { mnodes[nodeCountN] = { num, "", "", 0, 0, 0, 0, false }; return &mnodes[nodeCountN++]; }
  // evict oldest
  int oldest = 0; for (int i = 1; i < nodeCountN; i++) if (mnodes[i].lastHeard < mnodes[oldest].lastHeard) oldest = i;
  mnodes[oldest] = { num, "", "", 0, 0, 0, 0, false }; return &mnodes[oldest];
}
String nodeName(uint32_t num) {
  for (int i = 0; i < nodeCountN; i++) if (mnodes[i].num == num && mnodes[i].hasUser) {
    if (mnodes[i].longName.length()) return mnodes[i].longName;
  }
  char b[12]; snprintf(b, sizeof(b), "!%08x", num); return String(b);
}
bool dedup(uint32_t from, uint32_t id) {
  for (int i = 0; i < DEDUP_MAX; i++) if (seen[i].from == from && seen[i].id == id) return true;
  seen[seenHead] = { from, id }; seenHead = (seenHead + 1) % DEDUP_MAX; return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// RADIO
// ═══════════════════════════════════════════════════════════════════════════
void applyRadio() {
  if (!loraInitialized) return;
  curBw = PRESETS[presetIdx].bw; curSf = PRESETS[presetIdx].sf; curCr = PRESETS[presetIdx].cr;
  curFreq = computeFreq();
  radio.standby();
  radio.setFrequency(curFreq);
  radio.setBandwidth(curBw);
  radio.setSpreadingFactor(curSf);
  radio.setCodingRate(curCr);
  radio.setSyncWord(MESH_SYNC_WORD);
  radio.setPreambleLength(MESH_PREAMBLE);
  radio.setCRC(2);
  uint8_t pw = txPower; if (pw > REGIONS[regionIdx].power) pw = REGIONS[regionIdx].power;
  radio.setOutputPower(pw);
  radio.setRxBoostedGainMode(rxBoost);
  radio.startReceive();
}

String loraCodeMsg(int c) {
  switch (c) {
    case RADIOLIB_ERR_NONE: return "OK";
    case RADIOLIB_ERR_CHIP_NOT_FOUND: return "Chip not found -> check NSS/SCK/MOSI/MISO/BUSY(26) + 3V3";
    case RADIOLIB_ERR_SPI_CMD_TIMEOUT: return "SPI timeout -> BUSY(26) or TCXO";
    default: return "RadioLib error " + String(c);
  }
}
bool loraInit() {
  pinMode(PIN_CC1101_CS, OUTPUT); digitalWrite(PIN_CC1101_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
  curBw = PRESETS[presetIdx].bw; curSf = PRESETS[presetIdx].sf; curCr = PRESETS[presetIdx].cr;
  curFreq = computeFreq();
  loraStatus = RADIOLIB_ERR_CHIP_NOT_FOUND;
  for (unsigned i = 0; i < sizeof(TCXO_OPTIONS) / sizeof(float); i++) {
    float v = TCXO_OPTIONS[i];
    loraStatus = radio.begin(curFreq, curBw, curSf, curCr, MESH_SYNC_WORD, txPower, MESH_PREAMBLE, v);
    Serial.printf("  TCXO %.1fV -> %d\n", v, loraStatus);
    if (loraStatus == RADIOLIB_ERR_NONE) { loraTcxoV = v; break; }
    delay(10);
  }
  if (loraStatus != RADIOLIB_ERR_NONE) { loraInitialized = false; loraInitError = loraCodeMsg(loraStatus); return false; }
  radio.setDio2AsRfSwitch(true);
  radio.setCurrentLimit(140.0);
  radio.setCRC(2);
  radio.setPreambleLength(MESH_PREAMBLE);
  radio.setRxBoostedGainMode(rxBoost);
  radio.setPacketReceivedAction(onRx);
  loraInitialized = true;
  loraInitError = "OK (TCXO " + String(loraTcxoV, 1) + "V)";
  applyRadio();
  return true;
}

// ── build + transmit a Meshtastic packet ──
bool meshTx(uint32_t to, uint8_t port, const uint8_t* payload, int plen, int chIdx, bool wantResp, uint32_t fixedId = 0) {
  if (!loraInitialized) return false;
  // encode Data
  uint8_t data[256]; int dlen = encodeData(port, payload, plen, data);
  if (wantResp) {
    // append optional bitfield (field 9) with want_response bit set
    // bitfield: bit1 = want_response
    data[dlen++] = (9 << 3) | 0; data[dlen++] = 0x02;
  }
  // encrypt with the channel key
  Chan& ch = chans[chIdx];
  uint32_t pid = fixedId ? fixedId : esp_random(); if (!pid) pid = 1;
  uint8_t enc[256];
  if (ch.keyLen > 0) {
    uint8_t nonce[16]; meshNonce(pid, nodeNum, nonce);
    aesCtr(ch.key, ch.keyLen * 8, nonce, data, enc, dlen);
  } else {
    memcpy(enc, data, dlen);
  }
  // header
  MeshHeader h;
  h.to = to; h.from = nodeNum; h.id = pid;
  h.flags = (3 & FLAG_HOP_LIMIT_MASK) | (3 << FLAG_HOP_START_SHIFT); // hop_limit=3, hop_start=3
  h.channel = chanHash(ch);
  h.nextHop = 0;
  h.relayNode = nodeNum & 0xFF;
  uint8_t pkt[HEADER_LEN + 256];
  memcpy(pkt, &h, HEADER_LEN);
  memcpy(pkt + HEADER_LEN, enc, dlen);
  int st = radio.transmit(pkt, HEADER_LEN + dlen);
  rxFlag = false; radio.startReceive();
  if (st == RADIOLIB_ERR_NONE) { txCount++; dedup(nodeNum, pid); return true; }
  return false;
}

bool sendText(const String& textIn, int chIdx) {
  String text = textIn; if (text.length() > 200) text = text.substring(0, 200);
  bool ok = meshTx(BROADCAST_ADDR, PORT_TEXT, (const uint8_t*)text.c_str(), text.length(), chIdx, false);
  if (ok) { chatAdd(chIdx, longName, sanitize(text), 0, 0, true, 0); txNotify(); }
  return ok;
}
void sendNodeInfo(bool wantResp) {
  uint8_t u[128]; int ulen = encodeUser(u);
  meshTx(BROADCAST_ADDR, PORT_NODEINFO, u, ulen, 0, wantResp);
}

// ── relay (repeater) ──
void rebroadcast(const uint8_t* pkt, int len, const MeshHeader& h) {
  if (!repeater) return;
  uint8_t hopLimit = h.flags & FLAG_HOP_LIMIT_MASK;
  if (hopLimit == 0) return;
  if (h.to == nodeNum || h.from == nodeNum) return;
  // copy, decrement hop_limit, set relay to us
  uint8_t out[HEADER_LEN + 256]; if (len > (int)sizeof(out)) return;
  memcpy(out, pkt, len);
  MeshHeader* oh = (MeshHeader*)out;
  oh->flags = (oh->flags & ~FLAG_HOP_LIMIT_MASK) | ((hopLimit - 1) & FLAG_HOP_LIMIT_MASK);
  oh->relayNode = nodeNum & 0xFF;
  delay(20 + (esp_random() % 120)); // small CSMA-style backoff
  radio.standby();
  int st = radio.transmit(out, len);
  rxFlag = false; radio.startReceive();
  if (st == RADIOLIB_ERR_NONE) relayCount++;
}

// ── receive handling ──
void handleRx(uint8_t* pkt, int len, int rssi, float snr) {
  if (len < HEADER_LEN + 1) { rxBad++; return; }
  MeshHeader h; memcpy(&h, pkt, HEADER_LEN);
  lastRSSI = rssi; lastSNR = snr;

  uint8_t hopStart = (h.flags >> FLAG_HOP_START_SHIFT) & 0x07;
  uint8_t hopLimit = h.flags & FLAG_HOP_LIMIT_MASK;
  uint8_t hops = (hopStart >= hopLimit) ? (hopStart - hopLimit) : 0;

  // track the sender as a node (even before we know its name)
  MNode* n = nodeGet(h.from);
  n->rssi = rssi; n->snr = snr; n->hops = hops; n->lastHeard = millis() / 1000;

  bool dup = dedup(h.from, h.id);

  // try to decrypt against each known channel whose hash matches
  int ci = chanFindByHash(h.channel);
  bool decoded = false;
  if (ci >= 0) {
    int clen = len - HEADER_LEN;
    uint8_t dec[256];
    Chan& ch = chans[ci];
    if (ch.keyLen > 0) {
      uint8_t nonce[16]; meshNonce(h.id, h.from, nonce);
      aesCtr(ch.key, ch.keyLen * 8, nonce, pkt + HEADER_LEN, dec, clen);
    } else memcpy(dec, pkt + HEADER_LEN, clen);

    uint8_t port, payload[244]; int plen;
    decodeData(dec, clen, &port, payload, &plen);

    bool isImg = (port == PORT_IMG);
    if (isImg && plen >= 2) {
      decoded = true;
      if (!dup) imgHandleRx(payload, plen, h.from);
    } else if (port == PORT_TEXT && plen > 0) {
      decoded = true;
      if (!dup) {
        String txt; for (int i = 0; i < plen; i++) txt += (char)payload[i];
        chatAdd(ci, nodeName(h.from), txt, rssi, snr, false, hops);
        notify(0, 180, 60, 2200, 3000);
      }
    } else if (port == PORT_NODEINFO && plen > 0) {
      decoded = true;
      String ln, sn; decodeUser(payload, plen, ln, sn);
      n->longName = ln; n->shortName = sn; n->hasUser = true;
      if (!dup) notify(80, 120, 255, 2600, 2600);
      // respond to a direct solicited request so we show up fast
    } else if (plen >= 0) {
      decoded = true; // some other app (position/telemetry/routing) — counted, not shown
    }

    // forward to the phone app over BLE (only packets we could decrypt; not image chunks)
    if (!dup && decoded && !isImg && bleConnected)
      ble_forwardPacket(h.from, h.to, ci, port, payload, plen, rssi, snr, hops, h.id);
  }

  if (!dup) { rxCount++; rebroadcast(pkt, len, h); }
}

// ═══════════════════════════════════════════════════════════════════════════
// SPECTRUM SCAN (RSSI sweep across the current region)
// ═══════════════════════════════════════════════════════════════════════════
void doScan() {
  if (!loraInitialized) { scanCount = 0; return; }
  const Region& r = REGIONS[regionIdx];
  float span = r.fEnd - r.fStart; if (span <= 0) span = 1;
  float step = span / (float)(SCAN_MAX - 1);
  if (step < 0.1f) step = 0.1f;
  scanCount = 0;
  radio.standby();
  for (float f = r.fStart; f <= r.fEnd + 0.001f && scanCount < SCAN_MAX; f += step) {
    radio.setFrequency(f);
    radio.startReceive();
    delay(8);
    int best = -200;
    for (int s = 0; s < 3; s++) { int v = (int)radio.getRSSI(false); if (v > best) best = v; delay(2); }
    scanPts[scanCount++] = { f, best };
  }
  applyRadio();
}

// ═══════════════════════════════════════════════════════════════════════════
// SUPER LISTEN (monitor) — hop the region's slots, decrypt with every known key
// ═══════════════════════════════════════════════════════════════════════════
// Try to decrypt+decode an encrypted payload with a given AES key. Returns true
// and fills port/text if a valid Meshtastic Data message comes out.
bool tryKey(const uint8_t* key, int keyBits, uint32_t id, uint32_t from,
            const uint8_t* ct, int clen, uint8_t* portOut, String& textOut) {
  uint8_t nonce[16]; meshNonce(id, from, nonce);
  uint8_t dec[256]; if (clen > 256) clen = 256;
  aesCtr(key, keyBits, nonce, ct, dec, clen);
  uint8_t port, pl[244]; int plen;
  decodeData(dec, clen, &port, pl, &plen);
  // accept only known portnums (filters out garbage from a wrong key)
  bool known = (port == PORT_TEXT || port == PORT_POSITION || port == PORT_NODEINFO ||
                port == PORT_ROUTING || port == PORT_TELEMETRY);
  if (!known || plen <= 0) return false;
  if (port == PORT_TEXT) {
    // require printable text
    for (int i = 0; i < plen; i++) { uint8_t c = pl[i]; if (c < 0x09 || (c > 0x0d && c < 0x20)) return false; }
    textOut = ""; for (int i = 0; i < plen; i++) textOut += (char)pl[i];
  } else if (port == PORT_NODEINFO) {
    String ln, sn; decodeUser(pl, plen, ln, sn); textOut = "node: " + (ln.length() ? ln : String("?"));
  } else textOut = "";
  *portOut = port;
  return true;
}

// Attempt to decrypt an intercepted packet with all candidate keys.
bool monDecrypt(uint32_t id, uint32_t from, const uint8_t* ct, int clen, uint8_t* portOut, String& textOut) {
  // 1) our configured channel keys
  for (int i = 0; i < chanCount; i++)
    if (chans[i].keyLen > 0 && tryKey(chans[i].key, chans[i].keyLen * 8, id, from, ct, clen, portOut, textOut)) return true;
  // 2) public default PSK + its short-index variants (public channels 1..12)
  uint8_t k[16];
  for (int idx = 1; idx <= 12; idx++) {
    memcpy(k, DEFAULT_PSK, 16);
    k[15] = (uint8_t)(DEFAULT_PSK[15] + idx - 1);   // Meshtastic short-PSK expansion
    if (tryKey(k, 128, id, from, ct, clen, portOut, textOut)) return true;
  }
  return false;
}

int monDefaultSlot() {
  const Region& r = REGIONS[regionIdx];
  float bw = PRESETS[monitorPreset].bw;
  int n = (int)floorf((r.fEnd - r.fStart) / (r.spacing + bw / 1000.0f)); if (n < 1) n = 1;
  return djb2(PRESETS[monitorPreset].name) % n;
}
int monNumSlots() {
  const Region& r = REGIONS[regionIdx];
  float bw = PRESETS[monitorPreset].bw;
  int n = (int)floorf((r.fEnd - r.fStart) / (r.spacing + bw / 1000.0f)); if (n < 1) n = 1;
  return n;
}
float monSlotFreq(int i) {
  const Region& r = REGIONS[regionIdx];
  float bw = PRESETS[monitorPreset].bw;
  return r.fStart + (bw / 2000.0f) + i * (bw / 1000.0f);
}
void monTune() {
  if (!loraInitialized) return;
  radio.standby();
  radio.setBandwidth(PRESETS[monitorPreset].bw);
  radio.setSpreadingFactor(PRESETS[monitorPreset].sf);
  radio.setCodingRate(PRESETS[monitorPreset].cr);
  radio.setSyncWord(MESH_SYNC_WORD);
  radio.setPreambleLength(MESH_PREAMBLE);
  radio.setCRC(2);
  monSlots = monNumSlots();
  monSlot = monitorHop ? 0 : monDefaultSlot();
  curFreq = monSlotFreq(monSlot);
  radio.setFrequency(curFreq);
  radio.startReceive();
  monHopAt = millis();
}
void monitorStart() { monitorOn = true; monTune(); }
void monitorStop()  { monitorOn = false; applyRadio(); }

void monitorHandleRx(uint8_t* pkt, int len, int rssi, float snr) {
  if (len < HEADER_LEN + 1) { rxBad++; return; }
  MeshHeader h; memcpy(&h, pkt, HEADER_LEN);
  uint8_t hopStart = (h.flags >> FLAG_HOP_START_SHIFT) & 0x07;
  uint8_t hopLimit = h.flags & FLAG_HOP_LIMIT_MASK;
  uint8_t hops = (hopStart >= hopLimit) ? (hopStart - hopLimit) : 0;
  if (dedup(h.from, h.id)) return;   // avoid logging the same packet repeatedly

  uint8_t port = 0; String text;
  bool dec = monDecrypt(h.id, h.from, pkt + HEADER_LEN, len - HEADER_LEN, &port, text);

  monTotal++; if (dec) monDecoded++;
  // push into the intercept log (newest kept, ring buffer)
  if (mlogCount >= MON_MAX) { for (int i = 0; i < MON_MAX - 1; i++) mlog[i] = mlog[i + 1]; mlogCount = MON_MAX - 1; }
  mlog[mlogCount++] = { curFreq, h.from, h.to, h.channel, hops, rssi, snr, port, dec, text, millis() / 1000 };
  monBlip(dec);
}

// Non-blocking slot hop, called from loop() while monitoring
void monitorTick() {
  if (!monitorOn || !monitorHop || !loraInitialized) return;
  if (millis() - monHopAt < monDwell) return;
  monSlot = (monSlot + 1) % monSlots;
  curFreq = monSlotFreq(monSlot);
  radio.standby(); radio.setFrequency(curFreq); radio.startReceive();
  monHopAt = millis();
}

// ═══════════════════════════════════════════════════════════════════════════
// IMAGE OVER LoRa — receive (auto mode) + send
// "Ready" signal — LED + beep on both ends. sender=cyan/high, receiver=green/mid.
void imgReady(bool sender) {
  for (int k = 0; k < 2; k++) {
    ledColor(sender ? 0 : 0, sender ? 180 : 210, sender ? 255 : 70);
    if (soundOn) beep(sender ? 2400 : 1900, 90); else delay(90);
    ledOff(); delay(70);
  }
}

// payload[0]=subtype: 0x00 header, 0x01 data
void imgHandleRx(const uint8_t* p, int n, uint32_t from) {
  uint8_t sub = p[0];
  if (sub == 0x00 && n >= 7) {
    uint8_t id = p[1];
    // ignore duplicate headers of the image we're already receiving
    // (we send the header 3x for reliability, and it may be relayed)
    if (imgRx.active && imgRx.id == id && imgRx.from == from) return;
    // HEADER → auto-enter receive mode and reset the buffer
    imgRx.active = true;
    imgRx.id = id; imgRx.w = p[2]; imgRx.h = p[3]; imgRx.fmt = p[4];
    imgRx.nChunks = p[5]; imgRx.chunkSize = p[6];
    imgRx.from = from; imgRx.gotCount = 0; imgRx.lastTs = millis();
    memset(imgRx.got, 0, sizeof(imgRx.got));
    memset(imgRx.buf, 0, sizeof(imgRx.buf));
    imgRx.total = (uint16_t)imgRx.nChunks * imgRx.chunkSize;
    if (imgRx.total > IMG_BUF) imgRx.total = IMG_BUF;
    chatAdd(activeChan, nodeName(from), "[img] Receiving image " + String(imgRx.w) + "x" + String(imgRx.h) + "...", 0, 0, false, 0);
    imgReady(false);   // RECEIVER ready: LED + beep
  } else if (sub == 0x01 && n >= 3 && imgRx.active && p[1] == imgRx.id) {
    // DATA chunk
    uint8_t idx = p[2]; int dlen = n - 3;
    uint32_t off = (uint32_t)idx * imgRx.chunkSize;
    if (idx < IMG_CHUNKS && off + dlen <= IMG_BUF) {
      memcpy(imgRx.buf + off, p + 3, dlen);
      if (!imgRx.got[idx]) {
        imgRx.got[idx] = true; imgRx.gotCount++;
        if (imgRx.gotCount >= imgRx.nChunks) {        // image complete
          chatAdd(activeChan, nodeName(from), "[img] Image complete.", 0, 0, false, 0);
          if (soundOn) { beep(2000, 70); beep(2600, 70); beep(3200, 90); }
        }
      }
      imgRx.lastTs = millis();
      ledColor(0, 180, 255); delay(6); ledOff();   // tiny blip per chunk
    }
  }
}

bool imgTxHeader(uint8_t id, uint8_t w, uint8_t h, uint8_t fmt, uint8_t nCh, uint8_t cs) {
  uint8_t p[7] = { 0x00, id, w, h, fmt, nCh, cs };
  return meshTx(BROADCAST_ADDR, PORT_IMG, p, 7, activeChan, false);
}
// Announce an image: send the header several times so the receiver reliably
// catches it and auto-enters image mode. Beeps + LED on THIS (sender) node.
bool imgAnnounce(uint8_t id, uint8_t w, uint8_t h, uint8_t fmt, uint8_t nCh, uint8_t cs) {
  bool ok = false;
  for (int k = 0; k < 3; k++) { ok |= imgTxHeader(id, w, h, fmt, nCh, cs); delay(130); }
  chatAdd(activeChan, longName, "[img] Sending image " + String(w) + "x" + String(h) + "...", 0, 0, true, 0);
  imgReady(true);   // SENDER ready: LED + beep
  return ok;
}
bool imgTxChunk(uint8_t id, uint8_t idx, const uint8_t* d, int n) {
  if (n > 200) n = 200;
  uint8_t p[3 + 200]; p[0] = 0x01; p[1] = id; p[2] = idx;
  memcpy(p + 3, d, n);
  return meshTx(BROADCAST_ADDR, PORT_IMG, p, 3 + n, activeChan, false);
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}
int hexToBytes(const String& hex, uint8_t* out, int maxLen) {
  int n = hex.length() / 2; if (n > maxLen) n = maxLen;
  for (int i = 0; i < n; i++) out[i] = (hexNibble(hex[2*i]) << 4) | hexNibble(hex[2*i+1]);
  return n;
}
String bytesToHex(const uint8_t* b, int n) {
  static const char* H = "0123456789abcdef";
  String s; s.reserve(n * 2);
  for (int i = 0; i < n; i++) { s += H[b[i] >> 4]; s += H[b[i] & 0xf]; }
  return s;
}

String jImgRx() {
  String j = "{\"active\":" + String(imgRx.active ? "true" : "false");
  if (imgRx.active) {
    char fb[12]; snprintf(fb, sizeof(fb), "!%08x", imgRx.from);
    j += ",\"id\":" + String(imgRx.id) + ",\"w\":" + String(imgRx.w) + ",\"h\":" + String(imgRx.h);
    j += ",\"fmt\":" + String(imgRx.fmt) + ",\"n\":" + String(imgRx.nChunks) + ",\"got\":" + String(imgRx.gotCount);
    j += ",\"cs\":" + String(imgRx.chunkSize) + ",\"from\":\"" + String(fb) + "\",";
    j += "\"age\":" + String((millis() - imgRx.lastTs) / 1000) + ",";
    // received-chunk bitmap
    j += "\"map\":\"";
    for (int i = 0; i < imgRx.nChunks && i < IMG_CHUNKS; i++) j += imgRx.got[i] ? '1' : '0';
    j += "\",";
    j += "\"data\":\"" + bytesToHex(imgRx.buf, imgRx.total) + "\"";
  }
  return j + "}";
}

// ═══════════════════════════════════════════════════════════════════════════
// SUPER SOS — emergency beacon across all slots and all presets
// ═══════════════════════════════════════════════════════════════════════════
String sosBuildText() {
  String t = "SOS SOS SOS";
  if (sosMsg.length()) t += " " + sanitize(sosMsg);
  if (sosLoc.length()) t += " LOC " + sosLoc + " https://maps.google.com/?q=" + sosLoc;
  t += " [" + nodeId + "]";
  if (t.length() > 200) t = t.substring(0, 200);
  return t;
}
int sosNumSlots(int pi) {
  const Region& r = REGIONS[regionIdx];
  float bw = PRESETS[pi].bw;
  int n = (int)floorf((r.fEnd - r.fStart) / (r.spacing + bw / 1000.0f)); if (n < 1) n = 1;
  return n;
}
float sosSlotFreq(int pi, int slot) {
  const Region& r = REGIONS[regionIdx];
  float bw = PRESETS[pi].bw;
  return r.fStart + (bw / 2000.0f) + slot * (bw / 1000.0f);
}
float sosDefaultSlot(int pi) {
  return sosSlotFreq(pi, djb2("LongFast") % sosNumSlots(pi));
}
// Transmit the SOS on a given frequency + preset, on the PUBLIC LongFast channel
void sosTxAt(float freq, int pi, const String& text) {
  if (!loraInitialized) return;
  radio.standby();
  radio.setBandwidth(PRESETS[pi].bw);
  radio.setSpreadingFactor(PRESETS[pi].sf);
  radio.setCodingRate(PRESETS[pi].cr);
  radio.setSyncWord(MESH_SYNC_WORD);
  radio.setPreambleLength(MESH_PREAMBLE);
  radio.setCRC(2);
  radio.setFrequency(freq);
  sosCurFreq = freq;

  uint8_t data[256]; int dlen = encodeData(PORT_TEXT, (const uint8_t*)text.c_str(), text.length(), data);
  uint32_t pid = esp_random(); if (!pid) pid = 1;
  uint8_t nonce[16]; meshNonce(pid, nodeNum, nonce);
  uint8_t enc[256]; aesCtr(DEFAULT_PSK, 128, nonce, data, enc, dlen);
  uint8_t pubHash = xorHash((const uint8_t*)"LongFast", 8) ^ xorHash(DEFAULT_PSK, 16);  // = 0x08
  MeshHeader h;
  h.to = BROADCAST_ADDR; h.from = nodeNum; h.id = pid;
  h.flags = (3 & FLAG_HOP_LIMIT_MASK) | (3 << FLAG_HOP_START_SHIFT);
  h.channel = pubHash; h.nextHop = 0; h.relayNode = nodeNum & 0xFF;
  uint8_t pkt[HEADER_LEN + 256];
  memcpy(pkt, &h, HEADER_LEN); memcpy(pkt + HEADER_LEN, enc, dlen);
  radio.transmit(pkt, HEADER_LEN + dlen);
  sosCount++;
}
void sosStart() {
  if (monitorOn) monitorStop();
  sosOn = true; sosPreset = 0; sosSlot = 0; sosPhase = 0; sosLastAt = 0;
  // confirm start: red triple blink + rising beep
  for (int k = 0; k < 3; k++) { ledColor(255, 0, 0); if (soundOn) beep(2200 + k*400, 90); else delay(90); ledOff(); delay(70); }
}
void sosStop() {
  sosOn = false;
  applyRadio();   // back to normal operation
  ledColor(0, 60, 0); delay(120); ledOff();
}
// Non-blocking SOS sweeper, called from loop()
void sosTick() {
  if (!sosOn || !loraInitialized) return;
  if (millis() - sosLastAt < sosGap) return;
  String txt = sosBuildText();
  if (sosPhase == 0) {
    // every other TX hits the common target: LongFast default slot (most nodes)
    sosTxAt(sosDefaultSlot(0), 0, txt);
    sosPhase = 1;
  } else {
    // sweep: current preset + slot, then advance through ALL of them
    sosTxAt(sosSlotFreq(sosPreset, sosSlot), sosPreset, txt);
    sosSlot++;
    if (sosSlot >= sosNumSlots(sosPreset)) { sosSlot = 0; sosPreset = (sosPreset + 1) % PRESET_COUNT; }
    sosPhase = 0;
  }
  sosLastAt = millis();
  ledColor(255, 0, 0); delay(8); ledOff();        // red flash per TX
  if (soundOn && (sosCount % 6 == 0)) { beep(2600, 50); }  // periodic chirp
}

// ═══════════════════════════════════════════════════════════════════════════
// PERSISTENCE
// ═══════════════════════════════════════════════════════════════════════════
void cfgSave() {
  prefs.begin("mesh", false);
  prefs.putInt("region", regionIdx);
  prefs.putInt("preset", presetIdx);
  prefs.putUChar("pwr", txPower);
  prefs.putBool("boost", rxBoost);
  prefs.putBool("rep", repeater);
  prefs.putFloat("ofreq", overrideFreq);
  prefs.putString("ln", longName);
  prefs.putString("sn", shortName);
  prefs.putString("ch0", chans[0].name);
  prefs.putBool("snd", soundOn);
  prefs.putString("apssid", apSsid);
  prefs.putString("appass", apPass);
  prefs.end();
}
void cfgLoad() {
  prefs.begin("mesh", true);
  regionIdx = prefs.getInt("region", 1);
  presetIdx = prefs.getInt("preset", 0);
  txPower   = prefs.getUChar("pwr", 22);
  rxBoost   = prefs.getBool("boost", true);
  repeater  = prefs.getBool("rep", false);
  overrideFreq = prefs.getFloat("ofreq", 0);
  soundOn   = prefs.getBool("snd", true);
  longName  = prefs.getString("ln", "");
  shortName = prefs.getString("sn", "");
  String ch0 = prefs.getString("ch0", "LongFast");
  apSsid    = prefs.getString("apssid", "");
  apPass    = prefs.getString("appass", "");
  prefs.end();
  if (!apSsid.length()) apSsid = "SGP_MESH_" + String((uint16_t)(nodeNum & 0xFFFF), HEX);
  if (regionIdx < 0 || regionIdx >= REGION_COUNT) regionIdx = 1;
  if (presetIdx < 0 || presetIdx >= PRESET_COUNT) presetIdx = 0;
  if (ch0.length()) chans[0].name = ch0;
}

// ═══════════════════════════════════════════════════════════════════════════
// JSON ENDPOINTS
// ═══════════════════════════════════════════════════════════════════════════
String jStatus() {
  String j = "{";
  j += "\"fw\":\"" FW_VERSION "\",\"chip\":\"SX1262\",";
  j += "\"id\":\"" + nodeId + "\",";
  j += "\"name\":\"" + jsonEsc(longName) + "\",\"short\":\"" + jsonEsc(shortName) + "\",";
  j += "\"init\":" + String(loraInitialized ? "true" : "false") + ",";
  j += "\"region\":\"" + String(REGIONS[regionIdx].name) + "\",";
  j += "\"preset\":\"" + String(PRESETS[presetIdx].name) + "\",";
  j += "\"freq\":" + String(curFreq, 3) + ",";
  j += "\"bw\":" + String(curBw, 0) + ",\"sf\":" + String(curSf) + ",\"cr\":" + String(curCr) + ",";
  j += "\"pwr\":" + String(txPower) + ",\"maxpwr\":" + String(REGIONS[regionIdx].power) + ",";
  j += "\"boost\":" + String(rxBoost ? "true" : "false") + ",";
  j += "\"repeater\":" + String(repeater ? "true" : "false") + ",";
  j += "\"override\":" + String(overrideFreq, 3) + ",";
  j += "\"sound\":" + String(soundOn ? "true" : "false") + ",";
  j += "\"monitor\":" + String(monitorOn ? "true" : "false") + ",";
  j += "\"apssid\":\"" + jsonEsc(apSsid) + "\",\"appass\":\"" + jsonEsc(apPass) + "\",";
  j += "\"chan\":\"" + jsonEsc(chans[activeChan].name) + "\",\"chanIdx\":" + String(activeChan) + ",";
  j += "\"primary\":\"" + jsonEsc(chans[0].name) + "\",";
  j += "\"tx\":" + String(txCount) + ",\"rx\":" + String(rxCount) + ",\"relay\":" + String(relayCount) + ",\"bad\":" + String(rxBad) + ",";
  j += "\"rssi\":" + String(lastRSSI) + ",\"snr\":" + String(lastSNR, 1) + ",";
  j += "\"nodes\":" + String(nodeCountN) + ",";
  j += "\"clients\":" + String(WiFi.softAPgetStationNum());
  j += "}";
  return j;
}
String jChat() {
  String j = "{\"chanIdx\":" + String(activeChan) + ",\"chan\":\"" + jsonEsc(chans[activeChan].name) + "\",\"msgs\":[";
  bool first = true;
  for (int i = 0; i < chatCount; i++) {
    if (chat[i].ch != activeChan) continue;
    if (!first) j += ","; first = false;
    j += "{\"f\":\"" + jsonEsc(chat[i].from) + "\",\"t\":\"" + jsonEsc(chat[i].text) + "\",";
    j += "\"r\":" + String(chat[i].rssi) + ",\"snr\":" + String(chat[i].snr, 1) + ",";
    j += "\"m\":" + String(chat[i].mine ? "true" : "false") + ",\"h\":" + String(chat[i].hops) + ",\"ts\":" + String(chat[i].ts) + "}";
  }
  return j + "]}";
}
String jChannels() {
  String j = "{\"active\":" + String(activeChan) + ",\"list\":[";
  for (int i = 0; i < chanCount; i++) {
    if (i) j += ",";
    j += "{\"i\":" + String(i) + ",\"name\":\"" + jsonEsc(chans[i].name) + "\",";
    j += "\"enc\":" + String(chans[i].keyLen > 0 ? "true" : "false") + ",";
    j += "\"bits\":" + String(chans[i].keyLen * 8) + ",";
    j += "\"hash\":\"0x" + String(chanHash(chans[i]), HEX) + "\",\"primary\":" + String(i == 0 ? "true" : "false") + "}";
  }
  return j + "]}";
}
String jNodes() {
  unsigned long now = millis() / 1000;
  String j = "{\"count\":" + String(nodeCountN) + ",\"list\":[";
  for (int i = 0; i < nodeCountN; i++) {
    if (i) j += ",";
    char idb[12]; snprintf(idb, sizeof(idb), "!%08x", mnodes[i].num);
    j += "{\"id\":\"" + String(idb) + "\",";
    j += "\"name\":\"" + jsonEsc(mnodes[i].hasUser && mnodes[i].longName.length() ? mnodes[i].longName : String(idb)) + "\",";
    j += "\"short\":\"" + jsonEsc(mnodes[i].shortName) + "\",";
    j += "\"rssi\":" + String(mnodes[i].rssi) + ",\"snr\":" + String(mnodes[i].snr, 1) + ",";
    j += "\"hops\":" + String(mnodes[i].hops) + ",\"ago\":" + String(now - mnodes[i].lastHeard) + "}";
  }
  return j + "]}";
}
String jRegions() {
  String j = "{\"regionIdx\":" + String(regionIdx) + ",\"presetIdx\":" + String(presetIdx) + ",\"regions\":[";
  for (int i = 0; i < REGION_COUNT; i++) { if (i) j += ","; j += "\"" + String(REGIONS[i].name) + "\""; }
  j += "],\"presets\":[";
  for (int i = 0; i < PRESET_COUNT; i++) { if (i) j += ","; j += "\"" + String(PRESETS[i].name) + "\""; }
  j += "]}";
  return j;
}
String jScan() {
  String j = "{\"region\":\"" + String(REGIONS[regionIdx].name) + "\",\"freq\":" + String(curFreq, 3) + ",\"points\":[";
  for (int i = 0; i < scanCount; i++) { if (i) j += ","; j += "{\"f\":" + String(scanPts[i].f, 2) + ",\"r\":" + String(scanPts[i].rssi) + "}"; }
  return j + "]}";
}
String jSos() {
  String j = "{";
  j += "\"on\":" + String(sosOn ? "true" : "false") + ",";
  j += "\"count\":" + String(sosCount) + ",";
  j += "\"msg\":\"" + jsonEsc(sosMsg) + "\",\"loc\":\"" + jsonEsc(sosLoc) + "\",";
  j += "\"freq\":" + String(sosCurFreq, 3) + ",";
  j += "\"preset\":\"" + String(PRESETS[sosPhase == 0 ? 0 : sosPreset].name) + "\",";
  j += "\"region\":\"" + String(REGIONS[regionIdx].name) + "\"";
  return j + "}";
}
String jMonitor() {
  unsigned long now = millis() / 1000;
  String j = "{";
  j += "\"on\":" + String(monitorOn ? "true" : "false") + ",";
  j += "\"hop\":" + String(monitorHop ? "true" : "false") + ",";
  j += "\"preset\":\"" + String(PRESETS[monitorPreset].name) + "\",\"presetIdx\":" + String(monitorPreset) + ",";
  j += "\"region\":\"" + String(REGIONS[regionIdx].name) + "\",";
  j += "\"slot\":" + String(monSlot) + ",\"slots\":" + String(monSlots) + ",";
  j += "\"freq\":" + String(curFreq, 3) + ",";
  j += "\"total\":" + String(monTotal) + ",\"decoded\":" + String(monDecoded) + ",";
  j += "\"log\":[";
  // newest first
  for (int i = mlogCount - 1, n = 0; i >= 0; i--, n++) {
    if (n) j += ",";
    char fb[12]; snprintf(fb, sizeof(fb), "!%08x", mlog[i].from);
    String toS;
    if (mlog[i].to == 0xFFFFFFFFUL) toS = "all";
    else { char tb[12]; snprintf(tb, sizeof(tb), "!%08x", mlog[i].to); toS = tb; }
    const char* pn = "?";
    switch (mlog[i].port) { case PORT_TEXT: pn="text"; break; case PORT_NODEINFO: pn="nodeinfo"; break;
      case PORT_POSITION: pn="position"; break; case PORT_TELEMETRY: pn="telemetry"; break; case PORT_ROUTING: pn="routing"; break; }
    j += "{\"f\":" + String(mlog[i].f, 3) + ",\"from\":\"" + String(fb) + "\",\"to\":\"" + toS + "\",";
    j += "\"hash\":\"0x" + String(mlog[i].chHash, HEX) + "\",\"hops\":" + String(mlog[i].hops) + ",";
    j += "\"rssi\":" + String(mlog[i].rssi) + ",\"snr\":" + String(mlog[i].snr, 1) + ",";
    j += "\"dec\":" + String(mlog[i].dec ? "true" : "false") + ",\"port\":\"" + String(pn) + "\",";
    j += "\"text\":\"" + jsonEsc(mlog[i].text) + "\",\"ago\":" + String(now - mlog[i].ts) + "}";
  }
  return j + "]}";
}
String jDiag() {
  bool busOk = false;
  if (loraInitialized) { busOk = (radio.standby() == RADIOLIB_ERR_NONE); radio.startReceive(); }
  String j = "{";
  j += "\"init\":" + String(loraInitialized ? "true" : "false") + ",";
  j += "\"busTest\":" + String(busOk ? "true" : "false") + ",";
  j += "\"tcxo\":" + String(loraTcxoV, 1) + ",\"code\":" + String(loraStatus) + ",";
  j += "\"error\":\"" + jsonEsc(loraInitError) + "\",";
  j += "\"primaryHash\":\"0x" + String(chanHash(chans[0]), HEX) + "\",";
  j += "\"pins\":\"NSS=38 RST=47 DIO1=48 BUSY=26 SCK=36 MISO=33 MOSI=35\"";
  return j + "}";
}

// ═══════════════════════════════════════════════════════════════════════════
// WEB SERVER
// ═══════════════════════════════════════════════════════════════════════════
void sendJson(const String& s) { server.send(200, "application/json", s); }

void setupWeb() {
  server.on("/", []() { server.send_P(200, "text/html; charset=utf-8", PAGE_INDEX); });

  server.on("/api/status",   []() { sendJson(jStatus()); });
  server.on("/api/chat",     []() { sendJson(jChat()); });
  server.on("/api/channels", []() { sendJson(jChannels()); });
  server.on("/api/nodes",    []() { sendJson(jNodes()); });
  server.on("/api/regions",  []() { sendJson(jRegions()); });
  server.on("/api/diag",     []() { sendJson(jDiag()); });

  server.on("/api/scan", []() { doScan(); sendJson(jScan()); });

  // ── super listen (monitor) ──
  server.on("/api/monitor", []() { sendJson(jMonitor()); });
  server.on("/api/monitor/set", []() {
    if (server.hasArg("preset")) { int v = server.arg("preset").toInt(); if (v >= 0 && v < PRESET_COUNT) monitorPreset = v; }
    if (server.hasArg("hop")) monitorHop = (server.arg("hop") == "1");
    if (server.hasArg("on")) {
      bool want = (server.arg("on") == "1");
      if (want && !monitorOn) monitorStart();
      else if (!want && monitorOn) monitorStop();
      else if (want && monitorOn) monTune();   // re-apply preset/hop change
    } else if (monitorOn) monTune();
    sendJson(jMonitor());
  });
  server.on("/api/monitor/clear", []() { mlogCount = 0; monTotal = 0; monDecoded = 0; sendJson(jMonitor()); });

  // ── sound / LED feedback toggle ──
  server.on("/api/sound", []() {
    if (server.hasArg("on")) { soundOn = (server.arg("on") == "1"); cfgSave(); }
    sendJson(jStatus());
  });

  // ── WiFi AP name + password (persisted; reboots to apply) ──
  server.on("/api/wifi", []() {
    String ss = server.arg("ssid"); ss.trim();
    String pw = server.arg("pass");
    if (ss.length() < 1 || ss.length() > 31) { sendJson("{\"ok\":false,\"err\":\"Name must be 1-31 characters\"}"); return; }
    if (pw.length() > 0 && (pw.length() < 8 || pw.length() > 63)) {
      sendJson("{\"ok\":false,\"err\":\"Password must be empty (open) or 8-63 characters\"}"); return;
    }
    apSsid = ss; apPass = pw; cfgSave();
    sendJson("{\"ok\":true,\"reboot\":true}");
    rebootAt = millis() + 900;   // reboot from loop() AFTER the response is flushed
  });

  // ── super SOS ──
  server.on("/api/sos", []() { sendJson(jSos()); });
  server.on("/api/sos/start", []() {
    if (server.hasArg("msg")) sosMsg = server.arg("msg").substring(0, 120);
    sosLoc = server.hasArg("loc") ? server.arg("loc") : "";
    sosStart();
    sendJson(jSos());
  });
  server.on("/api/sos/stop", []() { sosStop(); sendJson(jSos()); });

  // ── image over LoRa ──
  server.on("/api/img/rx", []() { sendJson(jImgRx()); });
  server.on("/api/img/clear", []() { imgRx.active = false; imgRx.gotCount = 0; sendJson(jImgRx()); });
  server.on("/api/img/tx", []() {
    String t = server.arg("t");
    bool ok = false;
    uint8_t id = (uint8_t)server.arg("id").toInt();
    if (t == "h") {
      ok = imgAnnounce(id, (uint8_t)server.arg("iw").toInt(), (uint8_t)server.arg("ih").toInt(),
                       (uint8_t)server.arg("fmt").toInt(), (uint8_t)server.arg("n").toInt(),
                       (uint8_t)server.arg("cs").toInt());
    } else if (t == "d") {
      uint8_t idx = (uint8_t)server.arg("idx").toInt();
      uint8_t data[220]; int dn = hexToBytes(server.arg("data"), data, sizeof(data));
      ok = imgTxChunk(id, idx, data, dn);
    }
    sendJson(String("{\"ok\":") + (ok ? "true" : "false") + "}");
  });

  // send a text message
  server.on("/api/send", []() {
    String t = server.arg("t");
    int ci = server.hasArg("c") ? server.arg("c").toInt() : activeChan;
    if (ci < 0 || ci >= chanCount) ci = activeChan;
    bool ok = (t.length() > 0) && sendText(t, ci);
    sendJson(String("{\"ok\":") + (ok ? "true" : "false") + "}");
  });

  // broadcast our NodeInfo (announce / request replies)
  server.on("/api/announce", []() { sendNodeInfo(true); sendJson("{\"ok\":true}"); });

  // select active channel
  server.on("/api/chan/sel", []() {
    int i = server.arg("i").toInt();
    if (i >= 0 && i < chanCount) activeChan = i;
    sendJson(jChannels());
  });
  // add a channel (name + optional passphrase)
  server.on("/api/chan/add", []() {
    if (chanCount < MAX_CH) {
      String nm = server.arg("name"); if (!nm.length()) nm = "channel" + String(chanCount);
      chans[chanCount].name = nm;
      chanSetKey(chans[chanCount], server.arg("pass"));
      chanCount++;
      if (server.hasArg("primary") || chanCount == 1) { /* primary stays index 0 */ }
    }
    applyRadio(); cfgSave();
    sendJson(jChannels());
  });
  // delete a channel (not the primary)
  server.on("/api/chan/del", []() {
    int i = server.arg("i").toInt();
    if (i > 0 && i < chanCount) {
      for (int k = i; k < chanCount - 1; k++) chans[k] = chans[k + 1];
      chanCount--;
      if (activeChan >= chanCount) activeChan = 0;
    }
    applyRadio(); cfgSave();
    sendJson(jChannels());
  });
  // set the PRIMARY channel name (this changes the frequency slot)
  server.on("/api/chan/primary", []() {
    String nm = server.arg("name");
    if (nm.length()) { chans[0].name = nm; if (server.hasArg("pass")) chanSetKey(chans[0], server.arg("pass")); }
    applyRadio(); cfgSave();
    sendJson(jStatus());
  });

  // radio config
  server.on("/api/radio", []() {
    if (server.hasArg("region")) { int v = server.arg("region").toInt(); if (v >= 0 && v < REGION_COUNT) regionIdx = v; }
    if (server.hasArg("preset")) { int v = server.arg("preset").toInt(); if (v >= 0 && v < PRESET_COUNT) presetIdx = v; }
    if (server.hasArg("pwr"))    { int v = server.arg("pwr").toInt(); if (v >= 2 && v <= 22) txPower = v; }
    if (server.hasArg("boost"))  rxBoost = (server.arg("boost") == "1");
    if (server.hasArg("repeater")) repeater = (server.arg("repeater") == "1");
    if (server.hasArg("override")) { float f = server.arg("override").toFloat(); overrideFreq = (f > 100 && f < 2500) ? f : 0; }
    applyRadio(); cfgSave();
    sendJson(jStatus());
  });

  // identity
  server.on("/api/name", []() {
    if (server.hasArg("ln")) longName = sanitize(server.arg("ln")).substring(0, 39);
    if (server.hasArg("sn")) shortName = sanitize(server.arg("sn")).substring(0, 4);
    cfgSave(); sendNodeInfo(false);
    sendJson(jStatus());
  });

  server.on("/api/reinit", []() { loraInitialized = false; loraInit(); sendJson(jDiag()); });

  server.onNotFound([]() { server.send_P(200, "text/html; charset=utf-8", PAGE_INDEX); });
  server.begin();
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP / LOOP
// ═══════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n" FW_VERSION);

  // Power the sensor/3V3 rail (CE high) — needed for the radio to come up
  pinMode(PIN_SENSOR_CE, OUTPUT); digitalWrite(PIN_SENSOR_CE, HIGH); delay(100);

  ledInit(); ledColor(0, 0, 40);

  nodeNum = (uint32_t)ESP.getEfuseMac();
  char idb[12]; snprintf(idb, sizeof(idb), "!%08x", nodeNum); nodeId = idb;

  channelsDefault();
  cfgLoad();
  if (!longName.length())  longName  = "SGP-" + String((uint16_t)(nodeNum & 0xFFFF), HEX);
  if (!shortName.length()) shortName = "S" + String((uint8_t)(nodeNum & 0xFF), HEX);

  // SPI + radio
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_LORA_CS);
  Serial.println("Init SX1262...");
  if (loraInit()) { Serial.printf("Radio OK  freq=%.3f bw=%.0f sf=%u cr=%u\n", curFreq, curBw, curSf, curCr); ledColor(0, 60, 0); }
  else { Serial.printf("Radio FAIL: %s\n", loraInitError.c_str()); ledColor(60, 0, 0); }

  // WiFi AP + captive portal (name + optional password from NVS)
  WiFi.mode(WIFI_AP);
  bool apOk;
  if (apPass.length() >= 8) {
    apOk = WiFi.softAP(apSsid.c_str(), apPass.c_str());
    if (!apOk) { Serial.println("[WiFi] WPA2 AP failed, falling back to open"); apOk = WiFi.softAP(apSsid.c_str()); }
  } else {
    apOk = WiFi.softAP(apSsid.c_str());   // open AP (WPA2 needs >=8 chars)
  }
  Serial.printf("[WiFi] softAP %s\n", apOk ? "OK" : "FAILED");
  wifiStarted = true;
  WiFi.setSleep(WIFI_PS_NONE);
  dnsServer.start(53, "*", WiFi.softAPIP());
  setupWeb();
  Serial.printf("AP \"%s\" (%s)  ->  http://%s\n", apSsid.c_str(),
                apPass.length() >= 8 ? "WPA2" : "open", WiFi.softAPIP().toString().c_str());

  beep(2200, 60); beep(3000, 80);
  // announce ourselves and ask the mesh for node info
  if (loraInitialized) { delay(300); sendNodeInfo(true); }

  // Meshtastic BLE — the official app connects here
  bleSetup();
}

unsigned long lastNodeInfo = 0;

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  // RX
  if (loraInitialized && (rxFlag || digitalRead(PIN_LORA_DIO1) == HIGH)) {
    rxFlag = false;
    uint8_t buf[HEADER_LEN + 256];
    int len = radio.getPacketLength();
    if (len > 0 && len <= (int)sizeof(buf)) {
      int st = radio.readData(buf, len);
      if (st == RADIOLIB_ERR_NONE) {
        int rssi = (int)radio.getRSSI();
        float snr = radio.getSNR();
        if (monitorOn) monitorHandleRx(buf, len, rssi, snr);
        else          handleRx(buf, len, rssi, snr);
      } else rxBad++;
    }
    radio.startReceive();
  }

  // super-listen slot hopping
  monitorTick();

  // super SOS beacon sweep
  sosTick();

  // Meshtastic BLE bridge (drains app->radio TX + config handshake)
  bleLoop();

  // deferred reboot (e.g. after saving WiFi) — fires once the HTTP reply is out
  if (rebootAt && millis() > rebootAt) { Serial.println("[SYS] reboot"); delay(50); ESP.restart(); }

  // periodic NodeInfo broadcast (every ~5 min) so we stay visible in the mesh
  // (paused while monitoring so we don't transmit on the slot we're sweeping)
  if (loraInitialized && !monitorOn && millis() - lastNodeInfo > 300000UL) {
    lastNodeInfo = millis();
    sendNodeInfo(false);
  }
}
