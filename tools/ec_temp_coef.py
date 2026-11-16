"""Suggest an EC temperature coefficient (% per deg C) from logged readings.

Reads the readings API's SQLite database (table `readings`: measured_at, ec, ec_adc, water_temp_f).
Raw EC is recomputed from ec_adc with the device's ecFromAdc() curve, so the result doesn't depend
on any compensation the device already applies. Rows without ec_adc fall back to `ec`.

Method: top-ups (EC step-downs) and gaps split the data into segments. Inside each segment,
evaporation makes EC drift slowly, so EC and temperature are each detrended against time with a
straight line per segment, and the pooled slope of the EC residuals on the temperature residuals is
dEC/dT. Using whole segments, not hour-to-hour changes, matters: hourly temperature changes are about
one DS18B20 step (0.11 F), and that rounding biases a change-based fit toward zero.

The coefficient is a = (dEC/dT) / EC25, from EC(T) = EC25 * (1 + a * (T - 25)), the form the device uses.

Run it where the database is, for example on the Pi:
    ssh pi@192.168.12.230 'cd ~/envmonitor-api && .venv/bin/python - data/readings.db' < tools/ec_temp_coef.py
or on a copy:
    python tools/ec_temp_coef.py path/to/readings.db [--device envmonitor-1] [--days 30]
"""
import argparse
import sqlite3
import statistics
import sys
from datetime import datetime, timedelta, timezone

STEP_US = 3.0          # a median drop or rise this big between 6-reading windows splits segments
MAX_GAP_S = 25 * 60    # as the device's detector
MIN_SEGMENT = 24       # readings; shorter segments carry little temperature swing
SIDE = 6


def ec_from_adc(adc):
    """Same curve as ecFromAdc() in src/sensor_math.h."""
    v = adc / 4095.0 * 3.3
    return (133.42 * v ** 3 - 255.86 * v ** 2 + 857.39 * v) * 0.5


def parse_time(text):
    text = text.replace("Z", "+00:00")
    return datetime.fromisoformat(text).timestamp()


def load(path, device, days):
    con = sqlite3.connect(path)
    since = (datetime.now(timezone.utc) - timedelta(days=days)).strftime("%Y-%m-%dT%H:%M:%SZ")
    query = "SELECT measured_at, ec, ec_adc, water_temp_f FROM readings WHERE measured_at >= ?"
    args = [since]
    if device:
        query += " AND device = ?"
        args.append(device)
    rows = []
    for at, ec, adc, tf in con.execute(query + " ORDER BY measured_at", args):
        raw = ec_from_adc(adc) if adc is not None else ec
        if raw is None or tf is None:
            continue
        rows.append((parse_time(at), raw, (tf - 32) * 5 / 9))
    return rows


def segments(rows):
    """Splits at gaps and at EC steps (a step's 6 readings on each side are dropped)."""
    cuts = set()
    for i in range(1, len(rows)):
        if rows[i][0] - rows[i - 1][0] > MAX_GAP_S:
            cuts.add(i)
    for i in range(SIDE, len(rows) - SIDE + 1):
        before = statistics.median(r[1] for r in rows[i - SIDE:i])
        after = statistics.median(r[1] for r in rows[i:i + SIDE])
        if abs(before - after) >= STEP_US:
            cuts.update(range(i - SIDE, i + SIDE))  # leave the step out of every segment
    out, current = [], []
    for i, row in enumerate(rows):
        if i in cuts:
            if len(current) >= MIN_SEGMENT:
                out.append(current)
            current = []
        else:
            current.append(row)
    if len(current) >= MIN_SEGMENT:
        out.append(current)
    return out


def detrend(xs, ts):
    mt, mx = statistics.fmean(ts), statistics.fmean(xs)
    stt = sum((t - mt) ** 2 for t in ts)
    slope = sum((t - mt) * (x - mx) for t, x in zip(ts, xs)) / stt if stt else 0.0
    return [x - mx - slope * (t - mt) for t, x in zip(ts, xs)]


def fit(segs):
    """Pooled slope of detrended EC on detrended temperature, µS per deg C."""
    sxy = sxx = 0.0
    for seg in segs:
        ts = [r[0] for r in seg]
        re = detrend([r[1] for r in seg], ts)
        rt = detrend([r[2] for r in seg], ts)
        sxy += sum(a * b for a, b in zip(rt, re))
        sxx += sum(a * a for a in rt)
    return sxy / sxx if sxx else float("nan"), sxx


def coefficient(slope, rows):
    """% per deg C, relative to EC at 25 C."""
    mean_ec = statistics.fmean(r[1] for r in rows)
    mean_t = statistics.fmean(r[2] for r in rows)
    a = slope / mean_ec
    for _ in range(5):  # EC25 = mean EC / (1 + a (T - 25)), solved by iteration
        a = slope / (mean_ec / (1 + a * (mean_t - 25)))
    return a * 100


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("db", nargs="?", default="data/readings.db")
    parser.add_argument("--device", default="envmonitor-1")
    parser.add_argument("--days", type=float, default=30)
    args = parser.parse_args()

    rows = load(args.db, args.device, args.days)
    if len(rows) < 100:
        sys.exit(f"Only {len(rows)} usable readings; need at least 100.")
    segs = segments(rows)
    used = sum(len(s) for s in segs)
    if not segs:
        sys.exit("No segment long enough between top-ups and gaps.")
    slope, sxx = fit(segs)
    coef = coefficient(slope, rows)

    # Leave-one-segment-out spread, as a rough uncertainty
    loo = []
    for i in range(len(segs)):
        rest = segs[:i] + segs[i + 1:]
        if rest:
            s, _ = fit(rest)
            loo.append(coefficient(s, rows))
    temps = [r[2] for r in rows]
    print(f"readings: {len(rows)} over {args.days:g} days, {used} in {len(segs)} segments between top-ups")
    print(f"water temperature: {min(temps):.2f}-{max(temps):.2f} C ({min(temps) * 9 / 5 + 32:.1f}-{max(temps) * 9 / 5 + 32:.1f} F)")
    print(f"dEC/dT: {slope:.2f} uS per deg C ({slope * 5 / 9:.2f} uS per deg F)")
    if loo:
        print(f"suggested coefficient: {coef:.2f} %/C (leave-one-segment-out range {min(loo):.2f}-{max(loo):.2f})")
    else:
        print(f"suggested coefficient: {coef:.2f} %/C (one segment only: no spread estimate)")
    print("Enter it in the device's Settings > EC temperature compensation. 0 turns compensation off.")


if __name__ == "__main__":
    main()
