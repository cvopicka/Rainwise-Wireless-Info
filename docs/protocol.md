# Packet notes and evidence

Supporting detail for the [byte map in the README](../README.md#byte-map): how each field was confirmed, raw captures, the IP-100 XML used for comparison, and the sea-level pressure correction.

## Field notes

- **Temperature, humidity, pressure, wind direction:** match the IP-100 XML on every minute of the 2026-10-25 capture (e.g. 29.4 °F / 75 % / 26.272 vs 26.27 inHg / 337°).
- **Wind speed (09-10):** confirmed 2026-10-25 by spinning the anemometer by hand. Updates in every packet and matched the IP-100 value exactly at each step (1.2 → 2.8 → 0.0 mph). The 2024 mismatch (3.0 vs 5.4) was just a timing difference between packet and snapshot.
- **Wind gust:** not in the packet. The IP-100 tracked a 2.8 mph gust that equals the highest live speed sent, so gust is calculated by the receiving end from the live wind speed.
- **Wind direction (11-12):** confirmed 2026-10-25 by moving the tail: 337° → 135° → 157°, matching the IP-100 and updating live.
- **Rain (13-14):** confirmed 2026-10-25. About 4 oz of water poured into the gauge: byte 13 went `E1` → `E9` (8 tips) and the IP-100 daily rain went 0.00 → 0.08 in. The Oracle YTD went 10.95 → 11.03 in (+0.08). So **0.01 in per tip**, which matches Rainwise's own spec for their rain gauge ([Rainwise rain gauge][rw-gauge]). Byte 14 always equals 255 − byte 13 (`E1`/`1E`, `E9`/`16`, `3D`/`C2`), a simple check byte. Tips show up in the next packet, within about 2 s.
- **Rain counter wrap:** byte 13 is a running 8-bit count, not a daily or YTD total, and rolls over from 255 to 0. Rain amount = (new − old) mod 256 × 0.01 in. The station sends a packet every 2 s, so a wrap can't be missed in practice.
- **Rain counter resets on power-up (2026-11-07):** byte 13 was `23` (35) before the station was switched off and `00` in the first packet after it was switched back on. A logger computing (new − old) mod 256 would read that as 221 tips (2.21 in) of false rain. Any rain total must treat a drop to 0 right after a gap in packets as a restart, not a wrap. One tip followed at 1:01 pm, while the station was being reassembled, probably the gauge being bumped (**verify**). **How the receivers handle it (checked 2026-11-09):** the IP-100 handles the restart correctly: the rain it reported to its hosted weather service shows only 0.01 in on 2026-11-07 (the one bumped tip) and nothing on 2026-11-08, across both station power-offs. The Oracle display does not: its rain totals went up by several inches over the same two power cycles. So the IP-100 ignores the drop (probably by starting a new baseline after a gap in packets, **verify**) and the Oracle adds (new − old) mod 256. The LoRa receiver that feeds the Oracle has a rain-fix setting that hides the restart from it ([lora/README.md](../lora/README.md), "Rain fix for the Oracle display").
- **Byte 15 = IP-100 `station_volts` (confirmed 2026-10-26).** Over an 11-hour daytime log (669 minutes), every value of byte 15 matched exactly one `station_volts` value, and `V = 0.0291 × byte15 + 0.20`, rounded to 0.1 V, reproduced the IP-100 reading in all 669 minutes. It also gives 7.0 V for the 2024 sample (`EB` = 235), matching that XML. Several nearby slope/offset pairs (about 0.029-0.032 V per count) fit equally well, so the exact scale is still approximate; it works out to roughly 0.03 V per step, consistent with an 8-bit ADC reading the supply through a voltage divider ([SparkFun – voltage dividers][sparkfun-div]).
- **Is byte 15 really a battery voltage?** It rose smoothly from 216 to 234 (6.5 → 7.0 V) as the day warmed from 31 to 41 °F and fell back in the evening. Correlation with outside temperature is 0.98 ([Pearson correlation][pearson]). That could be the battery charging from the solar panel during the day, or the reading drifting with temperature ([correlation vs. causation][corr-cause]).
- **Sunlight comparison (2026-10-26): byte 15 does not follow sunlight on the panel.** The MK-III's panel faces true south, tilted 30° from vertical (60° from horizontal) for latitudes 22.5-55° N ([Rainwise MK-III User's Guide][rw-mk3]). A clear-sky model of sunlight on that panel (NOAA sun position + PVEducation direct-beam formula) peaks at solar noon, about 1:12 pm, but byte 15 peaked at 3:36 pm, together with air temperature. At 9:45 am the panel was already getting about half its peak sunlight while byte 15 hadn't moved from 216. Correlation with sunlight is only 0.34, against 0.98 with temperature, and after accounting for temperature what's left of byte 15 goes the *opposite* way to sunlight (−0.50). Conclusion: byte 15 is a voltage reading that drifts with temperature, not a sign of solar charging. Assumes a clear day and no shading of the panel.
- **Battery:** the MK-III uses a 6 V 5 Ah AGM sealed lead-acid battery with a 0.6 W solar panel ([Rainwise MK-III User's Guide][rw-mk3]). A multimeter reading on the battery, compared with byte 15 at the same minute on a cold night and a warm afternoon, will show whether byte 15 is the battery voltage plus temperature drift.
- **First meter reading (2026-10-29, about 5:36 pm, 37-38 °F):** the battery measured **6.92 V** with the station turned off and the solar panel disconnected (no load, no charging), when the station was powered down to read its XBee. The last packet before shutdown had byte 15 = 234 (7.01 V by the formula above), and the first packet after restart, 7 minutes later, had 233 (6.98 V). So byte 15 reads about 0.06-0.09 V (2-3 counts) above the meter here: it is the battery voltage to within a few counts. The exact minute of the meter reading relative to those packets is approximate (**verify**). 6.92 V is well above the resting voltage of a fully charged 6 V lead-acid battery (about 6.3 V after a 24-hour rest ([Battery University BU-903][bu-903])), so even with the panel disconnected the battery still carried surface charge from the day's charging; it had not rested. The night reading is still needed: a battery that has rested for hours without sun shows whether the daytime rise in byte 15 is real battery voltage or measurement drift.
- **Daytime discharge test (2026-11-07):** station off 12:53 pm, panel disconnected 12:55, station back on 12:56 and left running on battery alone until the panel was reconnected at 5:34 pm (after sunset, so no charging before morning). Meter readings at the battery against byte 15 at the same minute (packet times from the logger PC; the watch times may be about a minute off, since the last packet before shutdown is 12:54:01 and the first after restart 12:57:04):

  | Time | Station | Meter | Byte 15 | Formula | Byte 15 − meter | Air temp |
  | --- | --- | --- | --- | --- | --- | --- |
  | 12:56 pm | off | 7.06 V | 236 (first packet after restart) | 7.07 V | +0.01 V | 62 °F |
  | 12:58 pm | running | 7.04 V | 236 → 234 at 12:58:20 | 7.07 / 7.01 V | ±0.03 V | 62 °F |
  | 3:01 pm | running | 6.58 V | 220 | 6.60 V | +0.02 V | 61 °F |
  | 5:33 pm | running | 6.45 V | 218 | 6.54 V | +0.09 V | 47 °F |

  Before the shutdown, with the panel charging, byte 15 read 247-248 (7.39-7.42 V). With the station on battery it fell steadily, 236 → 217 over 4.7 hours, and stayed at 217-218 after the panel was reconnected in the dark. **At about 62 °F the formula matches the meter within one count (0.03 V).** At 47 °F it read 0.09 V high, and the 2026-10-29 reading at 37-38 °F was also 0.06-0.09 V high.

  **The meter cannot resolve differences this small.** All readings were taken with a RadioShack 22-807 pen-style auto-ranging DMM ([UPC listing][rs-22807]). Its owner's manual ([RadioShack 22-807 Owner's Manual][rs-22807-manual]) specifies DC volts as ±1.5 % of reading, ±0.2 % of full scale, ±1 in the last digit. A 6-7 V battery is read on the 40 V range (display `dd.dd V`, 0.01 V steps), so the allowed error at 6.5 V is 0.098 + 0.08 + 0.01 ≈ **±0.19 V**. The manual also gives best accuracy at 64-82 °F and an operating range of 41-104 °F. The 47 °F reading was near the bottom of that range, and the 37-38 °F reading on 2026-10-29 was below it. Every row in the table agrees with byte 15 well inside ±0.19 V, so byte 15 matches the battery within what this meter can show. The apparent "reads high when cold" pattern (0.06-0.09 V) is smaller than the meter's tolerance and comes from readings taken with the meter outside its best-accuracy temperature range, so it is **not supported** by this data. Checking it needs a better meter, or this meter compared indoors against a known voltage reference. Its 10 MΩ input impedance (same manual) draws under 1 µA from the battery, which is negligible. The voltage drop from 7.06 to 6.45 V is mostly surface charge leaving, not a deep discharge; 6.45 V is still about full for a rested 6 V lead-acid battery ([Battery University BU-903][bu-903]).
- **Red light on the main board (2026-11-07, 5:32 pm):** a blinking red LED on the station's main board, not noticed before, was seen at 5:32 pm with the battery at 6.45 V (station running, panel disconnected). Nothing in the packets changed at that time: byte 16 stayed `04` (bit 0 clear) for the whole 5-hour log, bytes 20-23 kept their usual noisy 6.6-7.6 V (they did not lock at 6.47-6.51 V as they do when bit 0 is set), and the IP-100 showed 6.5 V. So the light is a local indicator that is not reported over the radio, or is not a low-voltage warning at all. It is unknown when it started (the board was not watched between 12:58 and 5:32) and what it means (**verify** against the Rainwise manual; check in the morning whether it stops once the panel charges). **Update 2026-11-08:** with the battery charged in daylight, the LED still blinks once every 2 s, the same as the packet interval, and is very hard to see in direct sun. It was probably always there and missed before. Most likely a transmit/activity indicator rather than a fault light (**verify** in the manual).
- **Byte 16:** `04` almost all the time. It switched to `05` (bit 0 set) for nine single minutes during the day, and every time bytes 20-21 and 22-23 had both dropped to about 6.5 V (average 6.50 / 6.49 V flagged vs. 7.04 / 7.01 V not flagged). Most likely a low-voltage warning bit. Not every reading that low was flagged, so the exact rule is unknown. In all 262 flagged packets, bytes 20-21 and 22-23 read only 6.47-6.51 V, while unflagged packets ranged 6.45-7.52 V. So unflagged readings can be just as low, but flagged readings never vary. That fits the station sending a fixed substitute value (or a clamped threshold) while the bit is set, rather than a live measurement (**verify**).
- **Bytes 17-18:** read low byte first (`byte18 × 256 + byte17`) they form a value around 43,700-48,800 (11-hour log) that moves opposite to bytes 20-23 (correlation about −0.4). Read high byte first, byte 17 looks like noise. Probably a raw ADC channel, possibly a reference voltage. Not wind gust: it only changes at the once-a-minute update and didn't react to the anemometer test.
- **Bytes 20-21 and 22-23:** originally labeled battery and second supply voltage because the first sample (7.08 V) happened to be close to the IP-100's 7.0 V. The daytime log shows the IP-100's value actually comes from byte 15. These two jump around a lot, 6.45-7.6 V with typical minute-to-minute changes of 0.2-0.26 V, far noisier than a real battery, and are not tied to temperature or time of day. They partly move together (correlation 0.53) and move opposite to bytes 17-18 (about −0.4). Possibly voltages measured under load (for example while the radio transmits) or raw charging-circuit readings. Not solar panel voltage: they read 6.7-7.1 V at night. Still unidentified.
- **Bytes 19 and 24-30:** zero in every packet of the 11-hour log. Probably slots for sensors this station doesn't have (solar radiation, UV, extra probes).
- **Pressure is station pressure.** The value in bytes 7-8 is the actual air pressure at the station, not corrected to sea level. At higher elevations it reads well below the ~30 inHg people expect. See [Sea-level pressure](#sea-level-pressure).
- **IP-100 `station_volts`:** solved, it's byte 15 (see above).
- **Header:** may contain a station ID or type. Can't test against a second station (the other Rainwise transmitter is not XBee).

## RAW samples

2024-03-23 (original sample, matches XML snapshot below):

```
33 80 00 01 2B 4A 64 9B 00 1E 00 2D 3D C2 EB 04 B4 B4 00 02 C4 02 A6 00 00 00 00 00 00 00 AC 5E
```

2026-10-25 night capture (calm, dry, dark), one packet per minute:

```
21:09:57  33 80 00 01 28 4B 66 9D 00 00 01 51 E1 1E D9 04 BC B3 00 02 C3 02 A2 00 00 00 00 00 00 00 66 18
21:10:29  33 80 00 01 28 4B 66 9D 00 00 01 51 E1 1E D8 04 80 B2 00 02 CF 02 B8 00 00 00 00 00 00 00 8D 1F
21:11:29  33 80 00 01 27 4B 66 A3 00 00 01 51 E1 1E D8 04 EE B1 00 02 D1 02 BB 00 00 00 00 00 00 00 31 82
21:12:29  33 80 00 01 26 4B 66 9D 00 00 01 51 E1 1E D9 04 B4 B3 00 02 C6 02 A8 00 00 00 00 00 00 00 34 F7
21:13:35  33 80 00 01 26 4B 66 A0 00 00 01 51 E1 1E D9 04 C2 B3 00 02 C3 02 A2 00 00 00 00 00 00 00 B5 BC
21:14:29  33 80 00 01 25 4B 66 9D 00 00 01 51 E1 1E D8 04 D6 B2 00 02 D3 02 C3 00 00 00 00 00 00 00 70 71
```

IP-100 at 21:14:22: 29.3 °F, 75 %, 26.27 inHg, wind 0.0 @ 337°, gust 0.0, rain 0.00, inside 64.0 °F, station_volts 6.5.

2026-10-25 hands-on test (anemometer spun, tail moved, ~4 oz water poured in):

```
                                         spd   dir   rain
21:28:29  33 80 00 01 1D 4B 66 9D 00 1C 01 51 E1 1E ...   2.8 mph  337°  IP-100 gust 2.8
21:29:45  33 80 00 01 1C 4B 66 A0 00 00 00 87 E1 1E ...   0.0      135°
21:29:50  33 80 00 01 1C 4B 66 A0 00 00 00 9D E1 1E ...   0.0      157°
21:30:05  ... 00 00 00 9D E3 1C ...                                       IP-100 rain 0.02
21:30:07  ... 00 00 00 9D E5 1A ...                                       0.04
21:30:09  ... 00 00 00 9D E7 18 ...                                       0.06
21:30:11  ... 00 00 00 9D E8 17 ...                                       0.07
21:30:13  33 80 00 01 1C 4B 66 A0 00 00 00 9D E9 16 D8 04 99 B2 00 02 CC 02 B1 00 00 00 00 00 00 00 6A FB   0.08
```

## Closest XML Snapshot (2024-03-23)

```XML
<?xml version="1.0" encoding="ISO-8859-1"?>
<!-- Weather and hardware paramters for the IP-100, min's, max's and accumulations are for the current day -->
<status>
 <hardware>
   <serial_number></serial_number>
   <firmware_version>2083</firmware_version>
   <clock>2024/03/23 12:06:48</clock>
   <station_volts>7.0</station_volts>
   <network>
     <MAC_address></MAC_address>
     <IP_address></IP_address>
  <subnet></subnet>
  <gateway></gateway>
   </network>
   <interval>1</interval>
   <base_units>English</base_units>
   <speed_units>mph</speed_units>
 </hardware>

 <weather>
 <temperature_outside>
  <current>29.9</current>
  <max>30.0</max>
  <min>26.3</min>
 </temperature_outside>

 <humidity>
  <current>74</current>
  <max>74</max>
  <min>67</min>
 </humidity>

 <pressure>
  <current>25.76</current>
  <max>25.95</max>
  <min>25.76</min>
 </pressure>

 <precipitation>
  <current>0.00</current>
 </precipitation>

 <wind>
  <speed>5.4</speed>
  <direction>45</direction>
  <gust_speed>7.2</gust_speed>
  <gust_direction>45</gust_direction>
 </wind>

 <temperature_inside>
  <current>60.0</current>
  <max>62.0</max>
  <min>60.0</min>
 </temperature_inside>
 </weather>
</status>
```

The IP-100 serves its status XML live at `http://<ip100-address>/status.xml` (replace with your IP-100's address on your network).

## Sea-level pressure

The station sends **station pressure**: the real air pressure where the barometer sits. Weather reports, airports and most weather websites show pressure **corrected to sea level**, so stations at different elevations can be compared. At a few thousand feet the difference is about 4 inHg, so an uncorrected reading looks "wrong" next to the local forecast even though it isn't.

Both tools add a `baro_sl_inhg` value alongside the raw `baro_inhg`. It uses the NWS **altimeter setting** formula, the same correction airports report in METARs ([NWS formula sheet][nws-alt]):

```
P  = station pressure in hPa (inHg × 33.8639)
h  = barometer elevation in meters (feet × 0.3048)
n  = 0.190284

NWS form:   A = (P − 0.3) × (1 + ((1013.25^n × 0.0065 / 288) × h / (P − 0.3)^n))^(1/n)
Same thing: A = ((P − 0.3)^n + k × h)^(1/n),   k = 1013.25^n × 0.0065 / 288 ≈ 8.42288e-5

sea-level inHg = A / 33.8639 + offset
```

What the constants are:

- **1013.25 hPa** (29.92 inHg): standard sea-level pressure.
- **0.0065 K/m**: standard temperature lapse rate (temperature drops 6.5 °C per km of height).
- **288 K** (15 °C): standard sea-level temperature, rounded. The exact standard-atmosphere value is 288.15 K; using it changes the result by about 0.002 inHg.
- **n = 0.190284**: comes from the physics of the standard atmosphere (lapse rate × gas constant ÷ (gravity × molar mass of air)).
- **0.3 hPa**: a small allowance NWS uses because the barometer reading isn't taken at exactly the reference height.

Because it uses the *standard* atmosphere instead of today's actual temperature, it doesn't swing with outside temperature. That's the main difference from "mean sea-level pressure" (MSLP), which uses the station's recent temperatures and can be unreliable at high elevations ([NWS pressure definitions][nws-defs]).

**Setup:**

1. Find the elevation of the barometer itself, in feet (the Oracle display or IP-100 box, wherever the pressure sensor is, not the ground at the mast). A GPS, a topo map or an online elevation lookup works.
2. Logger: `--elevation-ft <feet>` or set `RAINWISE_ELEVATION_FT`. Service: `ELEVATION_FT` in `docker-compose.yml`.
3. Calibrate: compare `baro_sl_inhg` with the altimeter setting from the nearest airport METAR on a calm day with steady pressure. In a METAR the altimeter setting is the `A` group, e.g. `A3028` = 30.28 inHg ([METAR format][metar]). ASOS stations send their hourly report a few minutes before the top of the hour, usually at :53 ([NWS observations FAQ][nws-obs]), so compare readings from that minute. Put the difference in `--baro-offset` / `BARO_OFFSET`. Small differences (±0.03 inHg) are normal sensor tolerance.

The elevation is kept out of the repo on purpose: it's a setting, not a value in the code. With elevation `0` no correction is applied.

Raw station pressure is always stored too, so the correction can be redone later if the elevation or offset changes.

## References

**Pressure**

- [NWS – Altimeter Setting formula][nws-alt] (PDF): the formula used for `baro_sl_inhg`.
- [NWS Boulder – Pressure definitions][nws-defs]: station pressure vs. sea-level pressure vs. altimeter setting, in plain language.
- [NWS – Pressure Altitude formula][nws-palt] (PDF): companion sheet, the reverse problem (what altitude a pressure corresponds to).
- [Wikipedia – METAR][metar]: how to read airport weather reports, including the `A` (altimeter) group.
- [NWS Chicago – Weather observations FAQ][nws-obs]: when ASOS stations send their hourly reports.

**Data analysis**

- [Wikipedia – Pearson correlation coefficient][pearson]: the "correlation" numbers used above (−1 to +1).
- [Mathwords – Correlation vs. causation][corr-cause]: why two things rising together (like byte 15 and temperature) doesn't prove one causes the other.
- [SparkFun – Voltage dividers][sparkfun-div]: how a circuit scales a higher voltage down to what an ADC can read, which is where a "volts per count" scale comes from.

**Hardware and software**

- [Rainwise – Rain gauge][rw-gauge]: 0.01 in per tip.
- [Battery University – BU-903 How to Measure State-of-charge][bu-903]: lead-acid open-circuit voltage vs. state of charge (6 V ≈ 6.32 V full after 24 h rest), and surface charge.
- [Buycott – RadioShack 22-807 pen-style DMM (UPC 040293150419)][rs-22807]: identifies the owner's meter.
- RadioShack 22-807 Pen Style Digital Multimeter Owner's Manual (Tandy Corporation, 2000, part 811082310A; copy supplied by the owner, printed copies sold by [SMC Electronics][rs-22807-manual]): DC volts ±1.5 % of reading, ±0.2 % of full scale, ±1 digit; ranges 400 mV/4/40/400 V; 10 MΩ input; best accuracy at 64-82 °F, operating range 41-104 °F.
- [Rainwise – MK-III Weather Stations User's Guide][rw-mk3] (PDF): solar panel direction and tilt by latitude, battery spec.

**Sun position and sunlight**

- [NOAA – General Solar Position Calculations][noaa-sun] (PDF): sun position, equation of time, declination.
- [PVEducation – Intensity at sea level][pve-beam]: clear-sky direct beam formula.
- [PVEducation – Air mass][pve-am]
- [Sandia PVPMC – Basic solar position models][pvpmc-sun]

[nws-alt]: https://www.weather.gov/media/epz/wxcalc/altimeterSetting.pdf
[nws-palt]: https://www.weather.gov/media/epz/wxcalc/pressureAltitude.pdf
[nws-defs]: https://www.weather.gov/bou/pressure_definitions
[nws-obs]: https://www.weather.gov/lot/weather_observations_faq
[metar]: https://en.wikipedia.org/wiki/METAR
[rw-gauge]: https://rainwise.com/wired-rain-gauge
[pearson]: https://en.wikipedia.org/wiki/Pearson_correlation_coefficient
[bu-903]: https://batteryuniversity.com/article/bu-903-how-to-measure-state-of-charge
[rs-22807]: https://www.buycott.com/upc/040293150419
[rs-22807-manual]: https://www.smcelectronics.com/owner.htm
[rw-mk3]: https://rainwise.com/mwdownloads/download/link/id/385
[noaa-sun]: https://gml.noaa.gov/grad/solcalc/solareqns.PDF
[pve-beam]: https://www.pveducation.org/equations/intensity-sea-level
[pve-am]: https://www.pveducation.org/pvcdrom/properties-of-sunlight/air-mass
[pvpmc-sun]: https://pvpmc.sandia.gov/modeling-guide/1-weather-design-inputs/sun-position/basic-solar-position-models/
[corr-cause]: https://www.mathwords.com/c/correlation_vs_causation.htm
[sparkfun-div]: https://learn.sparkfun.com/tutorials/voltage-dividers/all
