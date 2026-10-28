/*
  Rainwise MK-III-LR LoRa link (replaces the XBee 802.15.4 radios).

  One sketch, two roles, chosen at compile time with ROLE:
    ROLE_STATION   reads the station's 32-byte packets (9600 8N1) on UART1
                   (and on USB, for bench replay), checks the CRC and
                   transmits each valid packet unchanged over LoRa.
    ROLE_RECEIVER  receives LoRa packets, checks length, header and CRC, and
                   writes the 32 raw bytes to USB and UART1 at 9600 8N1, the
                   same byte stream an XBee in transparent mode (AP=0) gives.

  Target: RAKwireless RAK4631 (nRF52840 + SX1262) on a RAK19003 base,
  RAK nRF52 Arduino core + RadioLib. Board-specific pins are all in the
  "Board" block so the sketch can move to another SX1262 board.

  References: see lora/README.md.
*/

#include <RadioLib.h>
#include <SPI.h>

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
// Station only: hold the XBee socket's CTS (pin 12) "ready" if the station
// MCU waits for it. -1 = not driven. Active level is unconfirmed (verify).
#define CTS_PIN -1
#define CTS_READY_LEVEL LOW

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

void blink() {
  digitalWrite(LED_PIN, HIGH);
  delay(20);
  digitalWrite(LED_PIN, LOW);
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

Framer uartFramer, usbFramer;
bool transmitting = false;
uint8_t pending[PACKET_LEN];
bool havePending = false;
uint32_t sent = 0, dropped = 0;

void queuePacket(const uint8_t *p, const char *src) {
  if (havePending) dropped++;  // newer reading replaces one not yet sent
  memcpy(pending, p, PACKET_LEN);
  havePending = true;
  if (debugText) {
    Serial.print(src);
    Serial.print(" rx: ");
    printHex(p, PACKET_LEN);
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  if (CTS_PIN >= 0) {
    pinMode(CTS_PIN, OUTPUT);
    digitalWrite(CTS_PIN, CTS_READY_LEVEL);
  }
  Serial.begin(115200);  // USB CDC; baud is ignored
  Serial1.begin(9600);   // UART1 (RX1/TX1 on RAK19003 J7) <- XBee socket DIN, pin 3
  setupRadio();
  Serial.print("station ready, airtime per packet ms: ");
  Serial.println(radio.getTimeOnAir(PACKET_LEN) / 1000.0);
}

void loop() {
  while (Serial1.available())
    if (uartFramer.push(Serial1.read())) queuePacket(uartFramer.buf, "uart");
  while (Serial.available())
    if (usbFramer.push(Serial.read())) queuePacket(usbFramer.buf, "usb");

  if (transmitting && radioIrq) {
    radioIrq = false;
    transmitting = false;
    radio.finishTransmit();
    sent++;
    if (debugText) {
      Serial.print("sent ");
      Serial.print(sent);
      Serial.print(" dropped ");
      Serial.println(dropped);
    }
  }

  if (!transmitting && havePending) {
    havePending = false;
    radioIrq = false;
    int16_t state = radio.startTransmit(pending, PACKET_LEN);
    if (state == RADIOLIB_ERR_NONE) {
      transmitting = true;
      blink();
    } else if (debugText) {
      Serial.print("startTransmit failed ");
      Serial.println(state);
    }
  }
}

// ===================== Receiver =====================
#elif ROLE == ROLE_RECEIVER

uint32_t good = 0, bad = 0;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);  // USB CDC to the NAS/PC: raw packets, like the XBee breakout
  Serial1.begin(9600);   // UART1 TX1 -> display line driver / local XBee
  setupRadio();
  int16_t state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) fail("startReceive", state);
}

void loop() {
  // Typing 'd' in a serial monitor switches between raw bytes and readable text with RSSI/SNR.
  while (Serial.available())
    if (Serial.read() == 'd') debugText = !debugText;

  if (!radioIrq) return;
  radioIrq = false;

  uint8_t p[PACKET_LEN];
  size_t len = radio.getPacketLength();
  int16_t state = (len == PACKET_LEN) ? radio.readData(p, PACKET_LEN)
                                      : RADIOLIB_ERR_PACKET_TOO_LONG;
  bool ok = state == RADIOLIB_ERR_NONE && packetOk(p);
  ok ? good++ : bad++;

  if (ok) {
    Serial1.write(p, PACKET_LEN);
    if (!debugText) Serial.write(p, PACKET_LEN);
    blink();
  }
  if (debugText) {
    Serial.print(ok ? "ok  " : "bad ");
    Serial.print("rssi ");
    Serial.print(radio.getRSSI());
    Serial.print(" snr ");
    Serial.print(radio.getSNR());
    Serial.print(" good ");
    Serial.print(good);
    Serial.print(" bad ");
    Serial.print(bad);
    Serial.print(" len ");
    Serial.print(len);
    if (ok) {
      Serial.print(": ");
      printHex(p, PACKET_LEN);
    } else {
      Serial.println();
    }
  }
  radio.startReceive();
}

#else
#error "ROLE must be ROLE_STATION or ROLE_RECEIVER"
#endif
