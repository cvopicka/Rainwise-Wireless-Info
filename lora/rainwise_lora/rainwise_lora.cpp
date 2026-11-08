/*
  Rainwise MK-III-LR LoRa link (replaces the XBee 802.15.4 radios).

  One sketch, two roles, chosen at compile time with ROLE:
    ROLE_STATION   reads the station's 32-byte packets (115200 8N1, the
                   station XBee's BD=7) on UART1 (and on USB, for bench
                   replay), checks the CRC and transmits each valid packet
                   unchanged over LoRa. Stands in for the XBee's pin-sleep
                   handshake (SLEEP_RQ in, CTS out), sends a status message
                   every minute and accepts handshake-mode commands over the
                   air, so the station can be diagnosed without a ladder trip.
    ROLE_RECEIVER  receives LoRa packets, checks length, header and CRC, and
                   writes the 32 raw bytes to USB and UART1 at 9600 8N1, the
                   same byte stream an XBee in transparent mode (AP=0) gives.
                   In text mode it shows station status and sends commands.

  Target: RAKwireless RAK4631 (nRF52840 + SX1262) on a RAK19003 base,
  RAK nRF52 Arduino core + RadioLib. Board-specific pins are all in the
  "Board" block so the sketch can move to another SX1262 board.

  References: see lora/README.md.
*/

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

// Private settings (command key) live in local_config.h, which is gitignored.
// Copy local_config.h.example to create it. Without it, remote commands are off.
#if __has_include("local_config.h")
#include "local_config.h"
#endif

#define ROLE_STATION 1
#define ROLE_RECEIVER 2
#ifndef ROLE
#define ROLE ROLE_RECEIVER
#endif

// ---- Board: RAK4631 (pins from RAK's variant.h and Meshtastic's rak4631 variant) ----
#define LORA_NSS 42
#define LORA_DIO1 47
#define LORA_RESET 38
#define LORA_BUSY 46
#define LORA_SCK 43
#define LORA_MISO 45
#define LORA_MOSI 44
#define LORA_POWER_EN 37   // powers the RF switch; must be HIGH
#define LORA_TCXO_V 1.8    // TCXO supplied from DIO3
#define LED_PIN LED_GREEN  // LED_BUILTIN on RAK4631
// Station only: XBee socket handshake lines, -1 = not connected.
// The station XBee runs pin sleep (SM=1): SLEEP_RQ (socket pin 9) high = sleep,
// low = wake; the XBee then drives CTS (pin 12) low and ON/SLEEP (pin 13) high
// when ready. The RAK19003 only brings these out on its 2.54 mm headers, so the
// I2C pins on J6 (unused here) serve as plain GPIO: SDA (13) <- SLEEP_RQ,
// SCL (14) -> CTS. Which lines the station MCU actually uses is unconfirmed
// (verify); the status message and the handshake modes below find out.
#define SLEEP_RQ_PIN 13
#define CTS_PIN 14
#define ON_SLEEP_PIN -1

// UART1 speed per role: the station MCU talks to its XBee at 115200 (BD=7);
// the receiver XBees output 9600 (BD=3) to the display and IP-100.
#define STATION_UART_BAUD 115200
#define RECEIVER_UART_BAUD 9600

// The core's default SPI is wired to the WisBlock IO slot, not the radio,
// so the SX1262 gets its own bus on the unused SPIM2 peripheral.
SPIClass loraSPI(NRF_SPIM2, LORA_MISO, LORA_SCK, LORA_MOSI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RESET, LORA_BUSY, loraSPI);

// ---- Radio settings: must match on every board ----
// 919.0 MHz sits in the gap between LoRaWAN US915 uplink (902.3-914.9) and
// downlink (923.3-927.5) channels. 500 kHz bandwidth for FCC 15.247 digital
// modulation (6 dB bandwidth >= 500 kHz; verify).
#define LORA_FREQ_MHZ 919.0
#define LORA_BW_KHZ 500.0
#define LORA_SF 10        // higher = longer range, longer airtime
#define LORA_CR 5         // coding rate 4/5
#define LORA_SYNC_WORD RADIOLIB_SX126X_SYNC_WORD_PRIVATE  // 0x12; Meshtastic uses 0x2B
#define LORA_POWER_DBM 22 // SX1262 maximum
#define LORA_PREAMBLE 8
#define LORA_CURRENT_LIMIT_MA 140

// ---- Rainwise packet (README "Packet framing") ----
const uint8_t PACKET_LEN = 32;
const uint8_t HEADER[3] = {0x33, 0x80, 0x00};

// ---- Link messages (ours, never forwarded to the display or logger) ----
// Different lengths from the 32-byte weather packet and their own headers, so
// they can't be mistaken for weather data. Multi-byte fields little-endian;
// CRC-16/XMODEM over everything before it, stored big-endian like the
// weather packet.
//
// Status, station -> receivers, every STATUS_INTERVAL_MS and after a command:
//   0-2 "RWS"  3 version  4 handshake mode  5 flags (bit0 SLEEP_RQ high,
//   bit1 CTS high, bit2 SLEEP_RQ wired, bit3 commands enabled)
//   6 last command seq  7-10 uptime s  11-12 wakes this interval
//   13-14 average awake ms  15-16 longest awake ms  17-20 weather packets
//   received (total)  21-24 UART bytes received (total)  25-26 TX errors
//   27-28 packets dropped  29-30 CRC
// Command, receiver -> station:
//   0-2 "RWC"  3 version  4-7 COMMAND_KEY  8 command  9 argument
//   10 sequence number  11-12 CRC
const uint8_t MSG_VERSION = 1;
const uint8_t STATUS_LEN = 31;
const uint8_t CMD_LEN = 13;
const uint8_t STATUS_MAGIC[3] = {'R', 'W', 'S'};
const uint8_t CMD_MAGIC[3] = {'R', 'W', 'C'};
const uint8_t CMD_SET_MODE = 1;  // argument: handshake mode
const uint8_t CMD_STATUS = 2;    // send a status message now
const uint32_t STATUS_INTERVAL_MS = 60000;

// Station handshake modes (what CTS does), settable over the air.
enum HandshakeMode : uint8_t {
  HS_MIRROR = 0,    // like a real XBee: CTS low while SLEEP_RQ is low (awake)
  HS_CTS_LOW = 1,   // CTS always low (always ready)
  HS_RELEASED = 2,  // CTS not driven (high impedance), as if not connected
};
const char *const MODE_NAMES[] = {"mirror", "cts-low", "released"};

#ifdef COMMAND_KEY
const bool COMMANDS_ENABLED = true;
#else
const bool COMMANDS_ENABLED = false;
#define COMMAND_KEY 0
#endif

// CRC-16/XMODEM, check value 0x31C3 for "123456789"
uint16_t crc16_xmodem(const uint8_t *data, size_t len) {
  uint16_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

// Valid when the header matches and the CRC over bytes 1-30 equals bytes 31-32 (big-endian).
bool packetOk(const uint8_t *p) {
  return memcmp(p, HEADER, 3) == 0 &&
         crc16_xmodem(p, 30) == (((uint16_t)p[30] << 8) | p[31]);
}

// Same check for our own messages: magic, version, CRC in the last two bytes.
bool messageOk(const uint8_t *p, size_t len, const uint8_t *magic) {
  return memcmp(p, magic, 3) == 0 && p[3] == MSG_VERSION &&
         crc16_xmodem(p, len - 2) == (((uint16_t)p[len - 2] << 8) | p[len - 1]);
}

void sealMessage(uint8_t *p, size_t len) {
  uint16_t crc = crc16_xmodem(p, len - 2);
  p[len - 2] = crc >> 8;
  p[len - 1] = crc & 0xFF;
}

void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
uint16_t get16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }

// Keeps the last 32 bytes of a serial stream and reports when they form a valid packet.
struct Framer {
  uint8_t buf[PACKET_LEN];
  uint8_t count = 0;

  bool push(uint8_t b) {
    if (count < PACKET_LEN) {
      buf[count++] = b;
    } else {
      memmove(buf, buf + 1, PACKET_LEN - 1);
      buf[PACKET_LEN - 1] = b;
    }
    if (count == PACKET_LEN && packetOk(buf)) {
      count = 0;
      return true;
    }
    return false;
  }
};

volatile bool radioIrq = false;
void onRadioIrq() { radioIrq = true; }

bool debugText = (ROLE == ROLE_STATION);  // receiver USB stays raw unless toggled

void printHex(const uint8_t *p, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (p[i] < 0x10) Serial.print('0');
    Serial.print(p[i], HEX);
    Serial.print(i + 1 < len ? ' ' : '\n');
  }
}

// Non-blocking LED blink: a delay() here could overflow the 64-byte UART
// receive buffer (32 bytes take under 3 ms at 115200).
uint32_t blinkStartMs = 0;
bool blinkActive = false;

void blink() {
  digitalWrite(LED_PIN, HIGH);
  blinkActive = true;
  blinkStartMs = millis();
}

void serviceLed() {
  if (blinkActive && millis() - blinkStartMs >= 20) {
    digitalWrite(LED_PIN, LOW);
    blinkActive = false;
  }
}

void fail(const char *what, int16_t state) {
  // Halt with a fast blink so a field unit shows it never got on the air.
  while (true) {
    Serial.print(what);
    Serial.print(" failed, RadioLib code ");
    Serial.println(state);
    for (int i = 0; i < 10; i++) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(100);
    }
  }
}

void setupRadio() {
  pinMode(LORA_POWER_EN, OUTPUT);
  digitalWrite(LORA_POWER_EN, HIGH);
  delay(10);
  loraSPI.begin();

  int16_t state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                              LORA_SYNC_WORD, LORA_POWER_DBM, LORA_PREAMBLE,
                              LORA_TCXO_V);
  if (state != RADIOLIB_ERR_NONE) fail("radio.begin", state);
  state = radio.setDio2AsRfSwitch(true);
  if (state != RADIOLIB_ERR_NONE) fail("setDio2AsRfSwitch", state);
  state = radio.setCurrentLimit(LORA_CURRENT_LIMIT_MA);
  if (state != RADIOLIB_ERR_NONE) fail("setCurrentLimit", state);
  radio.setPacketReceivedAction(onRadioIrq);  // same DIO1 line signals TX done
}

// ===================== Station =====================
#if ROLE == ROLE_STATION

using namespace Adafruit_LittleFS_Namespace;
const char *const MODE_FILE = "/hsmode";

Framer uartFramer, usbFramer;
uint8_t pending[PACKET_LEN];
bool havePending = false;
uint32_t sent = 0, dropped = 0, txErrors = 0, packetsIn = 0, uartBytes = 0;

enum RadioState { RS_IDLE, RS_RX, RS_TX_WEATHER, RS_TX_STATUS };
RadioState radioState = RS_IDLE;

uint8_t mode = HS_MIRROR;
uint8_t lastCmdSeq = 0;
bool statusDue = false;
uint32_t lastStatusMs = 0;

// SLEEP_RQ activity since the last status message.
bool sleepRqHigh = true;
uint32_t awakeSinceMs = 0;
uint16_t wakes = 0, awakeMaxMs = 0;
uint32_t awakeTotalMs = 0;

// The handshake mode survives power cycles, so a mode set over the air sticks.
void loadMode() {
  File f(InternalFS);
  uint8_t m;
  if (f.open(MODE_FILE, FILE_O_READ)) {
    if (f.read(&m, 1) == 1 && m <= HS_RELEASED) mode = m;
    f.close();
  }
}

void saveMode() {
  InternalFS.remove(MODE_FILE);  // FILE_O_WRITE appends
  File f(InternalFS);
  if (f.open(MODE_FILE, FILE_O_WRITE)) {
    f.write(&mode, 1);
    f.close();
  }
}

void queuePacket(const uint8_t *p, const char *src) {
  packetsIn++;
  if (havePending) dropped++;  // newer reading replaces one not yet sent
  memcpy(pending, p, PACKET_LEN);
  havePending = true;
  if (debugText) {
    Serial.print(src);
    Serial.print(" rx: ");
    printHex(p, PACKET_LEN);
  }
}

// Track SLEEP_RQ and drive CTS (and ON/SLEEP) according to the handshake mode.
void serviceHandshake() {
  bool high = (SLEEP_RQ_PIN >= 0) && digitalRead(SLEEP_RQ_PIN) == HIGH;
  uint32_t now = millis();
  if (sleepRqHigh && !high) {
    awakeSinceMs = now;  // controller woke the radio
  } else if (!sleepRqHigh && high) {
    uint32_t awake = now - awakeSinceMs;  // and put it back to sleep
    wakes++;
    awakeTotalMs += awake;
    if (awake > awakeMaxMs) awakeMaxMs = min(awake, (uint32_t)0xFFFF);
  }
  sleepRqHigh = high;

  bool asleep = (SLEEP_RQ_PIN >= 0) && high;
  if (CTS_PIN >= 0) {
    if (mode == HS_RELEASED) {
      pinMode(CTS_PIN, INPUT);
    } else {
      pinMode(CTS_PIN, OUTPUT);
      digitalWrite(CTS_PIN, (mode == HS_MIRROR && asleep) ? HIGH : LOW);
    }
  }
  if (ON_SLEEP_PIN >= 0) digitalWrite(ON_SLEEP_PIN, asleep ? LOW : HIGH);
}

void buildStatus(uint8_t *s) {
  memcpy(s, STATUS_MAGIC, 3);
  s[3] = MSG_VERSION;
  s[4] = mode;
  bool ctsHigh = CTS_PIN >= 0 && mode != HS_RELEASED && digitalRead(CTS_PIN) == HIGH;
  s[5] = (sleepRqHigh ? 1 : 0) | (ctsHigh ? 2 : 0) | (SLEEP_RQ_PIN >= 0 ? 4 : 0) |
         (COMMANDS_ENABLED ? 8 : 0);
  s[6] = lastCmdSeq;
  put32(s + 7, millis() / 1000);
  put16(s + 11, wakes);
  put16(s + 13, wakes ? awakeTotalMs / wakes : 0);
  put16(s + 15, awakeMaxMs);
  put32(s + 17, packetsIn);
  put32(s + 21, uartBytes);
  put16(s + 25, min(txErrors, (uint32_t)0xFFFF));
  put16(s + 27, min(dropped, (uint32_t)0xFFFF));
  sealMessage(s, STATUS_LEN);
  wakes = 0;
  awakeTotalMs = 0;
  awakeMaxMs = 0;
}

void handleCommand(const uint8_t *c) {
  if (!COMMANDS_ENABLED || get32(c + 4) != (uint32_t)COMMAND_KEY) return;
  if (c[8] == CMD_SET_MODE && c[9] <= HS_RELEASED) {
    if (c[9] != mode) {
      mode = c[9];
      saveMode();
      serviceHandshake();  // move CTS now, so the reply status shows the new level
    }
  } else if (c[8] != CMD_STATUS) {
    return;
  }
  lastCmdSeq = c[10];
  statusDue = true;  // the reply shows the command took effect
  if (debugText) {
    Serial.print("command ");
    Serial.print(c[8]);
    Serial.print(" seq ");
    Serial.print(c[10]);
    Serial.print(", mode now ");
    Serial.println(MODE_NAMES[mode]);
  }
}

void handleReceived() {
  uint8_t c[CMD_LEN];
  if (radio.getPacketLength() == CMD_LEN && radio.readData(c, CMD_LEN) == RADIOLIB_ERR_NONE &&
      messageOk(c, CMD_LEN, CMD_MAGIC))
    handleCommand(c);
}

bool startTx(const uint8_t *p, size_t len, RadioState next) {
  radioIrq = false;
  int16_t state = radio.startTransmit(p, len);
  if (state != RADIOLIB_ERR_NONE) {
    txErrors++;
    if (debugText) {
      Serial.print("startTransmit failed ");
      Serial.println(state);
    }
    return false;
  }
  radioState = next;
  return true;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  // XBee inputs have pull-ups by default (PR command); match that on SLEEP_RQ.
  if (SLEEP_RQ_PIN >= 0) pinMode(SLEEP_RQ_PIN, INPUT_PULLUP);
  if (ON_SLEEP_PIN >= 0) pinMode(ON_SLEEP_PIN, OUTPUT);
  InternalFS.begin();
  loadMode();
  serviceHandshake();
  Serial.begin(115200);              // USB CDC; baud is ignored
  Serial1.begin(STATION_UART_BAUD);  // UART1 (RX1/TX1 on RAK19003 J7) <- XBee socket DIN, pin 3
  setupRadio();
  Serial.print("station ready, handshake ");
  Serial.print(MODE_NAMES[mode]);
  Serial.print(COMMANDS_ENABLED ? ", commands on" : ", commands OFF (no local_config.h)");
  Serial.print(", airtime per packet ms: ");
  Serial.println(radio.getTimeOnAir(PACKET_LEN) / 1000.0);
  statusDue = true;  // announce a restart
}

void loop() {
  serviceHandshake();
  serviceLed();
  while (Serial1.available()) {
    uartBytes++;
    if (uartFramer.push(Serial1.read())) queuePacket(uartFramer.buf, "uart");
  }
  while (Serial.available())
    if (usbFramer.push(Serial.read())) queuePacket(usbFramer.buf, "usb");
  if (millis() - lastStatusMs >= STATUS_INTERVAL_MS) statusDue = true;

  if (radioIrq) {
    radioIrq = false;
    if (radioState == RS_TX_WEATHER || radioState == RS_TX_STATUS) {
      radio.finishTransmit();
      if (radioState == RS_TX_WEATHER) {
        sent++;
        if (debugText) {
          Serial.print("sent ");
          Serial.print(sent);
          Serial.print(" dropped ");
          Serial.println(dropped);
        }
      }
    } else if (radioState == RS_RX) {
      handleReceived();
    }
    radioState = RS_IDLE;
  }

  // Weather first, then status; otherwise listen for commands.
  if (radioState == RS_IDLE || radioState == RS_RX) {
    if (havePending) {
      havePending = false;
      if (startTx(pending, PACKET_LEN, RS_TX_WEATHER)) blink();
    } else if (statusDue) {
      uint8_t s[STATUS_LEN];
      buildStatus(s);
      statusDue = false;
      lastStatusMs = millis();
      startTx(s, STATUS_LEN, RS_TX_STATUS);
    } else if (radioState == RS_IDLE) {
      radioIrq = false;
      if (radio.startReceive() == RADIOLIB_ERR_NONE) radioState = RS_RX;
    }
  }
}

// ===================== Receiver =====================
#elif ROLE == ROLE_RECEIVER

uint32_t good = 0, bad = 0;
uint8_t cmdSeq = 0;
uint8_t cmd[CMD_LEN];
bool cmdPending = false;
uint32_t cmdQueuedMs = 0, lastHeardMs = 0;

void printStatus(const uint8_t *s) {
  uint8_t m = s[4] <= HS_RELEASED ? s[4] : 0;
  Serial.print("status: up ");
  Serial.print(get32(s + 7));
  Serial.print(" s, handshake ");
  Serial.print(MODE_NAMES[m]);
  Serial.print(", SLEEP_RQ ");
  Serial.print(s[5] & 4 ? (s[5] & 1 ? "high (asleep)" : "low (awake)") : "not wired");
  Serial.print(", CTS ");
  Serial.print(s[4] == HS_RELEASED ? "released" : (s[5] & 2 ? "high" : "low"));
  Serial.print(", wakes ");
  Serial.print(get16(s + 11));
  Serial.print(" (avg ");
  Serial.print(get16(s + 13));
  Serial.print(" ms, max ");
  Serial.print(get16(s + 15));
  Serial.print(" ms), weather packets ");
  Serial.print(get32(s + 17));
  Serial.print(", UART bytes ");
  Serial.print(get32(s + 21));
  Serial.print(", TX errors ");
  Serial.print(get16(s + 25));
  Serial.print(", dropped ");
  Serial.print(get16(s + 27));
  Serial.print(", commands ");
  Serial.print(s[5] & 8 ? "on" : "OFF");
  Serial.print(", last command ");
  Serial.print(s[6]);
}

void queueCommand(uint8_t command, uint8_t arg) {
  if (!COMMANDS_ENABLED) {
    Serial.println("commands off: no local_config.h with COMMAND_KEY");
    return;
  }
  memcpy(cmd, CMD_MAGIC, 3);
  cmd[3] = MSG_VERSION;
  put32(cmd + 4, (uint32_t)COMMAND_KEY);
  cmd[8] = command;
  cmd[9] = arg;
  cmd[10] = ++cmdSeq;
  sealMessage(cmd, CMD_LEN);
  cmdPending = true;
  cmdQueuedMs = millis();
  Serial.print("command seq ");
  Serial.print(cmdSeq);
  Serial.println(" queued; sent after the next station transmission");
}

// The station listens between its own transmissions, so send right after
// hearing it (or after 3 s of silence, when it is only listening).
void sendCommandIfDue(bool justHeard) {
  if (!cmdPending || (!justHeard && millis() - lastHeardMs < 3000 &&
                      millis() - cmdQueuedMs < 3000))
    return;
  cmdPending = false;
  int16_t state = radio.transmit(cmd, CMD_LEN);
  radioIrq = false;
  Serial.print(state == RADIOLIB_ERR_NONE ? "command sent, seq " : "command send failed, seq ");
  Serial.println(cmd[10]);
  radio.startReceive();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);  // USB CDC to the NAS/PC: raw packets, like the XBee breakout
  Serial1.begin(RECEIVER_UART_BAUD);  // UART1 TX1 -> display line driver / local XBee
  setupRadio();
  int16_t state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) fail("startReceive", state);
}

void loop() {
  serviceLed();
  // 'd' switches between raw bytes and readable text. In text mode:
  // '0' mirror, '1' CTS low, '2' CTS released (station handshake), 's' status now.
  while (Serial.available()) {
    char k = Serial.read();
    if (k == 'd') {
      debugText = !debugText;
      if (debugText) Serial.println("text mode: 0/1/2 set station handshake, s = status, d = raw");
    } else if (debugText && k >= '0' && k <= '2') {
      queueCommand(CMD_SET_MODE, k - '0');
    } else if (debugText && k == 's') {
      queueCommand(CMD_STATUS, 0);
    }
  }

  if (!radioIrq) {
    sendCommandIfDue(false);
    return;
  }
  radioIrq = false;
  lastHeardMs = millis();

  uint8_t p[PACKET_LEN];
  size_t len = radio.getPacketLength();
  int16_t state = (len == PACKET_LEN || len == STATUS_LEN || len == CMD_LEN)
                      ? radio.readData(p, len)
                      : RADIOLIB_ERR_PACKET_TOO_LONG;
  bool rxOk = state == RADIOLIB_ERR_NONE;
  bool ok = rxOk && len == PACKET_LEN && packetOk(p);
  bool status = rxOk && len == STATUS_LEN && messageOk(p, STATUS_LEN, STATUS_MAGIC);
  bool otherCmd = rxOk && len == CMD_LEN && messageOk(p, CMD_LEN, CMD_MAGIC);
  if (ok) good++;
  else if (!status && !otherCmd) bad++;

  if (ok) {
    Serial1.write(p, PACKET_LEN);
    if (!debugText) Serial.write(p, PACKET_LEN);
    blink();
  }
  if (debugText && !otherCmd) {
    if (status) printStatus(p);
    else Serial.print(ok ? "ok  " : "bad ");
    Serial.print(" | rssi ");
    Serial.print(radio.getRSSI());
    Serial.print(" snr ");
    Serial.print(radio.getSNR());
    if (!status) {
      Serial.print(" good ");
      Serial.print(good);
      Serial.print(" bad ");
      Serial.print(bad);
      Serial.print(" len ");
      Serial.print(len);
    }
    if (ok) {
      Serial.print(": ");
      printHex(p, PACKET_LEN);
    } else {
      Serial.println();
    }
  }
  radio.startReceive();
  sendCommandIfDue(true);
}

#else
#error "ROLE must be ROLE_STATION or ROLE_RECEIVER"
#endif
