# NAS logging service

`rainwise/rainwise_service.py` records the station's data on the NAS. It runs as a TrueNAS custom app ([TrueNAS custom apps][truenas-apps]), a Docker container defined by `rainwise/docker-compose.yml`, and stores everything in one SQLite file. This page covers what it does, every setting, the tables, installing and updating it, example queries and troubleshooting. For the packet bytes themselves see [protocol.md](protocol.md); for the radio link see [lora/README.md](../lora/README.md).

## How the data flows

```
station ──LoRa──> receiver (RAK4631, USB) ──> NAS: /dev/serial/by-id/... ──> container /dev/rainwise
                                                                                   │
IP-100 ──HTTP status.xml (once a minute)─────────────────────────────────────────> │ rainwise_service.py
                                                                                   ▼
                                                                    /mnt/<pool>/<dataset>/rainwise.db
```

- The **receiver** in raw mode sends two kinds of frames over USB ([lora/README.md, "Station ID"](../lora/README.md#station-id-and-several-stations)):
  - weather packets: `RWP`, the 2-byte station ID, then the 32-byte packet, every 2 s;
  - station status: `RWT`, the station ID, RSSI, SNR, then the 35-byte status message, once a minute.
- The **service** reads the serial port(s), checks each frame's CRC, decodes it and writes a row. Weather packets go to `packets`, status frames to `station_status`. It also polls the IP-100's XML into `ip100_xml`, which is a useful cross-check while the Rainwise system is still running.
- An **XBee USB breakout** also works as a source. Its packets have no tag, so `station_id` is empty.

The service keeps the raw bytes of every packet and status message, so history can be decoded again later, for example once the unknown bytes are understood.

## Settings

All settings are environment variables in the compose file's `environment:` block.

| Setting | Default | What it does |
| --- | --- | --- |
| `SERIAL_PORT` | (empty = off) | Receiver port(s) inside the container. Several, comma-separated, e.g. one LoRa receiver per frequency: `/dev/rainwise,/dev/rainwise2`. Each needs a `devices:` line. |
| `BAUD` | `9600` | Serial speed. Used by an XBee breakout; the RAK's USB port ignores it. |
| `XML_URL` | (empty = off) | The IP-100's `status.xml` address. |
| `XML_INTERVAL` | `60` | Seconds between IP-100 polls. |
| `DB_PATH` | `/data/rainwise.db` | Database file inside the container; `/data` is the dataset. |
| `STATUS_KEEP_DAYS` | `0` (forever) | Delete `station_status` rows older than this many days, checked once a day. Weather data (`packets`) is never deleted. |
| `ELEVATION_FT` | `0` (no correction) | Barometer height above sea level, for the sea-level pressure column. |
| `BARO_OFFSET` | `0` | inHg added after correction, to calibrate against a nearby airport ([protocol.md, sea-level pressure](protocol.md)). |
| `STATS_EVERY` | `600` | Seconds between the counts line in the log. |
| `UDP_PORT` | `0` (off) | See [UDP input](#udp-input-off). |
| `TZ` | (container default UTC) | Time zone for the log lines and for `'localtime'` in queries. The database itself always stores UTC. |

Other parts of the compose file:

- **`devices:`** maps the receiver into the container. Left side: the NAS's stable name under `/dev/serial/by-id/`, which comes from the board's USB serial number and survives reboots and port changes, unlike `/dev/ttyACM0` ([Docker Compose `devices`][compose-devices]). Right side: the name the service uses.
- **`volumes:`** mounts the dataset twice: read-only at `/app` (the script) and read-write at `/data` (the database).
- **`image: python:3.12-slim`** is the official Python image ([Docker Hub – python][docker-python]). At each start the container installs pyserial ([pyserial][pyserial]) and runs the script, so updating the service means copying a new script and restarting, with no image to build.
- **`restart: unless-stopped`** starts the app again after a crash or a NAS reboot.

### UDP input (off)

`UDP_PORT` accepts weather packets over the network, one 32-byte packet per UDP datagram, as raw bytes or hex text. It was meant for a receiver that forwards packets over Wi-Fi, such as an ESP32. Nothing uses it now, and it is **off by default** for two reasons:

- It has no sender check: anything on the network could add fake packets to the database.
- It doesn't read station IDs or status frames.

If a network receiver is ever needed (say, a receiver with no USB path to the NAS), the input should be updated for those first. Then set `UDP_PORT: "5005"` and uncomment the `ports:` lines.

## Tables

Times are ISO 8601 text in UTC, e.g. `2026-11-11T19:37:21+00:00`, so a daylight-saving change never creates duplicate or missing hours. Convert with SQLite's `'localtime'` modifier ([SQLite date functions][sqlite-date]). Numbers are stored as SQLite `REAL`.

### `packets`: weather, every 2 s, kept forever

| Column | Meaning |
| --- | --- |
| `id` | Row number, in arrival order |
| `ts_utc` | When the service received it |
| `source` | Which receiver, e.g. `serial:/dev/rainwise` |
| `station_id` | Station ID (4 hex digits) from the LoRa tag; empty for an XBee breakout |
| `raw_hex` | The 32 bytes as hex |
| `temp_f`, `humidity_pct` | Outside temperature (°F) and humidity (%) |
| `baro_inhg`, `baro_sl_inhg` | Station pressure; sea-level pressure from `ELEVATION_FT` and `BARO_OFFSET` |
| `wind_mph`, `wind_dir_deg` | Wind speed and direction |
| `rain_count` | Rain tip counter, 0.01 in per tip, wraps at 256 and restarts at 0 when the station powers up; see [daily rain](#daily-rain) |
| `rain_chk_ok` | 1 when byte 14 = 255 − byte 13 |
| `station_v` | Byte 15 as volts (the IP-100's "station volts") |
| `low_v_flag`, `b15`, `b16`, `b17_18_le`, `b19`, `v20_21`, `v22_23`, `b24_30_hex` | Raw and partly understood bytes, kept for later decoding ([protocol.md](protocol.md)) |

### `station_status`: metadata, once a minute per station

This is data *about* the data, from each LoRa station's status message. It shows whether the station restarted, how the controller drives the handshake lines, whether packets were lost and how good the link was.

| Column | Meaning | Normal |
| --- | --- | --- |
| `ts_utc`, `source`, `station_id` | As in `packets` | |
| `rssi_dbm`, `snr_db` | Signal strength and signal-to-noise ratio of this status message at the receiver | Steady; SNR above about −10 dB leaves comfortable margin at SF10 |
| `uptime_s` | Seconds since the station board started | Rises 60 per row; a drop means the station restarted |
| `handshake` | Station handshake mode: `mirror`, `cts-low` or `released` | What you set |
| `sleep_rq_high`, `cts_high`, `sleep_rq_wired` | Handshake line levels at that moment (1 = high) | |
| `commands_on`, `last_command` | Remote commands enabled; sequence number of the last command | |
| `wakes`, `avg_awake_ms`, `max_awake_ms` | How often the controller woke the radio in the last minute, and for how long | About 30 a minute once installed |
| `weather_packets`, `uart_bytes` | Totals since the station started | Rising together |
| `tx_errors`, `dropped` | Totals: failed transmissions; packets replaced before they could be sent | 0 |
| `channel_busy`, `sent_anyway` | Totals: listen-before-talk found the channel busy; sent after 5 busy checks | Low and flat; both climbing means a crowded frequency |
| `raw_hex` | The 35-byte status message | |

The counters are totals since the station started, so they go back to 0 at a restart. To get an amount per hour, compare rows, not single values.

### `ip100_xml`: the IP-100, once a minute

`clock`, `station_v`, `temp_out`, `humidity`, `pressure`, `precip`, `wind_speed`, `wind_dir`, `gust_speed`, `gust_dir`, `temp_in`, and the full XML in `raw_xml`. Useful for comparing against the LoRa data while the Rainwise receivers are still running. It can be turned off (`XML_URL: ""`) once they are retired.

### Size

At one weather packet every 2 s, `packets` grows by about 15.8 million rows a year, which is most of the file. `station_status` adds about 0.5 million rows a year per station, and `ip100_xml` the same plus its XML text. Estimate several GB a year in total. Check the real figure with the [size query](#table-sizes) after a few weeks.

## Install on TrueNAS

Needs TrueNAS SCALE 24.10 or newer, where apps run on Docker and can be installed from YAML ([TrueNAS custom apps][truenas-apps]).

1. **Dataset:** create one for the app, e.g. `<pool>/rainwise`.
2. **Script:** copy `rainwise/rainwise_service.py` into the dataset (SMB share or `scp`).
3. **Receiver:** plug the LoRa receiver into the NAS. In **System → Shell** run `ls /dev/serial/by-id/` and note its name (`usb-RAKwireless_WisCore_RAK4631_Board_<serial>-if00`).
4. **Compose file:** fill in a copy of `rainwise/docker-compose.yml`: the two dataset paths, the device name, `TZ`, `XML_URL`, `ELEVATION_FT`, `BARO_OFFSET`. Keep that filled-in copy out of git; `rainwise/docker-compose.local.yml` is gitignored for this.
5. **App:** **Apps → Discover Apps → ⋮ → Install via YAML**, name it `rainwise`, paste, **Save**.
6. **Check the log** (the app's **Logs**): `database ready`, `polling IP-100`, `serial open: /dev/rainwise @ 9600`. After `STATS_EVERY` seconds the first counts line follows; see [the stats line](#the-stats-line).

### Updating

- **New script:** copy it over the old one in the dataset, then **Restart** the app. The database and its rows stay. New columns are added to an existing database automatically.
- **Settings:** edit the app's YAML (the app's **Edit**) and save; TrueNAS recreates the container.
- **Station install day:** everything recorded before the LoRa station goes on the mast is bench-test data with current timestamps. Stop the app, delete `rainwise.db` (and `rainwise.db-wal`, `rainwise.db-shm` if present), start the app (step 5 of the [station install checklist](../lora/README.md#station-install-one-trip)).

## Reading the data

SQLite should only be opened from the machine that runs it. Opening the live file over an SMB share risks corruption, because network file locking is unreliable ([SQLite over a network][sqlite-net]). Two safe ways:

**A copy for the PC.** The service uses WAL mode, so readers don't block it ([SQLite WAL][sqlite-wal]). Python's backup function makes a consistent copy while it runs ([Python sqlite3 backup][py-backup]). In the NAS shell:

```bash
sudo docker exec rainwise python3 -c "import sqlite3; sqlite3.connect('/data/rainwise.db').backup(sqlite3.connect('/data/rainwise-copy.db'))"
```

Then fetch `rainwise-copy.db` from the dataset (SMB or `scp`) and open it on the PC with `sqlite3` or a viewer such as DB Browser for SQLite ([DB Browser for SQLite][sqlitebrowser]).

**A quick look on the NAS**, for one query:

```bash
sudo docker exec rainwise python3 -c "import sqlite3; [print(r) for r in sqlite3.connect('/data/rainwise.db').execute('SELECT ts_utc, temp_f, wind_mph FROM packets ORDER BY id DESC LIMIT 5')]"
```

### Example queries

These use window functions such as `LAG()`, which compare a row with the one before it ([SQLite window functions][sqlite-window]). `'localtime'` uses the time zone of whoever runs the query (the container's `TZ` on the NAS, the PC's own time zone otherwise). All of them were tested against a test database built by the service's own code.

#### Latest reading

```sql
SELECT ts_utc, temp_f, humidity_pct, baro_sl_inhg, wind_mph, wind_dir_deg, station_v
FROM packets ORDER BY id DESC LIMIT 1;
```

#### Daily rain

The counter wraps at 256 and restarts at 0 when the station powers up, so rain is the sum of the *changes*, with the same rules as the Oracle receiver's rain fix ([lora/README.md, rain fix](../lora/README.md#rain-fix-for-the-oracle-display-receiver-setting)). A long gap with the counter lower means a restart: count only the tips since it. Without a gap, more than 10 tips between packets means a restart too. Put your station ID in place of `ABCD`.

```sql
WITH c AS (
  SELECT ts_utc, rain_count AS rc,
         LAG(rain_count) OVER w AS prev,
         (julianday(ts_utc) - julianday(LAG(ts_utc) OVER w)) * 86400 AS gap_s
  FROM packets
  WHERE station_id = 'ABCD'
  WINDOW w AS (PARTITION BY station_id ORDER BY id)
)
SELECT date(ts_utc, 'localtime') AS day,
       ROUND(SUM(CASE
         WHEN prev IS NULL THEN 0                                   -- first packet
         WHEN gap_s > 30 THEN (CASE WHEN rc >= prev THEN rc - prev  -- outage: real rain
                                    ELSE rc END)                    --   or restart: tips since
         WHEN (rc - prev + 256) % 256 <= 10 THEN (rc - prev + 256) % 256  -- normal, incl. wrap
         WHEN rc <= 10 THEN rc                                      -- quick restart
         ELSE 0 END) * 0.01, 2) AS rain_in
FROM c GROUP BY day ORDER BY day;
```

The first packet in the database has nothing to compare against, so a tip in it isn't counted.

#### Link quality by hour

```sql
SELECT strftime('%Y-%m-%d %H:00', ts_utc, 'localtime') AS hour, station_id,
       ROUND(AVG(rssi_dbm), 1) AS rssi_avg, MIN(rssi_dbm) AS rssi_worst,
       ROUND(AVG(snr_db), 2) AS snr_avg, COUNT(*) AS status_msgs
FROM station_status GROUP BY hour, station_id ORDER BY hour;
```

`status_msgs` below 60 in an hour means status messages were missed.

#### Station restarts

```sql
SELECT ts_utc, station_id, prev_up AS uptime_before_s, uptime_s
FROM (SELECT ts_utc, station_id, uptime_s,
             LAG(uptime_s) OVER (PARTITION BY station_id ORDER BY id) AS prev_up
      FROM station_status)
WHERE uptime_s < prev_up;
```

#### Gaps (station not heard for more than 3 minutes)

```sql
SELECT prev_ts AS last_heard, ts_utc AS heard_again, station_id,
       ROUND((julianday(ts_utc) - julianday(prev_ts)) * 1440) AS minutes
FROM (SELECT ts_utc, station_id,
             LAG(ts_utc) OVER (PARTITION BY station_id ORDER BY id) AS prev_ts
      FROM station_status)
WHERE (julianday(ts_utc) - julianday(prev_ts)) * 1440 > 3;
```

#### Table sizes

```sql
SELECT 'packets' AS tbl, COUNT(*) AS rows FROM packets
UNION ALL SELECT 'station_status', COUNT(*) FROM station_status
UNION ALL SELECT 'ip100_xml', COUNT(*) FROM ip100_xml;
```

## Troubleshooting

### The stats line

Every `STATS_EVERY` seconds the log shows counts since the app started:

```
stats serial=4500  status=150  udp=0  udp_bad=0  xml=150  xml_err=0
```

| Count | Expect (per 10 minutes, one station, installed) | If not |
| --- | --- | --- |
| `serial` | About 300 weather packets | 0 with `status` rising: the station gets no data from its controller (check the station's handshake in `station_status`). Both 0: receiver or link. |
| `status` | About 10 | 0 with `serial` rising: an XBee breakout or an old receiver firmware. |
| `xml`, `xml_err` | 10 and 0 | `xml_err` rising: the IP-100 is unreachable. |

On the bench, with the station on a charger and no weather data, `serial=0` and `status` rising is normal.

### Common problems

- **`serial error on /dev/rainwise (...); retrying in 10 s`:** the receiver is unplugged, or the `devices:` line doesn't match the name in `/dev/serial/by-id/`. The service keeps retrying, so plugging the receiver back in is enough.
- **No rows although the port opens:** the receiver may be in text mode. A receiver starts in raw mode after power-up; replug it.
- **`Permission denied` on `/data/rainwise.db`:** the container can't write to the dataset. Check the dataset's permissions in **Datasets → Permissions**.
- **Times look 7 hours off:** the database is in UTC by design; use `'localtime'` in queries.
- **Only one program can open a serial port.** While the app runs, a `pio device monitor` on the same receiver fails, and the other way round.

## References

- [TrueNAS – Installing custom apps][truenas-apps]: Install via YAML, needs SCALE 24.10 or newer.
- [Docker – Compose file, services: `devices`][compose-devices]: mapping a host device into a container.
- [Docker Hub – python][docker-python]: the official image the app runs on.
- [pyserial][pyserial]: serial port library.
- [SQLite – Write-Ahead Logging][sqlite-wal]: readers don't block the writer.
- [SQLite – Date and time functions][sqlite-date]: `'localtime'`, `julianday()`, `strftime()`.
- [SQLite – Window functions][sqlite-window]: `LAG()` and `OVER`.
- [SQLite – SQLite over a network][sqlite-net]: why not to open the live file over SMB.
- [Python – sqlite3 `Connection.backup`][py-backup]: consistent copy of a live database.
- [DB Browser for SQLite][sqlitebrowser]: a free viewer for the PC.

[truenas-apps]: https://apps.truenas.com/managing-apps/installing-custom-apps/
[compose-devices]: https://docs.docker.com/reference/compose-file/services/#devices
[docker-python]: https://hub.docker.com/_/python
[pyserial]: https://pyserial.readthedocs.io/
[sqlite-wal]: https://www.sqlite.org/wal.html
[sqlite-date]: https://www.sqlite.org/lang_datefunc.html
[sqlite-window]: https://www.sqlite.org/windowfunctions.html
[sqlite-net]: https://www.sqlite.org/useovernet.html
[py-backup]: https://docs.python.org/3/library/sqlite3.html#sqlite3.Connection.backup
[sqlitebrowser]: https://sqlitebrowser.org/
