# LoRa link firmware (RAK4631)

Firmware for the XBee replacement planned in the main README ([LoRa replacement plan](../README.md#lora-replacement-plan)). One sketch, two roles:

- **Station:** reads the station's 32-byte packets at 9600 8N1 on UART1, checks header and CRC, and transmits each valid packet unchanged.
- **Receiver:** receives LoRa packets, checks length, header and CRC, and writes the 32 raw bytes to USB and to UART1 at 9600 8N1. That's the same byte stream an XBee in transparent mode (`AP` = 0) delivers, so `rainwise_logger.py` and the NAS service can read it as is.

| File | What |
| --- | --- |
| `platformio.ini` | PlatformIO project: `station` and `receiver` environments, RadioLib version pinned |
| `rainwise_lora/rainwise_lora.ino` | The sketch. Board pins and radio settings are in blocks at the top |
| `rakwireless/` | RAK4631 board definition and variant files from RAK's RAK_PATCH_V2 ([RAK WisBlock PlatformIO guide][rak-pio]), unchanged. The variant files are LGPL-2.1 (inherited from the Arduino and Adafruit cores); see their headers |
| `replay_packets.py` | Bench test: sends packets from a logger CSV to a station-role board over USB |

Status: builds clean for both roles with PlatformIO (nordicnrf52 11.0.0, Adafruit nRF52 framework 1.7.0, RadioLib 7.8.1) and with arduino-cli (RAK nRF52 core 1.3.3). The CRC and framing code was tested on a PC against all 19,235 packets in a logged CSV, with junk bytes inserted and 1 % of the packets corrupted: every good packet was found and every corrupted one rejected. **Not yet run on hardware.**

## Radio settings

Every board must use the same values ([RadioLib][radiolib-sx1262]).

| Setting | Value | Why |
| --- | --- | --- |
| Frequency | 919.0 MHz | Between the LoRaWAN US915 uplink channels (902.3-914.9 MHz) and downlink channels (923.3-927.5 MHz), so it stays out of the way of nearby LoRaWAN gateways ([LoRaWAN Regional Parameters RP002][rp002]) and away from Meshtastic's US default near 906.9 MHz |
| Bandwidth | 500 kHz | FCC 15.247 digital modulation needs a 6 dB bandwidth of at least 500 kHz ([47 CFR 15.247][ecfr-15247]). Whether a LoRa 500 kHz signal measures ≥ 500 kHz at the 6 dB points is **verify** |
| Spreading factor | 10 | Starting point. Each step up gains range and roughly doubles airtime (table below) |
| Coding rate | 4/5 | Least overhead; packets are already CRC-protected end to end |
| Sync word | 0x12 (private) | Keeps Meshtastic (0x2B) and LoRaWAN (0x34) packets from being received |
| Power | +22 dBm | SX1262 maximum ([Semtech SX1262][sx1262]). Under the 15.247 limit of 1 W with antennas up to 6 dBi |
| Current limit | 140 mA | Overcurrent protection for +22 dBm ([Semtech SX1262][sx1262] datasheet) |

**Airtime** for one 32-byte packet at 500 kHz, from the Semtech formula ([Semtech forum][lora-toa]). The station also prints the exact value from RadioLib at startup. One packet goes out every 2 s.

| SF | Airtime | Share of the 2 s interval |
| --- | --- | --- |
| 7 | 18 ms | 0.9 % |
| 9 | 62 ms | 3.1 % |
| 10 | 113 ms | 5.7 % |
| 11 | 206 ms | 10.3 % |
| 12 | 412 ms | 20.6 % |

Receiver sensitivity improves about 2.5 dB per SF step. Widening from 125 to 500 kHz costs about 6 dB (10·log10(4)), because the noise floor rises with bandwidth ([Semtech AN1200.22, LoRa Modulation Basics][an1200-22]). Pick the lowest SF that holds the link at the station's distance with margin; the range test below decides it. More airtime also means more transmit current at the station: at SF12 the transmitter is on 20 % of the time.

**Security:** like the XBee link today (`EE` = 0), packets are not encrypted or authenticated. The CRC catches errors, not forgeries.

## Board pins (RAK4631)

From RAK's core ([RAK-nRF52-Arduino][rak-core], `variants/WisCore_RAK4631_Board/variant.h`, the same pins as `rakwireless/variants/rak4630/variant.h` here) and Meshtastic's RAK4631 variant ([Meshtastic variant.h][mesh-variant]):

| Signal | Pin | Note |
| --- | --- | --- |
| SX1262 NSS / DIO1 / RESET / BUSY | 42 / 47 / 38 / 46 | |
| SX1262 SCK / MISO / MOSI | 43 / 45 / 44 | On its own SPI bus (SPIM2). The core's default `SPI` goes to the WisBlock IO slot, not the radio |
| RF switch power | 37 | Driven HIGH; DIO2 switches TX/RX |
| TCXO | 1.8 V from DIO3 | |
| UART1 RX1 / TX1 | 15 / 16 | RAK19003 header J7 ([RAK19003 datasheet][rak19003]) |
| Green LED | 35 | Blinks once per packet; fast blink = radio failed to start |

`CTS_PIN` (station only) is off (-1) until we know whether the station MCU waits on the XBee's CTS (pin 12) and which level means "ready" (**verify**, open task 1).

## Building and flashing (PlatformIO in VS Code)

PlatformIO downloads the compiler, the Adafruit nRF52 framework, the upload tool (`adafruit-nrfutil`) and the pinned RadioLib version by itself ([PlatformIO `platformio.ini`][pio-ini]). The standard nordicnrf52 platform has no RAK4631 board, so `boards_dir` points at RAK's board files in `rakwireless/` ([RAK WisBlock PlatformIO guide][rak-pio]).

1. The PlatformIO extension only finds a project whose `platformio.ini` is at the top of a workspace folder. Open `lora/` as its own folder, or add it to the workspace with **File > Add Folder to Workspace**.
2. In the PlatformIO sidebar, under **Project Tasks**, pick the **station** or **receiver** environment, then **Build**, **Upload** or **Monitor**. From a terminal in `lora/`:
   ```
   pio run                          # build both roles
   pio run -e station -t upload     # build + flash the station role
   pio run -e receiver -t upload
   pio device monitor               # 115200; keys are sent as you type
   ```
   `pio device list` shows the board's port, usually `/dev/ttyACM0`. Add `--upload-port` if several boards are plugged in. Upload resets the board into its bootloader automatically ("1200 bps touch"). If that fails, double-press the reset button and try again.
3. For the replay script: `pip install pyserial` ([pyserial][pyserial]).

Uploading replaces Meshtastic but keeps RAK's bootloader. To go back, drag the Meshtastic UF2 onto the board's USB drive ([RAK4631 Quick Start][rak4631-qs]).

## Bench test (four kits)

Flash one board as **station** (S) and three as **receivers** (R1-R3). Keep them a few metres apart with antennas attached. Never transmit without an antenna, because it can damage the radio.

1. **First link.** Run `pio device monitor -p <R1 port>` and press `d` to switch R1 to text mode. In a second terminal, from the repo root, run `python lora/replay_packets.py <S port> rainwise_log.csv`. R1 should print an `ok` line with RSSI and SNR every 2 s, and the `bad` count should stay at 0.
2. **Logger end to end.** Put R1 back in raw mode (`d` again, or unplug and replug), close the monitor, and run `python rainwise_logger.py --port /dev/ttyACM1 --no-xml` on it. The rows should match the replayed CSV.
3. **Several receivers.** R2 and R3 should count the same packets as R1 at the same time.
4. **UART1 output.** Connect a 3.3 V USB-serial adapter to R1's TX1 and GND, open it at 9600 8N1, and check that the same 32-byte packets come out. This is the output that will feed the Oracle line driver or a local XBee.
5. **Live station data (optional).** If you have a 3.3 V XBee breakout whose DOUT pin you can reach, wire XBee DOUT to S's RX1 and connect the grounds. S then relays the live station packets over LoRa. Check that both sides are 3.3 V logic first (**verify**).
6. **Range.** Carry a laptop with a receiver in text mode toward and past the station distance. Note RSSI and SNR, then repeat at other SF values (all boards must change together).

## References

- [RadioLib – SX1262 class reference][radiolib-sx1262]: `begin()` parameters (frequency, bandwidth, SF, CR, sync word, power, preamble, TCXO voltage).
- [RAKwireless – RAK-nRF52-Arduino core][rak-core]: board support; `variant.h` gives the UART, LED and default SPI pins.
- [Meshtastic – RAK4631 variant.h][mesh-variant]: SX1262 pins, power-enable pin 37, DIO2 RF switch, 1.8 V TCXO.
- [RAKwireless – RAK19003 datasheet][rak19003]: UART1 on J7, 5 V input limits.
- [RAKwireless – RAK4631 Quick Start][rak4631-qs]: Arduino setup, bootloader, restoring Meshtastic.
- [Semtech – SX1262][sx1262]: datasheet (output power, overcurrent protection, airtime formula).
- [Semtech – AN1200.22 LoRa Modulation Basics][an1200-22]: sensitivity vs. bandwidth and spreading factor (in the documents list on the SX1276 page).
- [Semtech forum – SX126x time-on-air calculation][lora-toa]
- [LoRa Alliance – RP002 Regional Parameters][rp002]: US915 channel plan.
- [eCFR – 47 CFR 15.247][ecfr-15247]: 902-928 MHz rules (500 kHz minimum 6 dB bandwidth, power, antenna gain).
- [RAKwireless – WisBlock PlatformIO guide][rak-pio]: RAK_PATCH_V2 board files used in `rakwireless/`.
- [PlatformIO – platformio.ini][pio-ini]: environments, `boards_dir`, `build_src_filter`, `lib_deps`.
- [Adafruit nRF52 nrfutil][nrfutil]: the upload tool PlatformIO uses for this board.
- [pyserial][pyserial]

[radiolib-sx1262]: https://jgromes.github.io/RadioLib/class_s_x1262.html
[rak-core]: https://github.com/RAKWireless/RAK-nRF52-Arduino
[mesh-variant]: https://github.com/meshtastic/firmware/blob/master/variants/nrf52840/rak4631/variant.h
[rak19003]: https://docs.rakwireless.com/product-categories/wisblock/rak19003/datasheet/
[rak4631-qs]: https://docs.rakwireless.com/product-categories/wisblock/rak4631/quickstart/
[sx1262]: https://www.semtech.com/products/wireless-rf/lora-connect/sx1262
[an1200-22]: https://www.semtech.com/products/wireless-rf/lora-connect/sx1276
[lora-toa]: https://forum.lora-developers.semtech.com/t/sx126x-time-on-air-calculation/127
[rp002]: https://resources.lora-alliance.org/technical-specifications/rp002-1-0-4-regional-parameters
[ecfr-15247]: https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247
[rak-pio]: https://github.com/RAKWireless/WisBlock/blob/master/PlatformIO/README.md
[pio-ini]: https://docs.platformio.org/en/latest/projectconf/index.html
[pyserial]: https://pyserial.readthedocs.io/
[nrfutil]: https://github.com/adafruit/Adafruit_nRF52_nrfutil
