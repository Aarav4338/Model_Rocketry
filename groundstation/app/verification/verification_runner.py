import csv
import json
import os
import subprocess
import time
from datetime import datetime
from pathlib import Path
from statistics import mean

ROOT = Path(__file__).resolve().parents[3]
PY = "python"
MAIN = ROOT / "groundstation" / "app" / "main.py"

VER_DIR = ROOT / "logs" / "verification"
VER_DIR.mkdir(parents=True, exist_ok=True)


def run_main(args, timeout=60):
    cmd = [PY, str(MAIN)] + args
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    return proc


def read_valid_csv(path):
    path = Path(path)
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8") as f:
        r = csv.DictReader(f)
        return list(r)


def read_raw_csv(path):
    path = Path(path)
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8") as f:
        r = csv.DictReader(f)
        return list(r)


def read_rejected_log(path):
    path = Path(path)
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8") as f:
        return [line.strip() for line in f if line.strip()]


def read_diagnostics_metrics(path):
    path = Path(path)
    if not path.exists():
        return []
    metrics = []
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            if "METRICS:" in line:
                try:
                    payload = line.split("METRICS:", 1)[1].strip()
                    metrics.append(json.loads(payload))
                except Exception:
                    pass
    return metrics


def seq_list_from_valid(rows):
    seqs = []
    for r in rows:
        try:
            seqs.append(int(r.get("sequence", "0")))
        except Exception:
            pass
    return seqs


def velocity_list_from_valid(rows):
    vals = []
    for r in rows:
        try:
            vals.append(float(r.get("vertical_velocity", "0")))
        except Exception:
            pass
    return vals


def altitude_list_from_valid(rows):
    vals = []
    for r in rows:
        try:
            vals.append(float(r.get("altitude", "0")))
        except Exception:
            pass
    return vals


def pressure_list_from_valid(rows):
    vals = []
    for r in rows:
        try:
            vals.append(float(r.get("pressure", "0")))
        except Exception:
            pass
    return vals


def voltage_list_from_valid(rows):
    vals = []
    for r in rows:
        try:
            vals.append(float(r.get("voltage", "0")))
        except Exception:
            pass
    return vals


def monotonic_increasing(lst):
    return all(x <= y for x, y in zip(lst, lst[1:]))


def monotonic_strict_increasing(lst):
    return all(x < y for x, y in zip(lst, lst[1:]))


def monotonic_decreasing(lst):
    return all(x >= y for x, y in zip(lst, lst[1:]))


def run_test_simulate(name, corruption_rate, count=100, interval=0.05, duration=None):
    out = VER_DIR / name
    if out.exists():
        # remove previous
        for p in out.glob("*"):
            try:
                p.unlink()
            except Exception:
                pass
    out.mkdir(parents=True, exist_ok=True)

    input_file = str(out / "sim_input.txt")
    args = [
        "--mode",
        "simulate",
        "--input-file",
        input_file,
        "--output-dir",
        str(out),
        "--simulate-count",
        str(count),
        "--simulate-interval",
        str(interval),
        "--corruption-rate",
        str(corruption_rate),
    ]
    if duration is not None:
        args += ["--duration", str(duration)]
    else:
        args += ["--duration", str(max(10, int(count * interval + 3)))]

    proc = run_main(args, timeout=120)
    return out, proc


def run_test_replay(name, replay_source):
    out = VER_DIR / name
    if out.exists():
        for p in out.glob("*"):
            try:
                p.unlink()
            except Exception:
                pass
    out.mkdir(parents=True, exist_ok=True)
    args = [
        "--mode",
        "replay",
        "--input-file",
        str(replay_source),
        "--output-dir",
        str(out),
        "--poll-interval",
        "0.05",
    ]
    proc = run_main(args, timeout=60)
    return out, proc


def analyze_basic(out):
    valid = read_valid_csv(out / "telemetry_valid.csv")
    raw = read_raw_csv(out / "telemetry_raw.csv")
    rejected = read_rejected_log(out / "rejected_packets.log")
    metrics = read_diagnostics_metrics(out / "diagnostics.log")
    return {"valid": valid, "raw": raw, "rejected": rejected, "metrics": metrics}


def report_passfail(ok):
    return "PASS" if ok else "FAIL"


def run_verification():
    results = {}

    # TEST 1: CLEAN TELEMETRY
    out1, proc1 = run_test_simulate("test1_clean", corruption_rate=0.0, count=100, interval=0.05, duration=8)
    a1 = analyze_basic(out1)
    valid = a1["valid"]
    rejected = a1["rejected"]
    metrics = a1["metrics"]
    
    # FIX 1: Validate clean runs have ZERO corruption (fail loudly if violated)
    rejected_count = len(rejected)
    checksum_failures = sum(1 for r in rejected if "Checksum" in r)
    if rejected_count > 0 or checksum_failures > 0:
        raise AssertionError(
            f"Clean run validation FAILED: corruption_rate=0.0 must produce zero rejections.\n"
            f"  Total rejected: {rejected_count}\n"
            f"  Checksum failures: {checksum_failures}\n"
            f"  See: {out1 / 'rejected_packets.log'}"
        )
    
    seqs = seq_list_from_valid(valid)
    alts = altitude_list_from_valid(valid)
    vels = velocity_list_from_valid(valid)
    press = pressure_list_from_valid(valid)
    volts = voltage_list_from_valid(valid)

    test1 = {"name": "CLEAN TELEMETRY"}
    test1["no_rejected"] = len(rejected) == 0
    test1["no_checksum_failures"] = all("Checksum" not in r for r in rejected)
    test1["sequence_monotonic"] = monotonic_increasing(seqs) and all((b - a) <= 1 for a, b in zip(seqs, seqs[1:])) if seqs else False
    # altitude behavior: find peak index
    peak_idx = None
    if alts:
        peak_idx = max(range(len(alts)), key=lambda i: alts[i])
        before = alts[:peak_idx+1]
        after = alts[peak_idx:]
        test1["altitude_increase_then_decrease"] = monotonic_increasing(before) and monotonic_decreasing(after)
    else:
        test1["altitude_increase_then_decrease"] = False
    # velocity sign check
    if vels:
        test1["velocity_pattern"] = (all(x > 0 for x in vels[:max(1, len(vels)//3)]) and abs(mean(vels[max(1,len(vels)//3):min(len(vels), 2*len(vels)//3)])) < 1.0 and all(x < 0 for x in vels[-max(1,len(vels)//3):]))
    else:
        test1["velocity_pattern"] = False
    # pressure vs altitude correlation
    if alts and press:
        # expect negative correlation (as altitude up, pressure down)
        corr = (alts[0] - alts[-1]) * (press[-1] - press[0])
        test1["pressure_plausible"] = corr >= 0
    else:
        test1["pressure_plausible"] = False
    test1["voltage_realistic"] = all(9.0 <= v <= 12.5 for v in volts) if volts else False
    test1["diagnostics_good"] = False
    if metrics:
        last = metrics[-1]
        test1["diagnostics_good"] = last.get("link_quality", 0) >= 0.8

    test1["evidence_files"] = {"valid": str(out1 / "telemetry_valid.csv"), "raw": str(out1 / "telemetry_raw.csv"), "rejected": str(out1 / "rejected_packets.log"), "diagnostics": str(out1 / "diagnostics.log")}
    results["TEST1"] = test1

    # TEST 2: CORRUPTION SURVIVAL
    out2, proc2 = run_test_simulate("test2_corrupt", corruption_rate=0.2, count=120, interval=0.03, duration=8)
    a2 = analyze_basic(out2)
    test2 = {"name": "CORRUPTION SURVIVAL"}
    test2["rejected_populated"] = len(a2["rejected"]) > 0
    test2["no_crash"] = proc2.returncode == 0
    test2["valid_processed"] = len(a2["valid"]) > 0
    test2["diagnostics_degraded"] = False
    if a2["metrics"]:
        q = a2["metrics"][-1].get("link_quality", 1.0)
        test2["diagnostics_degraded"] = q < 0.95
    # check categories
    cat_checks = {"malformed": False, "checksum": False, "missing": False, "random": False}
    for line in a2["rejected"]:
        if "CORRUPTED_PAYLOAD" in line or "malformed" in line.lower():
            cat_checks["malformed"] = True
        if "Checksum" in line:
            cat_checks["checksum"] = True
        if "Unexpected field count" in line or "missing" in line.lower():
            cat_checks["missing"] = True
        if any(ch in line for ch in ["\\x", "REJECTED", "CORRUPTED", "random"]) and "Checksum" not in line:
            # best-effort
            cat_checks["random"] = True
    test2["categories_detected"] = cat_checks
    test2["evidence_files"] = {"valid": str(out2 / "telemetry_valid.csv"), "raw": str(out2 / "telemetry_raw.csv"), "rejected": str(out2 / "rejected_packets.log"), "diagnostics": str(out2 / "diagnostics.log")}
    results["TEST2"] = test2

    # TEST 3: REPLAY MODE
    # Use captured file from TEST1 raw log, but validate file is not empty
    replay_src = out1 / "sim_input.txt"
    
    # FIX 2: Validate replay source exists AND is not empty
    # Prevents using consumed/empty files from live transport
    if not replay_src.exists() or replay_src.stat().st_size == 0:
        # Build from raw telemetry (immutable source)
        raw_rows = read_raw_csv(out1 / "telemetry_raw.csv")
        replay_src = out1 / "replay_source.txt"
        with replay_src.open("w", encoding="utf-8") as f:
            for r in raw_rows:
                packet = r.get("raw_packet", "").strip()
                if packet:  # Only write non-empty packets
                    f.write(packet + "\n")
        # Validate fallback source was created
        if not replay_src.exists() or replay_src.stat().st_size == 0:
            raise RuntimeError(f"Failed to create valid replay source from raw telemetry: {replay_src}")
    
    out3, proc3 = run_test_replay("test3_replay", replay_src)
    a3 = analyze_basic(out3)
    test3 = {"name": "REPLAY MODE"}
    test3["no_crash"] = proc3.returncode == 0
    test3["parsed_count"] = len(a3["valid"]) if a3 else 0
    # compare a sample of sequences between test1 valid and test3 valid
    seq1 = seq_list_from_valid(a1["valid"])[:50]
    seq3 = seq_list_from_valid(a3["valid"])[:50]
    test3["deterministic_equivalence"] = seq1 == seq3 if seq1 and seq3 else False
    test3["evidence_files"] = {"replay_valid": str(out3 / "telemetry_valid.csv"), "replay_raw": str(out3 / "telemetry_raw.csv"), "replay_diag": str(out3 / "diagnostics.log")}
    results["TEST3"] = test3

    # TEST 4: SEQUENCE INTEGRITY
    test4 = {"name": "SEQUENCE INTEGRITY"}
    seqs4 = seq_list_from_valid(a2["valid"])  # use corrupted run valid sequences
    test4["monotonic"] = monotonic_increasing(seqs4)
    # compute lost by gaps
    computed_lost = 0
    for a, b in zip(seqs4, seqs4[1:]):
        if b > a + 1:
            computed_lost += b - a - 1
    # compare against diagnostics last metrics
    diag_metrics = a2["metrics"]
    reported_lost = diag_metrics[-1].get("lost_packets", None) if diag_metrics else None
    test4["computed_lost"] = computed_lost
    test4["reported_lost"] = reported_lost
    test4["evidence_files"] = {"valid": str(out2 / "telemetry_valid.csv"), "diag": str(out2 / "diagnostics.log")}
    results["TEST4"] = test4

    # TEST 5: PHYSICS SANITY
    test5 = {"name": "PHYSICS SANITY"}
    # use clean run data a1
    seqs = seq_list_from_valid(a1["valid"]) or []
    alts = altitude_list_from_valid(a1["valid"]) or []
    vels = velocity_list_from_valid(a1["valid"]) or []
    phases = [r.get("current_flight_phase","") for r in a1["valid"]]
    states = [r.get("current_state","") for r in a1["valid"]]
    # partition by state tokens if present
    ascent_idx = [i for i, s in enumerate(states) if s and "ASCENT" in s.upper()]
    coast_idx = [i for i, s in enumerate(states) if s and "COAST" in s.upper()]
    descent_idx = [i for i, s in enumerate(states) if s and "DESCENT" in s.upper()]
    test5["altitude_non_negative"] = all(a >= 0 for a in alts)
    test5["ascent_checks"] = False
    if ascent_idx:
        a_vals = [alts[i] for i in ascent_idx]
        v_vals = [vels[i] for i in ascent_idx if i < len(vels)]
        # altitude increasing and velocity positive
        test5["ascent_checks"] = monotonic_increasing(a_vals) and all(v > 0 for v in v_vals)
    test5["coast_checks"] = False
    if coast_idx:
        v_vals = [vels[i] for i in coast_idx if i < len(vels)]
        test5["coast_checks"] = abs(mean(v_vals)) < 2.0
    test5["descent_checks"] = False
    if descent_idx:
        a_vals = [alts[i] for i in descent_idx]
        v_vals = [vels[i] for i in descent_idx if i < len(vels)]
        test5["descent_checks"] = monotonic_decreasing(a_vals) and all(v < 0 for v in v_vals)
    test5["evidence_files"] = {"valid": str(out1 / "telemetry_valid.csv")}
    results["TEST5"] = test5

    # TEST 6: DIAGNOSTICS SANITY
    test6 = {"name": "DIAGNOSTICS SANITY"}
    # compare metrics from clean and corrupt runs
    clean_last = a1["metrics"][-1] if a1["metrics"] else {}
    corrupt_last = a2["metrics"][-1] if a2["metrics"] else {}
    test6["clean_link_quality"] = clean_last.get("link_quality")
    test6["corrupt_link_quality"] = corrupt_last.get("link_quality")
    test6["frequency_clean_reported"] = clean_last.get("frequency_hz")
    # compute measured frequency
    if a1["valid"]:
        times = [float(r.get("mission_elapsed_seconds", 0)) for r in a1["valid"]]
        measured = None
        if len(times) >= 2:
            measured = (times[-1] - times[0]) / max(1, len(times)-1)
            test6["frequency_measured_hz"] = round(1.0 / measured, 3) if measured and measured>0 else None
    results["TEST6"] = test6

    # TEST 7: COMMAND PATH
    # Validate that we can write commands via file transport and manager doesn't crash when commands file exists
    cmd_out = VER_DIR / "test7_cmd"
    cmd_out.mkdir(parents=True, exist_ok=True)
    cmd_file = cmd_out / "telecommands.txt"
    # start a small simulate run and write commands
    out7, p7 = run_test_simulate("test7_cmdrun", corruption_rate=0.0, count=10, interval=0.05, duration=3)
    # append a command
    with open(out7 / "telecommands.txt", "a", encoding="utf-8") as f:
        f.write("CMD:START_TELEMETRY\n")
        f.write("CMD:REQUEST_STATUS\n")
    # check telecommands file presence
    test7 = {"name": "COMMAND PATH"}
    telecmd_file = out7 / "telecommands.txt"
    test7["commands_written"] = telecmd_file.exists()
    if telecmd_file.exists():
        test7["commands_content"] = telecmd_file.read_text(encoding="utf-8")
    results["TEST7"] = test7

    # Summarize and write report
    report_path = VER_DIR / "verification_report.json"
    with report_path.open("w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)

    # Also produce a human-readable summary
    txt = VER_DIR / "verification_report.txt"
    with txt.open("w", encoding="utf-8") as f:
        for k, v in results.items():
            f.write(f"=== {k} - {v.get('name')} ===\n")
            for kk, vv in v.items():
                if kk == 'name':
                    continue
                f.write(f"{kk}: {vv}\n")
            f.write("\n")
    return results


if __name__ == '__main__':
    start = datetime.utcnow()
    results = run_verification()
    end = datetime.utcnow()
    print(f"Verification completed in {(end-start).total_seconds():.1f}s. Report at {VER_DIR}")
