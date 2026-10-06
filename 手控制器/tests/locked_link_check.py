#!/usr/bin/env python3
"""Explicit hardware check: START3/PING/STOP only. Never arm or start motors.

Close the desktop serial monitor before running. Append raw evidence to the
existing communication_diagnosis.log; print the final summary as JSON.
"""
import argparse
import datetime
import json
from pathlib import Path
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--telemetry", action="store_true", help="Also verify aircraft attitude and UART3 cm range")
    args = parser.parse_args()
    if not 10 <= args.seconds <= 600:
        parser.error("seconds must be 10..600")
    path = Path(__file__).resolve().parents[1] / "build/communication_diagnosis.log"
    rows = []
    started = False
    began = time.monotonic()
    last_ping = 0
    bad_json = 0
    with path.open("a", buffering=1) as log, serial.Serial(args.port, 115200, timeout=.05, write_timeout=1) as port:
        log.write("\nHC3_PROTECTION_LOCKED_CHECK " + datetime.datetime.now().astimezone().isoformat() + "\n")
        port.reset_input_buffer()

        def command(action):
            assert action in ("START3", "PING", "STOP")
            port.write(f"@T,{action}\n".encode())

        def read_frame():
            nonlocal bad_json
            line = port.readline().decode("utf-8", "replace").strip()
            if not line.startswith("@HC:"):
                if line:
                    log.write("RAW " + line + "\n")
                return None
            try:
                frame = json.loads(line[4:])
            except ValueError:
                bad_json += 1
                log.write("BAD_JSON " + line + "\n")
                return None
            frame["test_elapsed_s"] = round(time.monotonic()-began, 3)
            log.write(json.dumps(frame, ensure_ascii=False) + "\n")
            rows.append(frame)
            return frame

        initial = None
        while time.monotonic()-began < 6:
            candidate = read_frame()
            if candidate is not None:
                initial = candidate
                assert initial["tx_phase"] in (0, 10), "Existing session active; leave it untouched"
                if initial.get("rc_ready"):
                    break
        assert initial is not None, "No telemetry"
        assert initial["tx_phase"] in (0, 10), "Existing session active; leave it untouched"
        assert initial.get("rc_ready"), "Radio not ready"
        assert "air_diag_age_ms" in initial, "New diagnostic firmware is not running"
        before = initial["tx_count"]
        try:
            command("START3")
            started = True
            began = time.monotonic()
            progress = 30
            while time.monotonic()-began < args.seconds:
                now = time.monotonic()
                if now-last_ping >= .2:
                    command("PING")
                    last_ping = now
                frame = read_frame()
                if frame:
                    assert frame["tx_phase"] in (0, 1), "Unexpected transmitter phase"
                    assert not (frame.get("air_flags", 0) & 1), "Aircraft reports armed"
                    assert frame.get("air_collective", 0) == 0, "Nonzero collective"
                if now-began >= progress:
                    print(f"locked check: {progress}s", flush=True)
                    progress += 30
        finally:
            if started:
                command("STOP")
                deadline = time.monotonic()+3
                while time.monotonic() < deadline:
                    if time.monotonic()-last_ping >= .2:
                        command("PING")
                        last_ping = time.monotonic()
                    read_frame()

    final = rows[-1]
    ready = [f for f in rows if f["tx_phase"] == 1]
    assert final["tx_phase"] == 0, "STOP has not completed"
    assert ready and any(f.get("air_valid") for f in ready), "No aircraft feedback"
    assert final["air_flags"] & 1 == 0
    summary = {
        "duration_s": args.seconds, "commands": ["START3", "PING", "STOP"],
        "arm_or_takeoff_sent": False, "samples": len(rows), "bad_json": bad_json,
        "local_completed": final["tx_count"]-before, "air_accepted_low16": final["air_rx_count"],
        "hand_valid_replies": final["air_frames"], "max_reply_gap_ms": final["air_max_gap_ms"],
        "air_rx_max_gap_ms": final["air_rx_max_gap_ms"], "tx_rx_max_us": final["tx_rx_max_us"],
        "tx_rx_late": final["tx_rx_late"], "air_first_event": final["air_first_event"],
        "air_bad": final["air_bad"], "local_errors": final["tx_errors"],
        "ready_warning_samples": sum(bool(f.get("link_warning")) for f in ready),
        "final_phase": final["tx_phase"],
        "all_disarmed": all(not (f.get("air_flags", 0) & 1) for f in rows),
        "final_rejects": {k: final[k] for k in ("reject_schema", "reject_session", "reject_sequence")},
    }
    if args.telemetry:
        unique = {}
        for f in ready:
            if f.get("air_telem_received") and f.get("air_telem_age_ms", -1) >= 0:
                unique.setdefault(f["air_telem_frames"], f)
        received = list(unique.values())
        assert len(received) >= args.seconds*2, "Insufficient 300ms telemetry"
        assert all((20 <= f["air_distance_cm"] <= 720) if f["air_distance_valid"] else f["air_distance_cm"] == 65535 for f in received)
        valid = [f for f in received if f["air_attitude_valid"]]
        assert len(valid) >= len(received)*.9, "Aircraft IMU telemetry invalid"
        assert all(-18000 <= f[key] <= 18000 for f in valid for key in ("air_pitch_cdeg", "air_roll_cdeg"))
        gaps = [f["air_telem_gap_ms"] for f in received if f["air_telem_gap_ms"]]
        summary["telemetry"] = dict(frames=len(received), valid_attitude=len(valid),
            gap_min_ms=min(gaps), gap_max_ms=max(gaps), gap_mean_ms=round(sum(gaps)/len(gaps),1),
            pitch_deg=valid[-1]["air_pitch_cdeg"]/100, roll_deg=valid[-1]["air_roll_cdeg"]/100,
            distance=received[-1]["air_distance_cm"] if received[-1]["air_distance_valid"] else "unavailable", distance_unit="cm", valid_range=[20,720])
    with path.open("a") as log:
        log.write("SUMMARY " + json.dumps(summary) + "\n")
    print(json.dumps(summary, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
