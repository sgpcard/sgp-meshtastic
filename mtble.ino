/*
 * ═══════════════════════════════════════════════════════════════════════════
 *   mtble.ino — Meshtastic BLE Client API for the official app
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *   Implements the Meshtastic phone API over BLE so the official Meshtastic
 *   app (Android / iOS / desktop) can connect to this card over Bluetooth,
 *   see it as a node, read config and send/receive messages.
 *
 *   GATT service 6ba1b218-15a8-461f-9fa8-5dcae273eafd with:
 *     · ToRadio   (WRITE)          — app -> radio protobufs (ToRadio)
 *     · FromRadio (READ)           — radio -> app protobufs (FromRadio), drained
 *     · FromNum   (READ + NOTIFY)  — "new data available" signal
 *
 *   On connect the app writes ToRadio{want_config_id}; we reply with the
 *   standard config sequence (MyNodeInfo, DeviceMetadata, our NodeInfo, the
 *   channels, the Config blocks, then config_complete_id) and finally stream
 *   live MeshPackets. Protobufs are the OFFICIAL nanopb-generated meshtastic
 *   messages (mesh.pb.h etc.), so the wire format matches the real firmware.
 *
 *   Pairing mode: NO_PIN (just-works) for a headless card — the app connects
 *   without a passkey.
 * ═══════════════════════════════════════════════════════════════════════════
 */

#define BLE_MESH_SERVICE_UUID "6ba1b218-15a8-461f-9fa8-5dcae273eafd"
#define BLE_TORADIO_UUID      "f75c76d2-129e-4dad-a1dd-7866124401e7"
#define BLE_FROMRADIO_UUID    "2c55e69e-4993-11ed-b878-0242ac120002"
#define BLE_FROMNUM_UUID      "ed9da18c-a800-4f66-a670-aa7547e34453"

bool bleConnected = false;

static NimBLEServer*         bleSrv = nullptr;
static NimBLECharacteristic* chToRadio = nullptr;
static NimBLECharacteristic* chFromRadio = nullptr;
static NimBLECharacteristic* chFromNum = nullptr;

// ── FromRadio queue (radio -> app). Pre-encoded protobuf blobs. ──
#define FRQ_SLOTS 48
#define FRQ_SIZE  320
struct FRMsg { uint16_t len; uint8_t data[FRQ_SIZE]; };
static FRMsg frq[FRQ_SLOTS];
static volatile int frHead = 0, frTail = 0;   // ring; head=write, tail=read
static SemaphoreHandle_t frMux = nullptr;
static volatile uint32_t fromNumVal = 0;      // incrementing packet counter for FromNum

// ── outgoing app->radio TX requests (filled in BLE task, sent from loop) ──
struct TxReq { uint32_t to; uint8_t ch; uint8_t port; uint16_t plen; uint8_t payload[240]; uint32_t id; };
#define TXQ_SLOTS 8
static TxReq txq[TXQ_SLOTS];
static volatile int txHead = 0, txTail = 0;
static SemaphoreHandle_t txMux = nullptr;

// ── config handshake request (set in BLE task, served from loop) ──
static volatile bool     wantConfig = false;
static volatile uint32_t wantConfigNonce = 0;

// ═══════════════════════════════════════════════════════════════════════════
// Queues
// ═══════════════════════════════════════════════════════════════════════════
static bool frPush(const uint8_t* d, int n) {
  if (n <= 0 || n > FRQ_SIZE) return false;
  bool ok = false;
  if (frMux) xSemaphoreTake(frMux, portMAX_DELAY);
  int next = (frHead + 1) % FRQ_SLOTS;
  if (next != frTail) {               // not full
    frq[frHead].len = n;
    memcpy(frq[frHead].data, d, n);
    frHead = next;
    fromNumVal++;
    ok = true;
  }
  if (frMux) xSemaphoreGive(frMux);
  return ok;
}
static int frPop(uint8_t* out) {       // returns len, 0 if empty
  int n = 0;
  if (frMux) xSemaphoreTake(frMux, portMAX_DELAY);
  if (frTail != frHead) {
    n = frq[frTail].len;
    memcpy(out, frq[frTail].data, n);
    frTail = (frTail + 1) % FRQ_SLOTS;
  }
  if (frMux) xSemaphoreGive(frMux);
  return n;
}
static void notifyFromNum() {
  if (!chFromNum || !bleConnected) return;
  uint32_t v = fromNumVal;
  chFromNum->setValue((uint8_t*)&v, 4);
  chFromNum->notify();
}

// Encode a FromRadio message and push it to the queue
static bool frPushMsg(meshtastic_FromRadio* fr) {
  uint8_t buf[FRQ_SIZE];
  pb_ostream_t s = pb_ostream_from_buffer(buf, sizeof(buf));
  if (!pb_encode(&s, &meshtastic_FromRadio_msg, fr)) return false;
  return frPush(buf, s.bytes_written);
}

// ═══════════════════════════════════════════════════════════════════════════
// Region / preset mapping  (our tables -> meshtastic enums, by name)
// ═══════════════════════════════════════════════════════════════════════════
static meshtastic_Config_LoRaConfig_RegionCode mapRegion() {
  const char* r = REGIONS[regionIdx].name;
  if (!strcmp(r, "US"))      return meshtastic_Config_LoRaConfig_RegionCode_US;
  if (!strcmp(r, "EU_868"))  return meshtastic_Config_LoRaConfig_RegionCode_EU_868;
  if (!strcmp(r, "EU_433"))  return meshtastic_Config_LoRaConfig_RegionCode_EU_433;
  if (!strcmp(r, "ANZ"))     return meshtastic_Config_LoRaConfig_RegionCode_ANZ;
  if (!strcmp(r, "CN"))      return meshtastic_Config_LoRaConfig_RegionCode_CN;
  if (!strcmp(r, "JP"))      return meshtastic_Config_LoRaConfig_RegionCode_JP;
  if (!strcmp(r, "KR"))      return meshtastic_Config_LoRaConfig_RegionCode_KR;
  if (!strcmp(r, "TW"))      return meshtastic_Config_LoRaConfig_RegionCode_TW;
  if (!strcmp(r, "RU"))      return meshtastic_Config_LoRaConfig_RegionCode_RU;
  if (!strcmp(r, "IN"))      return meshtastic_Config_LoRaConfig_RegionCode_IN;
  if (!strcmp(r, "NZ_865"))  return meshtastic_Config_LoRaConfig_RegionCode_NZ_865;
  if (!strcmp(r, "TH"))      return meshtastic_Config_LoRaConfig_RegionCode_TH;
  if (!strcmp(r, "UA_868"))  return meshtastic_Config_LoRaConfig_RegionCode_UA_868;
  if (!strcmp(r, "MY_919"))  return meshtastic_Config_LoRaConfig_RegionCode_MY_919;
  if (!strcmp(r, "SG_923"))  return meshtastic_Config_LoRaConfig_RegionCode_SG_923;
  if (!strcmp(r, "BR_902"))  return meshtastic_Config_LoRaConfig_RegionCode_BR_902;
  return meshtastic_Config_LoRaConfig_RegionCode_UNSET;
}
static meshtastic_Config_LoRaConfig_ModemPreset mapPreset() {
  const char* p = PRESETS[presetIdx].name;
  if (!strcmp(p, "LongFast"))   return meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
  if (!strcmp(p, "LongSlow"))   return meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
  if (!strcmp(p, "LongMod"))    return meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE;
  if (!strcmp(p, "MediumFast")) return meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
  if (!strcmp(p, "MediumSlow")) return meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW;
  if (!strcmp(p, "ShortFast"))  return meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST;
  if (!strcmp(p, "ShortSlow"))  return meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW;
  if (!strcmp(p, "ShortTurbo")) return meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
  if (!strcmp(p, "LongTurbo"))  return meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO;
  return meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
}

// ═══════════════════════════════════════════════════════════════════════════
// Config handshake — build the FromRadio sequence the app expects
// ═══════════════════════════════════════════════════════════════════════════
static void fillUser(meshtastic_User* u) {
  memset(u, 0, sizeof(*u));
  strncpy(u->id, nodeId.c_str(), sizeof(u->id) - 1);
  strncpy(u->long_name, longName.c_str(), sizeof(u->long_name) - 1);
  strncpy(u->short_name, shortName.c_str(), sizeof(u->short_name) - 1);
  u->hw_model = meshtastic_HardwareModel_PRIVATE_HW;
  u->role = meshtastic_Config_DeviceConfig_Role_CLIENT;
}

static void buildConfigSequence(uint32_t nonce) {
  meshtastic_FromRadio fr;

  // 1) MyNodeInfo
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_my_info_tag;
  fr.my_info.my_node_num = nodeNum;
  fr.my_info.reboot_count = 0;
  fr.my_info.min_app_version = 30200;
  fr.my_info.nodedb_count = (uint16_t)(nodeCountN + 1);
  frPushMsg(&fr);

  // 2) DeviceMetadata
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_metadata_tag;
  strncpy(fr.metadata.firmware_version, "2.5.0.sgp", sizeof(fr.metadata.firmware_version) - 1);
  fr.metadata.device_state_version = 24;
  fr.metadata.canShutdown = false;
  fr.metadata.hasWifi = true;
  fr.metadata.hasBluetooth = true;
  fr.metadata.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
  fr.metadata.hw_model = meshtastic_HardwareModel_PRIVATE_HW;
  fr.metadata.hasPKC = false;
  frPushMsg(&fr);

  // 3) Our own NodeInfo
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_node_info_tag;
  fr.node_info.num = nodeNum;
  fr.node_info.has_user = true;
  fillUser(&fr.node_info.user);
  fr.node_info.snr = 0;
  fr.node_info.last_heard = millis() / 1000;
  fr.node_info.has_hops_away = false;
  fr.node_info.is_favorite = true;
  frPushMsg(&fr);

  // 4) Channels 0..7
  for (int i = 0; i < 8; i++) {
    fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_channel_tag;
    fr.channel.index = i;
    fr.channel.has_settings = true;
    meshtastic_ChannelSettings& cs = fr.channel.settings;
    if (i < chanCount) {
      fr.channel.role = (i == 0) ? meshtastic_Channel_Role_PRIMARY : meshtastic_Channel_Role_SECONDARY;
      bool isDefault = (chans[i].keyLen == 16 && memcmp(chans[i].key, DEFAULT_PSK, 16) == 0);
      if (isDefault && chans[i].name == "LongFast") {
        // standard public default channel: name "" + psk {0x01}
        cs.name[0] = 0;
        cs.psk.size = 1; cs.psk.bytes[0] = 0x01;
      } else {
        strncpy(cs.name, chans[i].name.c_str(), sizeof(cs.name) - 1);
        cs.psk.size = chans[i].keyLen;
        memcpy(cs.psk.bytes, chans[i].key, chans[i].keyLen);
      }
    } else {
      fr.channel.role = meshtastic_Channel_Role_DISABLED;
    }
    frPushMsg(&fr);
  }

  // 5) Config blocks
  // device
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_config_tag;
  fr.config.which_payload_variant = meshtastic_Config_device_tag;
  fr.config.payload_variant.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
  frPushMsg(&fr);
  // position (empty defaults)
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_config_tag;
  fr.config.which_payload_variant = meshtastic_Config_position_tag;
  frPushMsg(&fr);
  // power (empty)
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_config_tag;
  fr.config.which_payload_variant = meshtastic_Config_power_tag;
  frPushMsg(&fr);
  // network (empty)
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_config_tag;
  fr.config.which_payload_variant = meshtastic_Config_network_tag;
  frPushMsg(&fr);
  // display (empty)
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_config_tag;
  fr.config.which_payload_variant = meshtastic_Config_display_tag;
  frPushMsg(&fr);
  // lora — the important one
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_config_tag;
  fr.config.which_payload_variant = meshtastic_Config_lora_tag;
  {
    meshtastic_Config_LoRaConfig& lc = fr.config.payload_variant.lora;
    lc.use_preset = true;
    lc.modem_preset = mapPreset();
    lc.region = mapRegion();
    lc.hop_limit = 3;
    lc.tx_enabled = true;
    lc.tx_power = txPower;
    lc.sx126x_rx_boosted_gain = rxBoost;
    lc.channel_num = 0;   // use default slot (name hash)
  }
  frPushMsg(&fr);
  // bluetooth
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_config_tag;
  fr.config.which_payload_variant = meshtastic_Config_bluetooth_tag;
  fr.config.payload_variant.bluetooth.enabled = true;
  fr.config.payload_variant.bluetooth.mode = meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
  fr.config.payload_variant.bluetooth.fixed_pin = 123456;
  frPushMsg(&fr);
  // security (empty)
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_config_tag;
  fr.config.which_payload_variant = meshtastic_Config_security_tag;
  frPushMsg(&fr);

  // 6) Other nodes we've heard (so the app shows the mesh)
  unsigned long now = millis() / 1000;
  for (int i = 0; i < nodeCountN; i++) {
    fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    fr.node_info.num = mnodes[i].num;
    if (mnodes[i].hasUser) {
      fr.node_info.has_user = true;
      memset(&fr.node_info.user, 0, sizeof(fr.node_info.user));
      char idb[16]; snprintf(idb, sizeof(idb), "!%08x", mnodes[i].num);
      strncpy(fr.node_info.user.id, idb, sizeof(fr.node_info.user.id) - 1);
      strncpy(fr.node_info.user.long_name, mnodes[i].longName.c_str(), sizeof(fr.node_info.user.long_name) - 1);
      strncpy(fr.node_info.user.short_name, mnodes[i].shortName.c_str(), sizeof(fr.node_info.user.short_name) - 1);
      fr.node_info.user.hw_model = meshtastic_HardwareModel_UNSET;
      fr.node_info.user.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    }
    fr.node_info.snr = mnodes[i].snr;
    fr.node_info.last_heard = now - mnodes[i].lastHeard;
    fr.node_info.has_hops_away = true;
    fr.node_info.hops_away = mnodes[i].hops;
    frPushMsg(&fr);
  }

  // 7) config complete
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
  fr.config_complete_id = nonce;
  frPushMsg(&fr);

  notifyFromNum();
}

// ═══════════════════════════════════════════════════════════════════════════
// Forward a packet heard over LoRa to the app (called from handleRx / loop ctx)
// ═══════════════════════════════════════════════════════════════════════════
void ble_forwardPacket(uint32_t from, uint32_t to, uint8_t chIdx, uint8_t port,
                       const uint8_t* payload, int plen, int rssi, float snr, uint8_t hops, uint32_t id) {
  if (!bleConnected) return;
  if (plen > (int)sizeof(((meshtastic_Data*)0)->payload.bytes)) plen = sizeof(((meshtastic_Data*)0)->payload.bytes);
  meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_packet_tag;
  meshtastic_MeshPacket& p = fr.packet;
  p.from = from;
  p.to = to;
  p.channel = chIdx;
  p.id = id;
  p.rx_time = millis() / 1000;
  p.rx_snr = snr;
  p.rx_rssi = rssi;
  p.hop_limit = 0;
  p.hop_start = hops;   // app derives hops away
  p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
  p.decoded.portnum = (meshtastic_PortNum)port;
  p.decoded.payload.size = plen;
  memcpy(p.decoded.payload.bytes, payload, plen);
  frPushMsg(&fr);
  notifyFromNum();
}

// ═══════════════════════════════════════════════════════════════════════════
// BLE callbacks
// ═══════════════════════════════════════════════════════════════════════════
class ToRadioCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    NimBLEAttValue v = c->getValue();
    meshtastic_ToRadio tr = meshtastic_ToRadio_init_zero;
    pb_istream_t s = pb_istream_from_buffer((const uint8_t*)v.data(), v.length());
    if (!pb_decode(&s, &meshtastic_ToRadio_msg, &tr)) return;

    if (tr.which_payload_variant == meshtastic_ToRadio_want_config_id_tag) {
      // clear any stale FromRadio backlog, then request the handshake
      if (frMux) xSemaphoreTake(frMux, portMAX_DELAY);
      frHead = frTail = 0;
      if (frMux) xSemaphoreGive(frMux);
      wantConfigNonce = tr.want_config_id;
      wantConfig = true;
    } else if (tr.which_payload_variant == meshtastic_ToRadio_packet_tag) {
      meshtastic_MeshPacket& p = tr.packet;
      if (p.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        // queue it for transmission from the main loop (never TX in BLE ctx)
        if (txMux) xSemaphoreTake(txMux, portMAX_DELAY);
        int next = (txHead + 1) % TXQ_SLOTS;
        if (next != txTail) {
          TxReq& t = txq[txHead];
          t.to = p.to ? p.to : 0xFFFFFFFFUL;
          t.ch = p.channel;
          t.port = (uint8_t)p.decoded.portnum;
          t.plen = p.decoded.payload.size;
          if (t.plen > sizeof(t.payload)) t.plen = sizeof(t.payload);
          memcpy(t.payload, p.decoded.payload.bytes, t.plen);
          t.id = p.id;
          txHead = next;
        }
        if (txMux) xSemaphoreGive(txMux);
      }
    }
    // heartbeat / disconnect: nothing required for a minimal node
  }
};

class FromRadioCB : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    uint8_t out[FRQ_SIZE];
    int n = frPop(out);
    if (n > 0) c->setValue(out, n);
    else       c->setValue((uint8_t*)"", 0);   // empty = nothing left to read
  }
};

class SrvCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    bleConnected = true;
    NimBLEDevice::getServer()->updateConnParams(info.getConnHandle(), 6, 12, 0, 200);
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    bleConnected = false;
    wantConfig = false;
    if (frMux) xSemaphoreTake(frMux, portMAX_DELAY);
    frHead = frTail = 0;
    if (frMux) xSemaphoreGive(frMux);
    NimBLEDevice::startAdvertising();
  }
};

// ═══════════════════════════════════════════════════════════════════════════
// Setup / loop
// ═══════════════════════════════════════════════════════════════════════════
void bleSetup() {
  frMux = xSemaphoreCreateMutex();
  txMux = xSemaphoreCreateMutex();

  String bleName = "SGP Mesh " + String((uint16_t)(nodeNum & 0xFFFF), HEX);
  NimBLEDevice::init(bleName.c_str());
  NimBLEDevice::setMTU(512);
  // NO_PIN pairing: no security callbacks, just-works connection

  bleSrv = NimBLEDevice::createServer();
  bleSrv->setCallbacks(new SrvCB(), true);

  NimBLEService* svc = bleSrv->createService(BLE_MESH_SERVICE_UUID);
  chToRadio = svc->createCharacteristic(BLE_TORADIO_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  chToRadio->setCallbacks(new ToRadioCB());
  chFromRadio = svc->createCharacteristic(BLE_FROMRADIO_UUID, NIMBLE_PROPERTY::READ);
  chFromRadio->setCallbacks(new FromRadioCB());
  chFromNum = svc->createCharacteristic(BLE_FROMNUM_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_MESH_SERVICE_UUID);
  adv->setName(bleName.c_str());
  adv->enableScanResponse(true);
  NimBLEDevice::startAdvertising();

  Serial.printf("[BLE] Meshtastic service up as \"%s\"\n", bleName.c_str());
}

void bleLoop() {
  // serve a pending config handshake (built in loop ctx, safe)
  if (wantConfig) {
    wantConfig = false;
    buildConfigSequence(wantConfigNonce);
  }
  // drain app -> radio TX queue (radio TX only happens here, never in BLE ctx)
  while (true) {
    TxReq t;
    if (txMux) xSemaphoreTake(txMux, portMAX_DELAY);
    bool have = (txTail != txHead);
    if (have) { t = txq[txTail]; txTail = (txTail + 1) % TXQ_SLOTS; }
    if (txMux) xSemaphoreGive(txMux);
    if (!have) break;
    int ci = (t.ch < chanCount) ? t.ch : 0;
    bool ok = meshTx(t.to, t.port, t.payload, t.plen, ci, false, t.id);
    if (ok && t.port == PORT_TEXT) {
      String txt; for (int i = 0; i < t.plen; i++) txt += (char)t.payload[i];
      chatAdd(ci, longName, sanitize(txt), 0, 0, true, 0);
      txNotify();
    }
  }
}
