#!/usr/bin/env python3
"""Measure clock91 idle power from the on-device coulomb counter.

Reads `clock91: power remain=...` lines (clock91_log_power(), once per minute
wake) and reports the mAh/h discharge rate, from which autonomy follows.

Why the coulomb counter and not a lab PSU: remain= is capacity(REMAIN_UF), the
gauge's unfiltered coulomb count, so it is not affected by BYPASS_BQ27427_SOC
deriving the displayed SOC from voltage.

Usage:
    # live capture from the device (Ctrl-C to stop and report)
    scripts/power-measure.py --port /dev/cu.usbmodem1101 --log run-a.log

    # re-report from a capture taken earlier
    scripts/power-measure.py --replay run-a.log

    # compare two runs (e.g. before/after a change)
    scripts/power-measure.py --replay run-a.log --compare run-b.log

Caveats this prints about, because they invalidate a run:
  - CHG/FC flags: a charger was attached, so the pack was not discharging.
  - ITPOR: the gauge reset and its state is untrustworthy.
  - A pack capacity far from design suggests the gauge has not learned yet.
"""

import argparse
import re
import signal
import sys
import time

# Anchored on the payload, not on log_impl()'s prefix, which carries file:line.
SAMPLE_RE = re.compile(
    r"power remain=(?P<remain>\d+)mAh"
    r"\s+full=(?P<full>\d+)mAh"
    r"\s+design=(?P<design>\d+)mAh"
    r"\s+v=(?P<mv>\d+)mV"
    r"\s+i=(?P<ma>[-+]?\d+)mA"  # firmware uses %d, but tolerate a signed form
    # The %% in the firmware format string does not render on this build, so the
    # line reads "itsoc=99flags=0x0088" with no % and no separator. Tolerate both.
    r"\s+itsoc=(?P<soc>\d+)%?"
    r"\s*flags=0x(?P<flags>[0-9A-Fa-f]{4})"
    r"(?P<rest>.*)"
)

# From lib/BQ27427/src/BQ27427_Definitions.h
BQ27427_FLAG_DSG = 1 << 0       # discharging
BQ27427_FLAG_SOCF = 1 << 1      # SOC final threshold
BQ27427_FLAG_SOC1 = 1 << 2      # SOC1 threshold
BQ27427_FLAG_BAT_DET = 1 << 3   # battery detected
BQ27427_FLAG_CFGUPMODE = 1 << 4 # config update mode
BQ27427_FLAG_ITPOR = 1 << 5     # Impedance Track reset
BQ27427_FLAG_DOD_CRRCT = 1 << 6 # depth-of-discharge correction
BQ27427_FLAG_OCVTAKEN = 1 << 7  # open-circuit voltage taken
BQ27427_FLAG_CHG = 1 << 8       # fast charging allowed
BQ27427_FLAG_FC = 1 << 9        # full charge detected
BQ27427_FLAG_UT = 1 << 14       # under-temperature
BQ27427_FLAG_OT = 1 << 15       # over-temperature

FLAG_NAMES = [
    (BQ27427_FLAG_OT, "OT"), (BQ27427_FLAG_UT, "UT"),
    (BQ27427_FLAG_FC, "FC"), (BQ27427_FLAG_CHG, "CHG"),
    (BQ27427_FLAG_OCVTAKEN, "OCVTAKEN"), (BQ27427_FLAG_DOD_CRRCT, "DOD_CRRCT"),
    (BQ27427_FLAG_ITPOR, "ITPOR"), (BQ27427_FLAG_CFGUPMODE, "CFGUPMODE"),
    (BQ27427_FLAG_BAT_DET, "BAT_DET"), (BQ27427_FLAG_SOC1, "SOC1"),
    (BQ27427_FLAG_SOCF, "SOCF"), (BQ27427_FLAG_DSG, "DSG"),
]


def decode_flags(f):
    names = [n for bit, n in FLAG_NAMES if f & bit]
    return " ".join(names) if names else "(none)"


class Sample:
    __slots__ = ("t", "remain", "full", "design", "mv", "ma", "soc", "flags")

    def __init__(self, t, m):
        self.t = t
        self.remain = int(m.group("remain"))
        self.full = int(m.group("full"))
        self.design = int(m.group("design"))
        self.mv = int(m.group("mv"))
        self.ma = int(m.group("ma"))
        self.soc = int(m.group("soc"))
        self.flags = int(m.group("flags"), 16)


def parse_stream(lines, period_s=60.0):
    """Parse power lines from a capture.

    A capture has no wall-clock timestamps, so sample N is placed at
    N * period_s. clock91_log_power() runs once per minute wake, so 60 s is
    right unless wakes were missed — the report prints the assumption.
    """
    out = []
    for line in lines:
        m = SAMPLE_RE.search(line)
        if m:
            out.append(Sample(len(out) * period_s, m))
    return out


def report(samples, label="", inferred_time=False):
    head = f"=== {label} ===" if label else "==="
    print(head)
    inferred = "  [inferred: 1 sample/min]" if inferred_time else ""


    if len(samples) < 2:
        print(f"  only {len(samples)} sample(s) — need at least 2 to compute a rate")
        return None

    first, last = samples[0], samples[-1]
    elapsed_h = (last.t - first.t) / 3600.0
    used_mah = first.remain - last.remain

    print(f"  samples        {len(samples)}")
    print(f"  elapsed        {elapsed_h * 60:.1f} min ({elapsed_h:.2f} h){inferred}")
    print(f"  remain         {first.remain} -> {last.remain} mAh  (delta {-used_mah:+d})")
    print(f"  voltage        {first.mv} -> {last.mv} mV")
    print(f"  gauge full     {last.full} mAh (design {last.design} mAh)")

    currents = [s.ma for s in samples]
    print(f"  i= reported    min {min(currents)} / mean {sum(currents)/len(currents):+.1f} / max {max(currents)} mA")

    flag_union = 0
    for s in samples:
        flag_union |= s.flags
    print(f"  flags seen     {decode_flags(flag_union)}")

    # Flag audit. Anything here means the rate below is not an idle discharge
    # rate. DSG is the positive signal: the pack is only discharging when it is
    # set, so "not DSG" catches charging AND sitting full on the charger, which
    # reports neither CHG nor DSG (observed: 0x0088 = BAT_DET|OCVTAKEN).
    problems = []
    dsg_count = sum(1 for s in samples if s.flags & BQ27427_FLAG_DSG)
    if dsg_count == 0:
        problems.append("DSG never set — pack never discharged (USB attached?); "
                        "unplug USB and rerun")
    elif dsg_count < len(samples):
        problems.append(f"DSG set on only {dsg_count}/{len(samples)} samples — "
                        "pack was not discharging throughout")
    if any(s.flags & (BQ27427_FLAG_CHG | BQ27427_FLAG_FC) for s in samples):
        problems.append("CHG/FC seen — charger was active")
    if any(s.flags & BQ27427_FLAG_ITPOR for s in samples):
        problems.append("ITPOR seen — gauge reset, readings untrustworthy")
    if any(s.flags & BQ27427_FLAG_CFGUPMODE for s in samples):
        problems.append("CFGUPMODE seen — gauge was being reconfigured")
    if any(s.ma > 0 for s in samples):
        problems.append("positive current seen — charging, not discharging")
    if elapsed_h < 0.5:
        problems.append(f"only {elapsed_h*60:.0f} min of data — too short to be stable")
    if used_mah <= 0:
        problems.append("remain did not decrease — no measurable discharge")

    if elapsed_h > 0 and used_mah > 0 and dsg_count == len(samples):
        rate = used_mah / elapsed_h
        print(f"\n  >> discharge    {rate:.1f} mAh/h  (= {rate:.1f} mA average draw)")
        if last.full > 0:
            print(f"  >> autonomy     {last.full / rate / 24:.1f} days from {last.full} mAh")
    else:
        rate = None
        print("\n  >> no discharge rate — see warnings below")

    if problems:
        print("\n  WARNINGS (the rate above may be meaningless):")
        for p in problems:
            print(f"    - {p}")

    return rate


def capture(port, baud, logpath):
    try:
        import serial  # pyserial, bundled with the PlatformIO penv
    except ImportError:
        sys.exit("pyserial not found — run this with the PlatformIO penv python:\n"
                 "  source ~/.platformio/penv/bin/activate")

    samples = []
    stop = {"now": False}

    def on_sigint(signum, frame):
        stop["now"] = True

    signal.signal(signal.SIGINT, on_sigint)

    print(f"Capturing from {port} at {baud}. Ctrl-C to stop and report.")
    print(f"Appending raw serial to {logpath}\n")

    with serial.Serial(port, baud, timeout=1) as ser, open(logpath, "a") as log:
        while not stop["now"]:
            try:
                raw = ser.readline()
            except serial.SerialException as e:
                print(f"\nserial error: {e}")
                break
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            log.write(line + "\n")
            log.flush()

            m = SAMPLE_RE.search(line)
            if m:
                s = Sample(time.time(), m)
                samples.append(s)
                mark = ""
                if s.flags & (BQ27427_FLAG_CHG | BQ27427_FLAG_FC):
                    mark += " [CHARGING]"
                if s.flags & BQ27427_FLAG_ITPOR:
                    mark += " [ITPOR]"
                elapsed = (s.t - samples[0].t) / 60.0
                delta = samples[0].remain - s.remain
                print(f"  [{len(samples):3d}] +{elapsed:6.1f} min  "
                      f"remain={s.remain} mAh (-{delta})  {s.mv} mV  "
                      f"i={s.ma:+d} mA{mark}")

    print()
    return samples


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial device, e.g. /dev/cu.usbmodem1101")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--log", default="power-run.log",
                    help="where to append raw serial (default power-run.log)")
    ap.add_argument("--replay", help="parse an existing capture instead of the device")
    ap.add_argument("--compare", help="second capture to compare --replay against")
    args = ap.parse_args()

    if args.replay:
        with open(args.replay) as f:
            a = parse_stream(f)
        rate_a = report(a, args.replay, inferred_time=True)

        if args.compare:
            with open(args.compare) as f:
                b = parse_stream(f)
            print()
            rate_b = report(b, args.compare, inferred_time=True)
            if rate_a and rate_b:
                delta = rate_b - rate_a
                pct = delta / rate_a * 100
                print(f"\n=== comparison ===")
                print(f"  {args.replay}: {rate_a:.1f} mAh/h")
                print(f"  {args.compare}: {rate_b:.1f} mAh/h")
                print(f"  change: {delta:+.1f} mAh/h ({pct:+.1f}%)")
        return

    if not args.port:
        ap.error("need --port (or --replay to re-read a capture)")

    samples = capture(args.port, args.baud, args.log)
    report(samples, args.log)
    if len(samples) >= 2:
        print(f"\nRe-report later with:  {sys.argv[0]} --replay {args.log}")


if __name__ == "__main__":
    main()
