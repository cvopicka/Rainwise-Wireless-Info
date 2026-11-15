/*
  Rainwise MK-III-LR LoRa link (replaces the XBee 802.15.4 radios).

  One sketch, two roles, chosen at compile time with ROLE:
    ROLE_STATION   reads the station's 32-byte packets (115200 8N1, the
                   station XBee's BD=7) on the J7 UART (and on USB, for bench
                   replay), checks the CRC and transmits each valid packet
                   unchanged over LoRa. Stands in for the XBee's pin-sleep
                   handshake (SLEEP_RQ in, CTS out), sends a status message
                   every minute and accepts handshake-mode commands over the
                   air, so the station can be diagnosed without a ladder trip.
    ROLE_RECEIVER  receives LoRa packets, checks length, header and CRC, and
                   writes the 32 raw bytes to USB and the J7 UART at 9600 8N1, the
                   same byte stream an XBee in transparent mode (AP=0) gives.
                   In text mode it shows station status and sends commands.
                   Optional per-board "rain fix" (saved setting) keeps the
                   rain counter on the J7 UART continuous across station restarts,
                   for the Oracle display.

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
#define ROLE_SCANNER 3   // bench diagnostic: channel activity survey
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
// SCL (14) -> CTS. The station never sends on the J7 UART, so its TX pin (J7 pin 2,
// TX0) is released from the UART and drives ON/SLEEP (XBee D9=1 enables it on the
// station XBee, read 2026-11-08). Which lines the station MCU actually uses is
// unconfirmed (verify); the status message and the handshake modes find out.
#define SLEEP_RQ_PIN 13
#define CTS_PIN 14
// UART on the RAK19003's J7 header. From base-board revision D on (our boards
// are Ver.E) J7 carries RX0/TX0 = P0.19/P0.20, which the RAK4631 Arduino BSP
// opens as Serial2 (UARTE1); revision B carried RX1/TX1 = P0.15/P0.16 = Serial1
// (UARTE0). RAK's datasheet still shows the old labels. Found on the bench
// 2026-11-14: Serial1 saw 0 bytes from J7. Build with -DJ7_UART=1 for a rev B
// board.
#ifndef J7_UART
#define J7_UART 2
#endif
#if J7_UART == 2
#define HDR_SERIAL Serial2
#define HDR_TX_PIN PIN_SERIAL2_TX
#define HDR_UARTE NRF_UARTE1
#define HDR_IRQN UARTE1_IRQn
#else
#define HDR_SERIAL Serial1
#define HDR_TX_PIN PIN_SERIAL1_TX
#define HDR_UARTE NRF_UARTE0
#define HDR_IRQN UARTE0_UART0_IRQn
#endif
#ifndef ON_SLEEP_PIN  // bench diagnosis: -DON_SLEEP_PIN=-1 leaves J7 TX on the UART
#define ON_SLEEP_PIN HDR_TX_PIN
#endif

// J7 UART speed per role: the station MCU talks to its XBee at 115200 (BD=7);
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
// modulation (6 dB bandwidth >= 500 kHz; verify). local_config.h can move a
// network to another frequency (keep 915.5-922.5 so the 500 kHz channel
// stays inside the gap); a receiver hears only stations on its own frequency.
#ifndef LORA_FREQ_MHZ
#define LORA_FREQ_MHZ 919.0
#endif
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

// ---- Station ID: the first 2 bytes of every radio message ----
// Like the XBee's PAN ID: every message on the air starts with the ID of the
// station it belongs to (big-endian), so a receiver knows for certain which
// station a packet came from. Commands carry the ID of the station they are
// for. Set STATION_ID in local_config.h; boards of one station all use the
// same ID. It identifies, it doesn't authenticate: data is not encrypted.
#ifndef STATION_ID
#define STATION_ID 0x0000
#endif
const uint16_t MY_STATION_ID = STATION_ID;
const uint8_t ID_LEN = 2;
const uint8_t AIR_MAX = 39;  // ID + 37-byte status message, the longest message

void putId(uint8_t *p, uint16_t id) {
  p[0] = id >> 8;
  p[1] = id & 0xFF;
}
uint16_t getId(const uint8_t *p) { return (uint16_t)p[0] << 8 | p[1]; }

void printId(uint16_t id) {
  for (int shift = 12; shift >= 0; shift -= 4) Serial.print((id >> shift) & 0xF, HEX);
}

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
//   27-28 packets dropped  29-30 channel found busy (total)  31-32 sent
//   without a clear channel (total)  33 power save (0 off, 1 on)
//   34 station battery: byte 15 of the last weather packet, raw (volts =
//   0.0291 x value + 0.20, see README; 0 = no packet yet)  35-36 CRC
// Command, receiver -> station (or -> a receiver with the rain fix on):
//   0-2 "RWC"  3 version  4-7 COMMAND_KEY  8 command  9 argument
//   10 sequence number  11-12 CRC
// Rain report, receiver with the rain fix on -> other receivers, after a
// rain command:
//   0-2 "RWR"  3 version  4 flags (bit0 rain fix on, bit1 baseline known)
//   5 counter sent to the display  6 last station counter  7-8 station
//   restarts detected since boot  9 last command seq  10-13 uptime s  14-15 CRC
const uint8_t MSG_VERSION = 3;  // 3: status gained power save and battery (37 bytes)
const uint8_t STATUS_LEN = 37;
const uint8_t CMD_LEN = 13;
const uint8_t RAIN_LEN = 16;
const uint8_t STATUS_MAGIC[3] = {'R', 'W', 'S'};
const uint8_t CMD_MAGIC[3] = {'R', 'W', 'C'};
const uint8_t RAIN_MAGIC[3] = {'R', 'W', 'R'};
const uint8_t CMD_SET_MODE = 1;     // station: argument = handshake mode
const uint8_t CMD_STATUS = 2;       // station: send a status message now
const uint8_t CMD_RAIN_STATUS = 3;  // rain-fix receiver: send a rain report
const uint8_t CMD_RAIN_RESYNC = 4;  // rain-fix receiver: counter = station's again
const uint8_t CMD_SET_POWER = 5;    // station: argument 0 = power save off, 1 = on
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

using namespace Adafruit_LittleFS_Namespace;

// Small settings files in the nRF52's internal flash (LittleFS).
bool loadByte(const char *path, uint8_t &value) {
  File f(InternalFS);
  bool ok = false;
  if (f.open(path, FILE_O_READ)) {
    ok = f.read(&value, 1) == 1;
    f.close();
  }
  return ok;
}

void saveByte(const char *path, uint8_t value) {
  InternalFS.remove(path);  // FILE_O_WRITE appends
  File f(InternalFS);
  if (f.open(path, FILE_O_WRITE)) {
    f.write(&value, 1);
    f.close();
  }
}

// ===================== Station =====================
#if ROLE == ROLE_STATION

const char *const MODE_FILE = "/hsmode";
const char *const POWER_FILE = "/pwrsave";

Framer uartFramer, usbFramer;
uint8_t pending[PACKET_LEN];
bool havePending = false;
uint32_t sent = 0, dropped = 0, txErrors = 0, packetsIn = 0, uartBytes = 0;

enum RadioState { RS_IDLE, RS_RX, RS_CAD, RS_TX_WEATHER, RS_TX_STATUS, RS_SLEEP };

// ---- Power save (settable over the air, saved in flash, off by default) ----
// Receivers send commands right after hearing the station, so in power save
// the station listens only for LISTEN_WINDOW_MS after each of its own
// transmissions and puts the SX1262 to sleep (settings kept) the rest of the
// time; a command takes about 80 ms on air and arrives within about 0.2 s.
// The main loop also pauses 1 ms per pass so the nRF52 can sleep between
// events (FreeRTOS tickless idle in the Adafruit core). Station data still
// arrives by interrupt into the 64-byte UART buffer, which takes about 5.5 ms
// to fill at 115200, so emptying it every ~1 ms is safe.
const uint32_t LISTEN_WINDOW_MS = 500;
uint8_t powerSave = 0;
uint32_t listenUntilMs = 0;
uint8_t lastBattery = 0;  // byte 15 of the last weather packet (0 = none yet)

// ---- Listen before talk (for several stations on one frequency) ----
// Before each transmission the station waits a random 0-JITTER_MAX_MS (so two
// stations sending every 2 s don't stay locked in step), then runs the
// SX1262's channel activity detection (CAD, Semtech AN1200.85; on SX126x it
// detects a transmission anywhere in the packet, not only the preamble). If another
// LoRa transmission is detected it backs off a random BACKOFF_MIN_MS to
// BACKOFF_MAX_MS and tries again; after LBT_MAX_TRIES it sends anyway, so a
// packet is never held long enough to be replaced by the next one (2 s).
// Worst case about 1.3 s of delay. LBT_ENABLED 0 in local_config.h turns
// the channel check off (the random delay stays).
// Why 400 ms: CAD can't hear a transmission that starts within a few ms of its
// own scan (SF10/500 kHz symbol = 2.05 ms), so two stations whose random
// delays land that close both see a clear channel and collide. With 0-40 ms
// that happened about a third of the time on the bench (two stations in
// lockstep, 2026-11-14); 0-400 ms makes it rare and stays well under 2 s.
#ifndef LBT_ENABLED
#define LBT_ENABLED 1
#endif
// CAD detection threshold (detPeak). RadioLib's default, SF + 13 = 23, comes
// from Semtech's table for 125 kHz; at our 500 kHz the scanner (bench,
// 2026-11-15) measured 0-12 % false "busy" results per check at 23 on good
// boards and 0-0.3 % at 25, with no rise in RSSI (false alarms, not signals).
// One board read 50-70 % at 23 and 29 % at 25: choose the station board with
// the scanner (README, "Choose the station board"). Higher = fewer false alarms
// but a distant station is noticed less often.
#ifndef LBT_DET_PEAK
#define LBT_DET_PEAK 25
#endif
const ChannelScanConfig_t LBT_CAD = {
    .cad = {
        .symNum = RADIOLIB_SX126X_CAD_ON_4_SYMB,
        .detPeak = LBT_DET_PEAK,
        .detMin = RADIOLIB_SX126X_CAD_PARAM_DEFAULT,
        .exitMode = RADIOLIB_SX126X_CAD_PARAM_DEFAULT,
        .timeout = 0,
        .irqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS,
        .irqMask = RADIOLIB_IRQ_CAD_DEFAULT_MASK,
    },
};
const uint16_t JITTER_MAX_MS = 400;
const uint16_t BACKOFF_MIN_MS = 30, BACKOFF_MAX_MS = 150;
const uint8_t LBT_MAX_TRIES = 5;
bool txWaiting = false;  // a send is scheduled for nextTryMs
uint32_t nextTryMs = 0;
uint8_t txTries = 0;
uint32_t channelBusy = 0, sentAnyway = 0;
uint32_t cadStartMs = 0;
const uint32_t CAD_TIMEOUT_MS = 100;  // a CAD takes a few symbols (~2 ms each at SF10/500 kHz)

void sendNext();
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
  uint8_t m;
  if (loadByte(MODE_FILE, m) && m <= HS_RELEASED) mode = m;
}

void saveMode() { saveByte(MODE_FILE, mode); }

void loadPower() {
  uint8_t v;
  if (loadByte(POWER_FILE, v) && v <= 1) powerSave = v;
}

void queuePacket(const uint8_t *p, const char *src) {
  packetsIn++;
  lastBattery = p[14];  // byte 15: station battery (README byte map)
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
  // ON/SLEEP is the XBee's "awake" output: high while awake. It follows the
  // same mode as CTS: mirrors SLEEP_RQ, always "awake", or not driven.
  if (ON_SLEEP_PIN >= 0) {
    if (mode == HS_RELEASED) {
      pinMode(ON_SLEEP_PIN, INPUT);
    } else {
      pinMode(ON_SLEEP_PIN, OUTPUT);
      digitalWrite(ON_SLEEP_PIN, (mode == HS_MIRROR && asleep) ? LOW : HIGH);
    }
  }
}

// HDR_SERIAL.begin() connects both J7 pins to the UARTE, which then owns TX and
// holds it high. The station only receives, so disconnect TX from the UARTE to
// use it as a GPIO (ON/SLEEP). PSEL may only be changed while the UARTE is
// disabled, and RX must be stopped first (nRF52840 Product Specification, UARTE
// chapter). Runs once at startup, before any data arrives.
void releaseHeaderTx() {
  NVIC_DisableIRQ(HDR_IRQN);
  HDR_UARTE->EVENTS_RXTO = 0;
  HDR_UARTE->TASKS_STOPRX = 1;
  while (!HDR_UARTE->EVENTS_RXTO) {
  }
  HDR_UARTE->EVENTS_RXTO = 0;
  HDR_UARTE->EVENTS_ENDRX = 0;
  HDR_UARTE->ENABLE = UARTE_ENABLE_ENABLE_Disabled;
  HDR_UARTE->PSEL.TXD = 0xFFFFFFFF;  // disconnected
  HDR_UARTE->ENABLE = UARTE_ENABLE_ENABLE_Enabled;
  HDR_UARTE->TASKS_STARTRX = 1;  // RXD.PTR and MAXCNT are kept from begin()
  NVIC_ClearPendingIRQ(HDR_IRQN);
  NVIC_EnableIRQ(HDR_IRQN);
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
  put16(s + 29, min(channelBusy, (uint32_t)0xFFFF));
  put16(s + 31, min(sentAnyway, (uint32_t)0xFFFF));
  s[33] = powerSave;
  s[34] = lastBattery;
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
  } else if (c[8] == CMD_SET_POWER && c[9] <= 1) {
    if (c[9] != powerSave) {
      powerSave = c[9];
      saveByte(POWER_FILE, powerSave);
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
    Serial.print(MODE_NAMES[mode]);
    Serial.println(powerSave ? ", power save on" : ", power save off");
  }
}

// Only commands addressed to this station's ID are considered.
void handleReceived() {
  uint8_t a[ID_LEN + CMD_LEN];
  if (radio.getPacketLength() == sizeof(a) && radio.readData(a, sizeof(a)) == RADIOLIB_ERR_NONE &&
      getId(a) == MY_STATION_ID && messageOk(a + ID_LEN, CMD_LEN, CMD_MAGIC))
    handleCommand(a + ID_LEN);
}

// Sends the station ID followed by the message. startTransmit copies the
// bytes into the radio's buffer, so the local array can go out of scope.
bool startTx(const uint8_t *p, size_t len, RadioState next) {
  uint8_t air[AIR_MAX];
  putId(air, MY_STATION_ID);
  memcpy(air + ID_LEN, p, len);
  radioIrq = false;
  int16_t state = radio.startTransmit(air, ID_LEN + len);
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

// Weather first, then status. Called when the channel is clear (or LBT gave up).
void sendNext() {
  txWaiting = false;
  if (havePending) {
    havePending = false;
    if (startTx(pending, PACKET_LEN, RS_TX_WEATHER)) blink();
  } else if (statusDue) {
    uint8_t s[STATUS_LEN];
    buildStatus(s);
    statusDue = false;
    lastStatusMs = millis();
    startTx(s, STATUS_LEN, RS_TX_STATUS);
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  randomSeed(NRF_FICR->DEVICEID[0] ^ NRF_FICR->DEVICEID[1]);  // differs per board
  // XBee inputs have pull-ups by default (PR command); match that on SLEEP_RQ.
  if (SLEEP_RQ_PIN >= 0) pinMode(SLEEP_RQ_PIN, INPUT_PULLUP);
  if (ON_SLEEP_PIN >= 0) pinMode(ON_SLEEP_PIN, OUTPUT);
  InternalFS.begin();
  loadMode();
  loadPower();
  serviceHandshake();
  Serial.begin(115200);              // USB CDC; baud is ignored
  HDR_SERIAL.begin(STATION_UART_BAUD);  // J7 RX0 <- XBee socket DIN, pin 3
  if (ON_SLEEP_PIN == HDR_TX_PIN) releaseHeaderTx();
  serviceHandshake();  // drive ON/SLEEP now that J7 TX is a GPIO
  setupRadio();
  Serial.print("station ready, ID ");
  printId(MY_STATION_ID);
  Serial.print(", ");
  Serial.print(LORA_FREQ_MHZ, 1);
  Serial.print(" MHz, handshake ");
  Serial.print(MODE_NAMES[mode]);
  Serial.print(COMMANDS_ENABLED ? ", commands on" : ", commands OFF (no local_config.h)");
  Serial.print(powerSave ? ", power save on" : ", power save off");
  Serial.print(", airtime per packet ms: ");
  Serial.println(radio.getTimeOnAir(ID_LEN + PACKET_LEN) / 1000.0);
  statusDue = true;  // announce a restart
}

void loop() {
  serviceHandshake();
  serviceLed();
  while (HDR_SERIAL.available()) {
    uartBytes++;
    if (uartFramer.push(HDR_SERIAL.read())) queuePacket(uartFramer.buf, "uart");
  }
  while (Serial.available())
    if (usbFramer.push(Serial.read())) queuePacket(usbFramer.buf, "usb");
  if (millis() - lastStatusMs >= STATUS_INTERVAL_MS) statusDue = true;

  if (radioIrq) {
    radioIrq = false;
    if (radioState == RS_TX_WEATHER || radioState == RS_TX_STATUS) {
      radio.finishTransmit();
      bool wasWeather = radioState == RS_TX_WEATHER;
      radioState = RS_IDLE;
      listenUntilMs = millis() + LISTEN_WINDOW_MS;  // command window (power save)
      if (wasWeather) {
        sent++;
        if (debugText) {
          Serial.print("sent ");
          Serial.print(sent);
          Serial.print(" dropped ");
          Serial.println(dropped);
        }
      }
    } else if (radioState == RS_RX) {
      radioState = RS_IDLE;
      handleReceived();
    } else if (radioState == RS_CAD) {
      radioState = RS_IDLE;
      if (radio.getChannelScanResult() == RADIOLIB_LORA_DETECTED) {
        channelBusy++;
        if (++txTries < LBT_MAX_TRIES) {
          nextTryMs = millis() + random(BACKOFF_MIN_MS, BACKOFF_MAX_MS + 1);
          if (debugText) Serial.println("channel busy, backing off");
        } else {
          sentAnyway++;
          sendNext();
        }
      } else {
        sendNext();
      }
    }
  } else if (radioState == RS_CAD && millis() - cadStartMs > CAD_TIMEOUT_MS) {
    radioState = RS_IDLE;  // CAD never reported back: don't hold the packet
    sendNext();
  }

  // Something to send: schedule it after a random delay, then check the
  // channel (or just send, with LBT off). Otherwise listen for commands
  // (always, or only inside the window after a transmission in power save).
  if (radioState == RS_IDLE || radioState == RS_RX || radioState == RS_SLEEP) {
    if ((havePending || statusDue) && !txWaiting) {
      txWaiting = true;
      txTries = 0;
      nextTryMs = millis() + random(0, JITTER_MAX_MS + 1);
    }
    bool listen = !powerSave || (int32_t)(listenUntilMs - millis()) > 0;
    if (txWaiting && (int32_t)(millis() - nextTryMs) >= 0) {
      radioIrq = false;
      if (radioState == RS_SLEEP) radio.standby();  // wake the SX1262 first
      if (LBT_ENABLED && radio.startChannelScan(LBT_CAD) == RADIOLIB_ERR_NONE) {
        radioState = RS_CAD;
        cadStartMs = millis();
      } else {
        sendNext();
      }
    } else if (listen && radioState != RS_RX) {
      radioIrq = false;
      if (radio.startReceive() == RADIOLIB_ERR_NONE) radioState = RS_RX;
    } else if (!listen && radioState != RS_SLEEP) {
      radioIrq = false;
      radio.sleep(true);  // warm sleep: settings kept
      radioState = RS_SLEEP;
    }
  }
  if (powerSave) delay(1);  // let the nRF52 sleep until the next tick or interrupt
}

// ===================== Receiver =====================
#elif ROLE == ROLE_RECEIVER

uint32_t good = 0, bad = 0;
uint32_t otherGood = 0, otherHeard = 0;  // packets from other stations' IDs
uint16_t lastOtherId = 0;
uint8_t cmdSeq = 0;  // starts at a random value (setup) so a restart can't match an old reply
uint8_t cmd[CMD_LEN];
bool cmdPending = false;  // until confirmed or CMD_MAX_TRIES sends
uint8_t cmdTries = 0;
bool cmdBlindSent = false;
const uint8_t CMD_MAX_TRIES = 5;
uint32_t cmdQueuedMs = 0, lastHeardMs = 0;
uint8_t stationPower = 0xFF;  // power save as last reported by the station (0xFF = unknown)

// ---- Rain-counter fix (a per-board setting, off by default) ----
// The station's rain counter (byte 13) restarts at 0 whenever the station
// powers up. The IP-100 ignores that; the Oracle display counts the drop as
// (new - old) mod 256 tips of rain (docs/protocol.md). With the fix on, this
// receiver keeps its own continuous counter and sends that to the J7 UART (the
// display) in byte 13, with byte 14 and the CRC recomputed. USB output stays
// the station's original bytes, for the logger.
//   Normal packets: add (new - old) mod 256 tips.
//   Station restart: after a gap of more than RAIN_GAP_MS a counter that went
//   down is a restart (count only the tips since it, i.e. the new value);
//   without a gap, more than RAIN_MAX_TIPS in one packet is a restart (10 tips
//   in about 2 s would be 18 in/h).
// The counter is saved in flash, so a power cut doesn't make a jump either.
const char *const RX_CONFIG_FILE = "/rxcfg";  // bit0: rain fix on, bit1: listen to all stations
const char *const RAIN_FILE = "/rainout";
const uint32_t RAIN_GAP_MS = 30000;
const uint8_t RAIN_MAX_TIPS = 10;

// ---- Listen to all stations (a per-board setting, off by default) ----
// Off: only this board's own station (STATION_ID) is heard; everything else is
// counted and dropped. On (for a logger such as the NAS): weather packets from
// every station on this frequency also go to USB, each tagged with its
// station ID. The display output (J7 UART), the rain fix and commands stay
// with the board's own station either way.
bool listenAll = false;

void saveRxConfig();

bool rainFix = false;
uint8_t rainOut = 0;        // counter sent to the display
bool rainOutValid = false;  // false until the first packet sets it
bool rainBaseline = false;  // a station counter has been seen since boot
bool resyncPending = false;
uint8_t rainLastRaw = 0;
uint32_t rainLastMs = 0;
uint16_t rainRestarts = 0;
uint8_t lastRainCmdSeq = 0;
uint8_t rainReport[RAIN_LEN];
bool rainReportPending = false;

void applyRainFix(uint8_t *p) {
  uint8_t raw = p[12];  // byte 13 (1-based) is index 12
  uint32_t now = millis();
  if (!rainOutValid || resyncPending) {
    rainOut = raw;  // start from (or go back to) the station's own count
    rainOutValid = true;
    resyncPending = false;
    saveByte(RAIN_FILE, rainOut);
  } else if (rainBaseline) {
    uint8_t d = raw - rainLastRaw;  // mod 256
    bool longGap = now - rainLastMs > RAIN_GAP_MS;
    bool restart = longGap ? raw < rainLastRaw : d > RAIN_MAX_TIPS;
    uint8_t add = d;
    if (restart) {
      rainRestarts++;
      add = (longGap || raw <= RAIN_MAX_TIPS) ? raw : 0;
    }
    if (add) {
      rainOut += add;
      saveByte(RAIN_FILE, rainOut);
    }
  }  // else: first packet since this board started; just take the baseline
  rainLastRaw = raw;
  rainLastMs = now;
  rainBaseline = true;
  p[12] = rainOut;
  p[13] = 255 - rainOut;  // check byte (byte 14)
  sealMessage(p, PACKET_LEN);
}

void printRainReport(const uint8_t *r) {
  Serial.print("rain fix: ");
  Serial.print(r[4] & 1 ? "on" : "OFF");
  if (r[4] & 2) {
    Serial.print(", display counter ");
    Serial.print(r[5]);
    Serial.print(", station counter ");
    Serial.print(r[6]);
    Serial.print(", offset ");
    Serial.print((uint8_t)(r[5] - r[6]));
  } else {
    Serial.print(", no station packet yet");
  }
  Serial.print(", station restarts seen ");
  Serial.print(get16(r + 7));
  Serial.print(", last rain command ");
  Serial.print(r[9]);
  Serial.print(", up ");
  Serial.print(get32(r + 10));
  Serial.print(" s");
}

void buildRainReport(uint8_t *r) {
  memcpy(r, RAIN_MAGIC, 3);
  r[3] = MSG_VERSION;
  r[4] = (rainFix ? 1 : 0) | (rainBaseline && rainOutValid ? 2 : 0);
  r[5] = rainOut;
  r[6] = rainLastRaw;
  put16(r + 7, rainRestarts);
  r[9] = lastRainCmdSeq;
  put32(r + 10, millis() / 1000);
  sealMessage(r, RAIN_LEN);
}

void doRainResync() {
  if (rainBaseline) {
    rainOut = rainLastRaw;
    rainOutValid = true;
    saveByte(RAIN_FILE, rainOut);
  } else {
    resyncPending = true;  // apply to the next station packet
  }
}

// A rain command heard over the air (sent by another receiver).
void handleRainCommand(const uint8_t *c) {
  if (!rainFix || !COMMANDS_ENABLED || get32(c + 4) != (uint32_t)COMMAND_KEY) return;
  if (c[8] == CMD_RAIN_RESYNC) doRainResync();
  else if (c[8] != CMD_RAIN_STATUS) return;
  lastRainCmdSeq = c[10];
  buildRainReport(rainReport);
  rainReportPending = true;  // sent right after the next station transmission
  if (debugText) {
    Serial.print(c[8] == CMD_RAIN_RESYNC ? "rain resync" : "rain status");
    Serial.print(" by radio, seq ");
    Serial.println(c[10]);
  }
}

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
  Serial.print(", channel busy ");
  Serial.print(get16(s + 29));
  Serial.print(", sent anyway ");
  Serial.print(get16(s + 31));
  Serial.print(", commands ");
  Serial.print(s[5] & 8 ? "on" : "OFF");
  Serial.print(", last command ");
  Serial.print(s[6]);
  Serial.print(", power save ");
  Serial.print(s[33] ? "on" : "off");
  Serial.print(", battery ");
  if (s[34]) {
    Serial.print(0.0291 * s[34] + 0.20, 2);  // byte 15 formula (README byte map)
    Serial.print(" V");
  } else {
    Serial.print("unknown");
  }
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
  cmdTries = 0;
  cmdBlindSent = false;
  cmdQueuedMs = millis();
  Serial.print("command seq ");
  Serial.print(cmdSeq);
  Serial.println(" queued; sent after the next station transmission, repeated until confirmed");
}

// The station listens right after its own transmissions (in power save only
// then), so send right after hearing it. One extra "blind" try after 3 s of
// silence covers a station that is listening but not sending. A command is
// repeated after each station transmission until a reply carries its sequence
// number (station status, or a rain report), up to CMD_MAX_TRIES sends. A
// command goes first; a rain report waits for the next turn.
void sendCommandIfDue(bool justHeard) {
  if (!cmdPending && !rainReportPending) return;
  bool quiet = millis() - lastHeardMs >= 3000 && millis() - cmdQueuedMs >= 3000;
  bool blind = !justHeard && quiet && cmdPending && !cmdBlindSent;
  if (!justHeard && !blind && !(quiet && !cmdPending)) return;
  const uint8_t *msg = cmdPending ? cmd : rainReport;
  size_t len = cmdPending ? CMD_LEN : RAIN_LEN;
  uint8_t air[AIR_MAX];
  putId(air, MY_STATION_ID);  // addressed to this board's own station
  memcpy(air + ID_LEN, msg, len);
  int16_t state = radio.transmit(air, ID_LEN + len);
  radioIrq = false;
  if (cmdPending) {
    cmdTries++;
    if (blind) cmdBlindSent = true;
    Serial.print(state == RADIOLIB_ERR_NONE ? "command sent, seq " : "command send failed, seq ");
    Serial.print(cmd[10]);
    Serial.print(", try ");
    Serial.println(cmdTries);
    if (cmdTries >= CMD_MAX_TRIES) {
      cmdPending = false;
      Serial.println("command not confirmed after 5 tries; type it again");
    }
  } else {
    rainReportPending = false;
    if (debugText) Serial.println(state == RADIOLIB_ERR_NONE ? "rain report sent" : "rain report send failed");
  }
  radio.startReceive();
}

void saveRxConfig() { saveByte(RX_CONFIG_FILE, (rainFix ? 1 : 0) | (listenAll ? 2 : 0)); }

void printSettings() {
  Serial.print("this board: station ID ");
  printId(MY_STATION_ID);
  Serial.print(", ");
  Serial.print(LORA_FREQ_MHZ, 1);
  Serial.print(" MHz SF");
  Serial.print(LORA_SF);
  Serial.print(", listen to ");
  Serial.print(listenAll ? "ALL stations" : "own station only");
  Serial.print(", rain fix ");
  Serial.print(rainFix ? "on" : "off");
  Serial.print(", commands ");
  Serial.print(COMMANDS_ENABLED ? "on" : "OFF");
  Serial.print(", station power save ");
  Serial.print(stationPower == 0xFF ? "unknown" : (stationPower ? "on" : "off"));
  Serial.print(", good ");
  Serial.print(good);
  Serial.print(" bad ");
  Serial.print(bad);
  Serial.print(", other stations: ");
  Serial.print(otherHeard);
  Serial.print(" messages heard");
  if (otherHeard) {
    Serial.print(" (last ID ");
    printId(lastOtherId);
    Serial.print(")");
  }
  Serial.println();
}

// USB output in raw mode: "RWP", the station ID (big-endian), then the 32-byte
// packet. Loggers that only look for 33 80 00 skip the 5-byte tag; the NAS
// service and rainwise_logger.py read it as the station ID.
void writeUsbPacket(uint16_t id, const uint8_t *p) {
  uint8_t tag[5] = {'R', 'W', 'P', (uint8_t)(id >> 8), (uint8_t)(id & 0xFF)};
  Serial.write(tag, sizeof(tag));
  Serial.write(p, PACKET_LEN);
}

// Station status on USB in raw mode, for the NAS service to store as metadata:
// "RWT", the station ID (big-endian), RSSI (dBm, signed byte), SNR (dB x 4,
// signed byte), then the 35-byte status message with its own CRC. 42 bytes.
void writeUsbStatus(uint16_t id, const uint8_t *s) {
  long rssi = constrain(lroundf(radio.getRSSI()), -128L, 127L);
  long snr4 = constrain(lroundf(radio.getSNR() * 4), -128L, 127L);
  uint8_t tag[7] = {'R', 'W', 'T', (uint8_t)(id >> 8), (uint8_t)(id & 0xFF),
                    (uint8_t)(int8_t)rssi, (uint8_t)(int8_t)snr4};
  Serial.write(tag, sizeof(tag));
  Serial.write(s, STATUS_LEN);
}

// Keys that change something important must be pressed twice within 5 s.
char armedKey = 0;
uint32_t armedMs = 0;

bool confirmed(char k, const char *warning) {
  if (armedKey == k && millis() - armedMs < 5000) {
    armedKey = 0;
    return true;
  }
  armedKey = k;
  armedMs = millis();
  Serial.println(warning);
  return false;
}

void handleKey(char k) {
  if (k == 'd') {
    debugText = !debugText;
    if (debugText)
      Serial.println("text mode: 0/1/2 set station handshake, s = status, i = this board's settings, "
                     "r = rain report, R = rain resync, F = rain fix on/off, "
                     "A = listen to all stations on/off, P = station power save on/off, d = raw");
  } else if (!debugText) {
    return;
  } else if (k >= '0' && k <= '2') {
    queueCommand(CMD_SET_MODE, k - '0');
  } else if (k == 's') {
    queueCommand(CMD_STATUS, 0);
  } else if (k == 'r') {
    if (rainFix) {
      buildRainReport(rainReport);
      printRainReport(rainReport);
      Serial.println(" (this board)");
    } else {
      queueCommand(CMD_RAIN_STATUS, 0);  // ask the rain-fix receiver by radio
    }
  } else if (k == 'R') {
    if (!confirmed('R', "press R again within 5 s to resync the display's rain counter to the "
                        "station's. The display will count the difference as rain, so do it "
                        "just before clearing the display's rain totals."))
      return;
    if (rainFix) {
      doRainResync();
      Serial.println(resyncPending ? "rain resync: applies to the next station packet"
                                   : "rain resync done on this board");
    } else {
      queueCommand(CMD_RAIN_RESYNC, 0);
    }
  } else if (k == 'F') {
    if (!confirmed('F', rainFix ? "press F again within 5 s to turn the rain fix OFF on this board "
                                  "(the display then gets the station's counter again)"
                                : "press F again within 5 s to turn the rain fix ON on this board "
                                  "(only the board feeding the Oracle display needs it)"))
      return;
    rainFix = !rainFix;
    saveRxConfig();
    if (rainFix) resyncPending = true;  // start level with the station: no jump
    Serial.println(rainFix ? "rain fix ON (saved)" : "rain fix OFF (saved)");
  } else if (k == 'A') {
    if (!confirmed('A', listenAll ? "press A again within 5 s to hear ONLY this board's own station"
                                  : "press A again within 5 s to also log OTHER stations' weather "
                                    "packets on USB, tagged with their station ID (for a logger)"))
      return;
    listenAll = !listenAll;
    saveRxConfig();
    Serial.println(listenAll ? "listening to ALL stations (saved)" : "own station only (saved)");
  } else if (k == 'P') {
    // Toggle from what the station last reported (unknown counts as off).
    uint8_t target = stationPower == 1 ? 0 : 1;
    if (!confirmed('P', target ? "press P again within 5 s to turn station power save ON"
                               : "press P again within 5 s to turn station power save OFF"))
      return;
    queueCommand(CMD_SET_POWER, target);
  } else if (k == 'i') {
    printSettings();
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  randomSeed(NRF_FICR->DEVICEID[0] ^ NRF_FICR->DEVICEID[1]);
  cmdSeq = random(0, 256);
  InternalFS.begin();
  uint8_t cfg = 0;
  if (loadByte(RX_CONFIG_FILE, cfg)) {
    rainFix = cfg & 1;
    listenAll = cfg & 2;
  }
  rainOutValid = loadByte(RAIN_FILE, rainOut);
  Serial.begin(115200);  // USB CDC to the NAS/PC: raw packets, like the XBee breakout
  HDR_SERIAL.begin(RECEIVER_UART_BAUD);  // J7 TX0 -> display / IP-100
  setupRadio();
  int16_t state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) fail("startReceive", state);
}

void loop() {
  serviceLed();
  // 'd' switches between raw bytes and readable text; the other keys work in text mode.
  while (Serial.available()) handleKey(Serial.read());

  if (!radioIrq) {
    sendCommandIfDue(false);
    return;
  }
  radioIrq = false;

  // Every message starts with a 2-byte station ID (see "Station ID" above).
  uint8_t a[AIR_MAX];
  size_t alen = radio.getPacketLength();
  size_t len = alen > ID_LEN ? alen - ID_LEN : 0;
  bool lenOk = len == PACKET_LEN || len == STATUS_LEN || len == CMD_LEN || len == RAIN_LEN;
  bool rxOk = lenOk && radio.readData(a, alen) == RADIOLIB_ERR_NONE;
  uint16_t id = rxOk ? getId(a) : 0;
  const uint8_t *p = a + ID_LEN;
  bool ok = rxOk && len == PACKET_LEN && packetOk(p);
  bool status = rxOk && len == STATUS_LEN && messageOk(p, STATUS_LEN, STATUS_MAGIC);
  bool otherCmd = rxOk && len == CMD_LEN && messageOk(p, CMD_LEN, CMD_MAGIC);
  bool rainMsg = rxOk && len == RAIN_LEN && messageOk(p, RAIN_LEN, RAIN_MAGIC);
  bool valid = ok || status || otherCmd || rainMsg;
  bool own = valid && id == MY_STATION_ID;

  if (!valid) {
    bad++;
  } else if (!own) {
    otherHeard++;
    lastOtherId = id;
    if (ok && listenAll) {
      otherGood++;
      if (!debugText) writeUsbPacket(id, p);  // USB only; never to the display
    }
    if (status && listenAll && !debugText) writeUsbStatus(id, p);
  } else {
    lastHeardMs = millis();
    if (ok) good++;
    if (status) stationPower = p[33];
    // A reply carrying our sequence number confirms the pending command.
    bool ack = (status && p[6] == cmd[10]) || (rainMsg && p[9] == cmd[10]);
    if (cmdPending && cmdTries > 0 && ack) {
      cmdPending = false;
      Serial.print("command seq ");
      Serial.print(cmd[10]);
      Serial.println(" confirmed");
    }
    if (status && !debugText) writeUsbStatus(id, p);  // metadata for the logger
    if (otherCmd) handleRainCommand(p);
    if (ok) {
      if (!debugText) writeUsbPacket(id, p);  // USB: the station's bytes, tagged
      if (rainFix) {
        uint8_t out[PACKET_LEN];
        memcpy(out, p, PACKET_LEN);
        applyRainFix(out);
        HDR_SERIAL.write(out, PACKET_LEN);  // J7 UART (display): continuous rain counter
      } else {
        HDR_SERIAL.write(p, PACKET_LEN);
      }
      blink();
    }
  }

  // Text mode: own station's messages, plus other stations' when listening to all.
  if (debugText && !otherCmd && (own || !valid || listenAll)) {
    if (valid) {
      Serial.print("[");
      printId(id);
      Serial.print(own ? "] " : " other] ");
    }
    if (status) printStatus(p);
    else if (rainMsg) printRainReport(p);
    else Serial.print(ok ? "ok  " : "bad ");
    Serial.print(" | rssi ");
    Serial.print(radio.getRSSI());
    Serial.print(" snr ");
    Serial.print(radio.getSNR());
    if (!status && !rainMsg) {
      Serial.print(" good ");
      Serial.print(own ? good : otherGood);
      Serial.print(" bad ");
      Serial.print(bad);
      Serial.print(" len ");
      Serial.print(alen);
    }
    if (ok && own && rainFix) {
      Serial.print(" rain ");
      Serial.print(rainLastRaw);
      Serial.print("->");
      Serial.print(rainOut);
    }
    if (ok) {
      Serial.print(": ");
      printHex(p, PACKET_LEN);
    } else {
      Serial.println();
    }
  } else if (debugText && valid && !own && otherHeard == 1) {
    Serial.print("note: heard another station, ID ");
    printId(id);
    Serial.println(" (ignored; press i for counts, A to log it too)");
  }
  radio.startReceive();
  sendCommandIfDue(own);  // only our own station's transmission opens its listen window
}

// ===================== Scanner (bench diagnostic) =====================
#elif ROLE == ROLE_SCANNER

// Surveys what the station's channel check (CAD) hears, without transmitting.
// It cycles through a list of settings, spends DWELL_MS on each running CAD
// back to back, and after each CAD briefly switches to receive to read the
// instantaneous RSSI (signal strength in the 500 kHz channel). One line per
// setting: how often CAD said "busy", and the RSSI overall and at busy moments.
// A real signal shows a raised RSSI when busy; false alarms show the noise
// floor. Station defaults (RadioLib, from Semtech's table for 125 kHz):
// 4 symbols, detPeak = SF + 13 = 23, detMin = 10. The 919 MHz entries with a
// higher detPeak test whether a less sensitive check stops the alarms.
// Output: USB serial, 115200, plain text. Our own station on 919 MHz adds a
// little (one ~0.1 s status message a minute, or a packet every 2 s).
struct ScanSetting {
  float mhz;
  uint8_t detPeak;
};
const ScanSetting SETTINGS[] = {
    {903.0, 23}, {907.0, 23}, {911.0, 23}, {915.0, 23}, {919.0, 23},
    {923.0, 23}, {927.0, 23}, {919.0, 25}, {919.0, 27}, {919.0, 30},
};
const uint8_t N_SETTINGS = sizeof(SETTINGS) / sizeof(SETTINGS[0]);
const uint32_t DWELL_MS = 10000;
uint32_t totalCad[N_SETTINGS], totalBusy[N_SETTINGS];
uint8_t current = 0;
uint32_t sweep = 0;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  setupRadio();
  delay(2000);
  Serial.println("scanner: channel activity survey, SF10 / 500 kHz, sync word ignored by CAD");
  Serial.println("columns: MHz, detPeak, CADs, busy (this dwell), busy % (all sweeps), RSSI avg / max / avg when busy");
}

void loop() {
  const ScanSetting &st = SETTINGS[current];
  radio.setFrequency(st.mhz);
  ChannelScanConfig_t cfg = {
      .cad = {
          .symNum = RADIOLIB_SX126X_CAD_ON_4_SYMB,
          .detPeak = st.detPeak,
          .detMin = RADIOLIB_SX126X_CAD_PARAM_DEFAULT,
          .exitMode = RADIOLIB_SX126X_CAD_PARAM_DEFAULT,
          .timeout = 0,
          .irqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS,
          .irqMask = RADIOLIB_IRQ_CAD_DEFAULT_MASK,
      },
  };
  uint32_t n = 0, busy = 0;
  float rssiSum = 0, rssiMax = -200, busyRssiSum = 0;
  uint32_t start = millis();
  while (millis() - start < DWELL_MS) {
    int16_t r = radio.scanChannel(cfg);
    bool isBusy = r == RADIOLIB_LORA_DETECTED;
    radio.startReceive();
    delay(2);  // let the receiver settle before reading RSSI
    float rssi = radio.getRSSI(false);
    radio.standby();
    n++;
    rssiSum += rssi;
    if (rssi > rssiMax) rssiMax = rssi;
    if (isBusy) {
      busy++;
      busyRssiSum += rssi;
      blink();
    }
    serviceLed();
  }
  totalCad[current] += n;
  totalBusy[current] += busy;
  Serial.print(st.mhz, 1);
  Serial.print(" MHz  peak ");
  Serial.print(st.detPeak);
  Serial.print("  CAD ");
  Serial.print(n);
  Serial.print("  busy ");
  Serial.print(busy);
  Serial.print("  (all sweeps ");
  Serial.print(totalCad[current] ? 100.0 * totalBusy[current] / totalCad[current] : 0, 1);
  Serial.print(" %)  RSSI avg ");
  Serial.print(n ? rssiSum / n : 0, 1);
  Serial.print(" max ");
  Serial.print(rssiMax, 1);
  Serial.print(" busy-avg ");
  if (busy) Serial.print(busyRssiSum / busy, 1);
  else Serial.print("-");
  Serial.println(" dBm");
  if (++current == N_SETTINGS) {
    current = 0;
    Serial.print("--- sweep ");
    Serial.print(++sweep);
    Serial.println(" done ---");
  }
}

#else
#error "ROLE must be ROLE_STATION, ROLE_RECEIVER or ROLE_SCANNER"
#endif
