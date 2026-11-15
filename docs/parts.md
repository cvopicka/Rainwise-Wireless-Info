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
| [Straight Header - Female (PTH, 0.1in., 40-Pin) (PRT-00115)][prt-00115] | 1 | 0 | $1.95 | SparkFun | Spare | Bought in case mounting needed it; not used |
| [Long Header - Male (PTH, 0.1in., 40-Pin) (PRT-10158)][prt-10158] | 1 | 0 | $3.50 | SparkFun | Spare | Bought in case mounting needed it; not used |
| [2.54mm XH Connector Socket Kit with pre-Crimped Cable Wire 2/3/4/5/6/7 Pin Housing JST Adapter Cable Male and Female Compatible with JST-XH Connector (Connector&Socket)][xh-kit] | 1 variety pack | 4-pin sets as needed | $0.00 (on hand) | Amazon | Harness between breakout and RAK (J6 and J7) | Left over from an earlier project (bought 2021, $20.99 per pack); not counted |
| Hookup wire | — | — | — | Owner's stock | Harness wires | Not counted |

## Tools and test gear

Bought for this project but staying on the bench.

| Item | Bought | Used | Price paid | Where | Used for | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| [SparkFun Serial Basic Breakout - CH340C and USB-C (DEV-15096)][dev-15096] | 1 | tool | $10.50 | SparkFun | Feeding test data into the station's UART; reading receiver output | |
| [HiLetgo USB Logic Analyzer Device with EMI Ferrite Ring USB Cable 24MHz 8CH 24MHz 8 Channel UART IIC SPI Debug][logic] | 1 | tool | $12.69 | Amazon | Capturing the Oracle box's RJ11 data (0-5 V inputs only) | |
| [Goupchn SMD IC Test Hook Clips with Silicone Jumper Wires, 10 Colors][hooks] | 1 set | tool | $20.99 | Amazon | Clipping onto header pins and test points | |
| [Waveshare UART Communication Board Supports XBee Connectivity][waveshare-xbee] (CP2102) | 1 | tool | $9.99 | Amazon | Reading the XBees on the PC (XCTU) | Bought 2024, earlier in the project |
| RJ11 jack with 4 screw terminals | — | tool | — | Owner's stock | Breakout for measuring the Oracle box's phone cable | Not counted |
| Pine64 Pinecil soldering iron (V2, **verify**) | — | tool | — | Owner's stock | Headers and harness | Not counted |
| RadioShack 22-807 multimeter | — | tool | — | Owner's stock | Voltages, continuity, battery checks | Not counted |

## Shipping

One line per order.

| Order (seller) | Items in it | Shipping paid |
| --- | --- | --- |
| Amazon | RAK kits | $0.00 |
| SparkFun | Breakouts, headers, DEV-15096 | $9.32 |
| Amazon | Buck converters | $0.00 |
| Amazon | USB-C plugs | $0.00 |
| Amazon | Logic analyzer, test hooks | $0.00 |
| Amazon (2024) | Waveshare XBee USB board | $0.00 |

## Totals

| | Amount |
| --- | --- |
| System parts | $179.31 |
| Tools and test gear | $54.17 |
| Shipping | $9.32 |
| **Total spent** | **$242.80** |

## References

- [RAKwireless WisBlock Mini Meshtastic Starter Kit US915 (Amazon)][rak-kit]
- [PW-D buck converter (Amazon)][buck]
- [USB-C screw-terminal plugs (Amazon)][usbc-plug]
- [XH connector kit (Amazon)][xh-kit]
- [HiLetgo logic analyzer (Amazon)][logic], [Goupchn test hook clips (Amazon)][hooks]
- [Waveshare XBee USB board (Amazon)][waveshare-xbee]
- SparkFun: [BOB-08276][bob-08276], [PRT-10112][prt-10112], [PRT-00115][prt-00115], [PRT-10158][prt-10158], [DEV-15096][dev-15096]

[rak-kit]: https://www.amazon.com/dp/B0DFMMTQZM
[buck]: https://www.amazon.com/dp/B076P4C42B
[usbc-plug]: https://www.amazon.com/dp/B0GYHRM564
[xh-kit]: https://www.amazon.com/dp/B08G188ZQ8
[logic]: https://www.amazon.com/dp/B077LSG5P2
[hooks]: https://www.amazon.com/dp/B0D3ZWTBGC
[waveshare-xbee]: https://www.amazon.com/dp/B0BFFC8WHY
[bob-08276]: https://www.sparkfun.com/breakout-board-for-xbee-module.html
[prt-10112]: https://www.sparkfun.com/straight-header-male-pth-2mm-10-pin.html
[prt-00115]: https://www.sparkfun.com/straight-header-female-pth-0-1in-40-pin.html
[prt-10158]: https://www.sparkfun.com/long-header-male-pth-0-1in-40-pin.html
[dev-15096]: https://www.sparkfun.com/sparkfun-serial-basic-breakout-ch340c-and-usb-c.html
