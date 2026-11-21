# Parts list

Everything bought for the LoRa replacement, with where it came from and what it cost, so the project has a running total. Prices are what was paid (USD; no sales tax was charged) and will change; check the link for today's price. Shipping is charged per order, so it has its own table below and is included in the totals. Names are as shown on the seller's page.

Some parts come in packs larger than needed. **Bought** is what was ordered, **Used** is what is in the system (or **tool** for test gear that stays on the bench), and the cost of the leftovers is still counted in the total spent.

**TBD** = details not filled in yet.

## System parts

These end up installed: the station transmitter and its adapter, and the receivers in the house.

| Item | Bought | Used | Price paid | Where | Used for | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| [RAKwireless WisBlock Mini Meshtastic Starter Kit US915 (Mini Starter Kit with RAK19003 Base)][rak-kit] (RAK4631 + RAK19003) | 4 | 4 | $31.97 each ($127.88) | Amazon | Station transmitter and the three receivers (Oracle, IP-100, NAS) | |
| [Breakout Board for XBee Module (BOB-08276)][bob-08276] | 4 | 1 | $3.75 each ($15.00) | SparkFun | Station adapter: plugs into the station's XBee socket | |
| [Straight Header - Male (PTH, 2mm, 10-Pin) (PRT-10112)][prt-10112] | 8 | 2 | $1.05 each ($8.40) | SparkFun | Pins on the station breakout | |
| [PW-D Control Buck Converter 6-24V to 5V 1.5A Step-Down Regulator Module Power Inverter Volt Stabilizer][buck] (pack of 4) | 1 pack (4) | 1 | $13.99 per pack | Amazon | Station: battery to 5 V for the RAK | |
| [5 Pcs USB Type C Solderless Connector Screw Terminal Adapter Type C 2 Pin Connector USB-C Breakout Board 5V 3A Male to 2 Pin Terminal Block for Smart Device Power Charging Repair DIY][usbc-plug] | 1 pack (5) | TBD | $8.59 per pack | Amazon | Power into the RAK's USB-C without a cut cable | |
| [RAK19003 Meshtastic Phone Backpack Case][nas-case] by Darkextratoasty (Printables, [CC BY 4.0][cc-by]) | 1 printed | 1 | $0 (filament on hand) | Printables (free model) | NAS receiver enclosure | RAK19003 held by 3 M2.5 screws (included with the RAK kit); snap-on lid; hole for an SMA-to-U.FL antenna cable. Variants: case-mag (M3 nut + magnet), case-zip (zip-tie channel), tall versions, lid with 0.96" OLED cutout. CC BY allows remixes with credit, so it can be the base for the other receiver boxes |
| [Straight Header - Female (PTH, 0.1in., 40-Pin) (PRT-00115)][prt-00115] | 1 | 0 | $1.95 | SparkFun | Spare | Bought in case mounting needed it; not used |
| [Long Header - Male (PTH, 0.1in., 40-Pin) (PRT-10158)][prt-10158] | 1 | 0 | $3.50 | SparkFun | Spare | Bought in case mounting needed it; not used |
| [2.54mm XH Connector Socket Kit with pre-Crimped Cable Wire 2/3/4/5/6/7 Pin Housing JST Adapter Cable Male and Female Compatible with JST-XH Connector (Connector&Socket)][xh-kit] | 1 variety pack | 4-pin sets as needed | $0.00 (on hand) | Amazon | Harness between breakout and RAK (J6 and J7) | Left over from an earlier project (bought 2021, $20.99 per pack); not counted |

## Tools and test gear

Bought for this project but staying on the bench.

| Item | Bought | Used | Price paid | Where | Used for | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| [SparkFun Serial Basic Breakout - CH340C and USB-C (DEV-15096)][dev-15096] | 1 | tool | $10.50 | SparkFun | Feeding test data into the station's UART; reading receiver output | |
| [HiLetgo USB Logic Analyzer Device with EMI Ferrite Ring USB Cable 24MHz 8CH 24MHz 8 Channel UART IIC SPI Debug][logic] | 1 | tool | $12.69 | Amazon | Capturing the Oracle box's RJ11 data (0-5 V inputs only) | |
| [Goupchn SMD IC Test Hook Clips with Silicone Jumper Wires, 10 Colors][hooks] | 1 set | tool | $20.99 | Amazon | Clipping onto header pins and test points | |
| [Waveshare UART Communication Board Supports XBee Connectivity][waveshare-xbee] (CP2102) | 1 | tool | $9.99 | Amazon | Reading the XBees on the PC (XCTU) | Bought 2024, earlier in the project |
| [Pack of 10 pcs 500mA F0.5AL Fast-Blow Fuse 0.5A 0.5amp 250V Ceramic Fuses 0.2 x 0.79 inch / 5 x 20 mm (F0.5A Ceramic)][meter-fuse] | 1 pack (10) | 1 | $4.99 per pack | Amazon | Replacement for the multimeter's mA-range fuse (blown before the station current measurement) | Meter marking: 0.5A F250V, 5x20 mm ceramic. "L" = low breaking capacity: fine for this project's 6 V / 5 V circuits; use an "H" fuse if the meter is ever used on mains |

## Weather station

The existing Rainwise equipment this project works with. Bought before the project as a package; the price isn't known, so none of it is counted in the totals. Names and part numbers are from Rainwise's current product pages, which may differ slightly from older units (**verify** against your own labels).

| Item | Part no. | Role | Notes |
| --- | --- | --- | --- |
| [MK-III RTI-LR Sensor Assembly with Rain Gauge][rw-mk3] | 800-0009 | The outdoor station: sensors, controller, XBee radio | Ours is labeled MK-III-LR; Rainwise rates the XBee link at about 1 mile line of sight. This project replaces its XBee with a RAK4631 |
| [Oracle Multi-Display][rw-oracle] | 801-000 (as shown on the page; **verify**) | Indoor display, fed by its XBee receiver box over a phone cable | Gets its own RAK receiver |
| [IP-100 Network Interface][rw-ip100] | 802-0001 | Internet gateway with its own XBee receiver; serves `status.xml` | Gets its own RAK receiver |
| CUI wall adapter for the Oracle's receiver box | SMI6B-9-4-P5R | Powers the receiver box, and the Oracle through the phone cable | 9 V DC, 0.66 A, regulated switching supply, center positive (label); 5.5 x 2.1 mm right-angle plug (P5R suffix; measured about 5.5 mm outside, 2.0-2.1 mm hole) ([Mouser – SMI6B series][smi6b]) |
| [MK-III, SLA Battery][rw-battery] (Duracell Ultra 6V 5AH AGM SLA, F1 terminals) | 600997 | Station battery, charged by the 0.6 W solar panel | F1 = 0.187" quick-disconnect tabs: the RAK's power tap uses 0.187" piggyback Fastons |

**Older unit (second station, not yet part of the LoRa replacement).** About 15 years older than the MK-III-LR and no longer listed by Rainwise, so there are no links. Also not counted. As recalled, every indoor device has its own external receiver; this unit is at another site, so the details wait for a visit.

| Item | Role | Notes |
| --- | --- | --- |
| Rainwise MK-III (the model before the LR) | Outdoor sensors and controller | 418 MHz radio: a Linx TXM-418-LC transmitter on its own small board, on a 3-wire lead to the main board ([docs/hardware.md](hardware.md)) |
| Rainwise WS-2000 data logger | Logs the station's data; connects to a PC | DB-9 RS-232 to the PC, phone jack for its own external 418 MHz receiver box (**verify**) |
| IP-100 Network Interface | Internet gateway for this station | A second unit, separate from the one above, with its own external 418 MHz receiver |

## Also needed (already on hand)

Not bought for this project, so not counted in the total, but you need them to build it. Prices aren't given; most electronics benches already have these.

| Item | Used for | Notes |
| --- | --- | --- |
| **Tools** | | |
| Soldering iron (here a Pine64 Pinecil, V2 **verify**) | Headers on the RAK19003 and breakout, harness wires | Leaded solder at about 320 °C worked well |
| Multimeter (here a RadioShack 22-807) | Buck output, polarity, continuity, handshake levels, battery checks | |
| Wire strippers and flush cutters | Harness wires; trimming the breakout's bottom tails flush | |
| PC with free USB ports | Flashing and monitoring the boards, logging | Two or three boards at a time is enough for every bench test |
| Calipers | Measuring parts for the printed enclosures | |
| 3D printer (here an Elegoo Neptune 4 Pro) | Receiver enclosures | Several fit-test revisions per design are normal |
| Duplex (splitter) phone adapter | Measuring the Oracle link with the display still connected | |
| RJ11 jack with 4 screw terminals | Breakout for measuring the Oracle box's phone cable | Any surface-mount phone jack works |
| **Consumables** | | |
| Solder and flux | All joints | |
| Hookup wire, five colors plus red | Station harness (green, black, blue, yellow, white; red for +5 V) | Colors in [lora/README.md](../lora/README.md) |
| Kapton or electrical tape | Insulating the underside of the station breakout from the station board | |
| Filament: PLA for indoor boxes, PETG or ASA for anything near the station or in sun | Enclosures | |
| Cable ties and peelable silicone | Strain relief; holding the XH plugs in (XH has no latch) | |
| **Cables and power** | | |
| USB-C **data** cables, one per board on the PC | Flashing and monitoring | A charge-only cable powers the board but the PC never sees it |
| USB-C PD charger that offers 9 V (here the Pine64 USB-C supply sold with the Pinecil; **verify** 9 V on its label) | Option for powering the Oracle receiver box (9 V to the display) through a PD trigger board | The original 9 V barrel adapter also works; the box is planned with both inputs |
| USB charger or power bank with an "always on" mode | Powering a board away from the PC (bench and range tests) | Ordinary power banks shut off at the station's light load |
| **Software** | | |
| [PlatformIO][platformio] | Building and flashing the firmware | Free |
| Python 3 with [pyserial][pyserial] | `rainwise_logger.py`, `replay_packets.py` | Free |
| [PulseView][pulseview] with the sigrok `fx2lafw` firmware | The logic analyzer | Free |
| [Digi XCTU][xctu] | Reading and setting the XBees | Free |
| [Visual Studio Code][vscode] | Editing the code and docs; the PlatformIO and Claude Code extensions run inside it | Free |
| [Claude Code][claude-code] (Anthropic) | AI coding partner: decoding the protocol, writing the firmware, logger and NAS service, the docs, and working through each bench test | Needs a paid Claude plan; not counted |

## Still to order (planned, one combined order)

Not bought yet; links and prices get filled in when ordered. Bought as kits so the Oracle capture result doesn't cause another order.

| Item | For | Notes |
| --- | --- | --- |
| Resistor assortment, 1/4 W | Analyzer input divider, inverter, pull-ups | |
| Transistor + diode assortment (2N3904/2N2222, 1N4148) | Probable inverter to drive the Oracle's data line (T2 idles low, **verify** with a capture) | |
| Schottky diodes, 1 A or more (e.g. 1N5819) | Oracle box: OR-ing the barrel and USB-C 9 V inputs so two plugged-in supplies can't feed each other | About 0.3-0.5 V drop |
| BSS138 4-channel logic level shifter modules | If the Oracle line turns out to be 5 V logic | |
| USB-C PD trigger board, 9 V (fixed or jumper-set) | Oracle box USB-C input | A spare is useful |
| Panel-mount 5.5 × 2.1 mm DC barrel jack, center positive | Oracle box barrel input (original adapter) | |
| RJ11 6P4C screw-terminal jack, or a phone cord to cut | Oracle box connection to the display cable | Keeps the current jack free as a test tool |
| Breadboard + jumper wires | Bench trials before soldering | Skip if on hand |
| 0.187" piggyback Faston terminals | Station battery tap | |
| Waterproof inline ATM (mini blade) fuse holder + 1 A ATM fuses | Station battery tap | |

## Shipping

One line per order.

| Order (seller) | Items in it | Shipping paid |
| --- | --- | --- |
| Amazon | RAK kits | $0.00 |
| SparkFun | Breakouts, headers, DEV-15096 | $9.32 |
| Amazon | Buck converters | $0.00 |
| Amazon | USB-C plugs | $0.00 |
| Amazon | Logic analyzer, test hooks | $0.00 |
| Amazon | Meter fuses | $0.00 |
| Amazon (2024) | Waveshare XBee USB board | $0.00 |

## Totals

| | Amount |
| --- | --- |
| System parts | $179.31 |
| Tools and test gear | $59.16 |
| Shipping | $9.32 |
| **Total spent** | **$247.79** |

## References

- [RAKwireless WisBlock Mini Meshtastic Starter Kit US915 (Amazon)][rak-kit]
- [PW-D buck converter (Amazon)][buck]
- [USB-C screw-terminal plugs (Amazon)][usbc-plug]
- [XH connector kit (Amazon)][xh-kit]
- [HiLetgo logic analyzer (Amazon)][logic], [Goupchn test hook clips (Amazon)][hooks]
- [Waveshare XBee USB board (Amazon)][waveshare-xbee]
- [Meter fuses (Amazon)][meter-fuse]
- [RAK19003 Meshtastic Phone Backpack Case (Printables)][nas-case], licensed [CC BY 4.0][cc-by]
- Software: [PlatformIO][platformio], [pyserial][pyserial], [PulseView][pulseview], [Digi XCTU][xctu], [VS Code][vscode], [Claude Code][claude-code]
- Rainwise: [MK-III RTI-LR][rw-mk3], [Oracle Multi-Display][rw-oracle], [IP-100][rw-ip100], [MK-III SLA battery][rw-battery]
- SparkFun: [BOB-08276][bob-08276], [PRT-10112][prt-10112], [PRT-00115][prt-00115], [PRT-10158][prt-10158], [DEV-15096][dev-15096]

[rak-kit]: https://www.amazon.com/dp/B0DFMMTQZM
[nas-case]: https://www.printables.com/model/714895-rak19003-meshtastic-phone-backpack-case
[cc-by]: https://creativecommons.org/licenses/by/4.0/
[buck]: https://www.amazon.com/dp/B076P4C42B
[usbc-plug]: https://www.amazon.com/dp/B0GYHRM564
[xh-kit]: https://www.amazon.com/dp/B08G188ZQ8
[logic]: https://www.amazon.com/dp/B077LSG5P2
[hooks]: https://www.amazon.com/dp/B0D3ZWTBGC
[waveshare-xbee]: https://www.amazon.com/dp/B0BFFC8WHY
[platformio]: https://platformio.org/
[pyserial]: https://pyserial.readthedocs.io/
[pulseview]: https://sigrok.org/wiki/PulseView
[xctu]: https://www.digi.com/products/embedded-systems/digi-xbee/digi-xbee-tools/xctu
[rw-mk3]: https://rainwise.com/mk-iii-rti-lr-sensor-assembly-with-rain-gauge
[rw-oracle]: https://rainwise.com/oracle-multi-display
[rw-ip100]: https://rainwise.com/ip-100-network-interface
[vscode]: https://code.visualstudio.com/
[claude-code]: https://claude.com/claude-code
[rw-battery]: https://rainwise.com/mk-iii-sla-battery
[meter-fuse]: https://www.amazon.com/dp/B07KZRR3GL
[smi6b]: https://eu.mouser.com/new/cui-inc/cui-smi6b-acdc-power-supply/
[bob-08276]: https://www.sparkfun.com/breakout-board-for-xbee-module.html
[prt-10112]: https://www.sparkfun.com/straight-header-male-pth-2mm-10-pin.html
[prt-00115]: https://www.sparkfun.com/straight-header-female-pth-0-1in-40-pin.html
[prt-10158]: https://www.sparkfun.com/long-header-male-pth-0-1in-40-pin.html
[dev-15096]: https://www.sparkfun.com/sparkfun-serial-basic-breakout-ch340c-and-usb-c.html
