"""Clear-sky sunlight on a tilted panel, compared with Rainwise byte 15.

Sun position: NOAA "General Solar Position Calculations"
  https://gml.noaa.gov/grad/solcalc/solareqns.PDF
Direct beam:  PVEducation  I_D = 1.353 * 0.7^(AM^0.678) kW/m^2
  https://www.pveducation.org/equations/intensity-sea-level
Air mass:     Kasten & Young (1989), as given on PVEducation
  https://www.pveducation.org/pvcdrom/properties-of-sunlight/air-mass
Diffuse:      ~10 % of direct on a clear day, spread evenly over the sky (isotropic)
"""

import csv, math, os, sys, statistics as st

# Your location: set SOLAR_LAT / SOLAR_LON / SOLAR_TZ (hours from UTC, e.g. -6 for MDT).
# Defaults are placeholders, not a real site.
LAT = float(os.environ.get("SOLAR_LAT", "40.0"))
LON = float(os.environ.get("SOLAR_LON", "-105.0"))  # west is negative
TZ = float(os.environ.get("SOLAR_TZ", "-6"))
DOY = 299  # Oct 26, 2026
ALBEDO = 0.2  # bare ground; fresh snow is ~0.8


def sun(hour_local):
    g = 2 * math.pi / 365 * (DOY - 1 + (hour_local - 12) / 24)
    eqt = 229.18 * (
        0.000075
        + 0.001868 * math.cos(g)
        - 0.032077 * math.sin(g)
        - 0.014615 * math.cos(2 * g)
        - 0.040849 * math.sin(2 * g)
    )
    decl = (
        0.006918
        - 0.399912 * math.cos(g)
        + 0.070257 * math.sin(g)
        - 0.006758 * math.cos(2 * g)
        + 0.000907 * math.sin(2 * g)
        - 0.002697 * math.cos(3 * g)
        + 0.00148 * math.sin(3 * g)
    )
    tst = hour_local * 60 + eqt + 4 * LON - 60 * TZ
    ha = math.radians(tst / 4 - 180)
    lat = math.radians(LAT)
    cz = math.sin(lat) * math.sin(decl) + math.cos(lat) * math.cos(decl) * math.cos(ha)
    zen = math.acos(max(-1, min(1, cz)))
    # azimuth clockwise from north
    az = (
        math.degrees(
            math.atan2(
                math.sin(ha),
                math.cos(ha) * math.sin(lat) - math.tan(decl) * math.cos(lat),
            )
        )
        + 180
    )
    return math.degrees(zen), az % 360


def poa(hour_local, tilt, panel_az=180):
    zen, az = sun(hour_local)
    if zen >= 90:
        return 0.0
    am = 1 / (math.cos(math.radians(zen)) + 0.50572 * (96.07995 - zen) ** -1.6364)
    dni = 1353 * 0.7 ** (am**0.678)  # W/m^2
    dhi = 0.1 * dni
    ghi = dni * math.cos(math.radians(zen)) + dhi
    z, t = math.radians(zen), math.radians(tilt)
    cos_aoi = math.cos(z) * math.cos(t) + math.sin(z) * math.sin(t) * math.cos(
        math.radians(az - panel_az)
    )
    return (
        dni * max(cos_aoi, 0)
        + dhi * (1 + math.cos(t)) / 2
        + ghi * ALBEDO * (1 - math.cos(t)) / 2
    )


if __name__ == "__main__":
    rows = list(csv.DictReader(open(sys.argv[1])))
    minute = {}
    for r in rows:
        minute[r["pc_time"][:16]] = r
    keys = sorted(minute)
    hrs = [int(k[11:13]) + int(k[14:16]) / 60 for k in keys]
    b15 = [bytes.fromhex(minute[k]["raw_hex"])[14] for k in keys]
    temp = [float(minute[k]["temp_f"]) for k in keys]

    # sunrise / noon / sunset
    fine = [h / 60 for h in range(5 * 60, 20 * 60)]
    up = [h for h in fine if sun(h)[0] < 90]
    noon = min(fine, key=lambda h: sun(h)[0])
    fmt = lambda h: f"{int(h):02d}:{int(round(h % 1 * 60)) % 60:02d}"
    print(
        f"sunrise {fmt(up[0])}  solar noon {fmt(noon)} (sun {90-sun(noon)[0]:.1f} deg high)  sunset {fmt(up[-1])}"
    )
    print(
        f"byte 15 peak {fmt(hrs[b15.index(max(b15))])}   temperature peak {fmt(hrs[temp.index(max(temp))])}"
    )

    def corr_lag(model, target, max_lag=240):
        best = None
        for lag in range(0, max_lag + 1, 5):  # model shifted later by lag minutes
            x = model[: len(model) - lag] if lag else model
            y = target[lag:]
            c = st.correlation(x, y)
            if best is None or c > best[1]:
                best = (lag, c)
        return st.correlation(model, target), best

    print(f"\n{'model':28} {'corr b15':>8} {'best lag':>9} {'corr there':>10}")
    c, (lag, cl) = corr_lag(temp, b15)
    print(f"{'outside temperature':28} {c:8.2f} {lag:7d} m {cl:10.2f}")
    for tilt in (0, 30, 45, 60, 90):
        p = [poa(h, tilt) for h in hrs]
        c, (lag, cl) = corr_lag(p, b15)
        pk = fmt(hrs[p.index(max(p))])
        print(
            f"{'clear-sky sun, tilt '+str(tilt)+' S':28} {c:8.2f} {lag:7d} m {cl:10.2f}   peak {pk}, max {max(p):.0f} W/m2"
        )
        # energy received so far today (a battery-charge proxy)
        cum, s = [], 0
        for v in p:
            s += v / 60
            cum.append(s)
    c, (lag, cl) = corr_lag(cum, b15)
    print(f"{'cumulative sun since dawn':28} {c:8.2f} {lag:7d} m {cl:10.2f}")
