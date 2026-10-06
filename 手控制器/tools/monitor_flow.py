"""Read-only HC/FD logger. Does not write to serial or toggle DTR/RTS.

python monitor_flow.py --port COM21 --output D:/Espressif/logs/flow-test --seconds 300
Create output/stop.request to stop cleanly. Aircraft operation stays with the user.
"""
import argparse
import datetime
import json
from pathlib import Path
import time


def signed(word, scale=1):
    if word == 0x8000:
        return None
    return (word - 65536 if word >= 32768 else word) / scale


def decode_flow(frame):
    """Preserve absent pages as absent, never turn missing values into zero."""
    if frame.get('v') != 1 or len(frame.get('pages', [])) != 5:
        raise ValueError('Unsupported diagnostic schema')
    mask = frame['mask']
    if not isinstance(mask, int) or not 0 <= mask <= 31:
        raise ValueError('Invalid page mask')
    result = {}
    for i, page in enumerate(frame['pages']):
        if not mask & (1 << i):
            if page is not None:
                raise ValueError('Absent page has payload')
            continue
        if not isinstance(page, list) or len(page) != 6 or any(
                not isinstance(n, int) or not 0 <= n <= 65535 for n in page):
            raise ValueError('Invalid diagnostic page')
        if i == 0:
            flags = page[5]
            result.update(quality=page[0] >> 8, control_state=page[0] & 255,
                          source_age_ms=None if page[1] == 65535 else page[1],
                          received_pitch=page[2], received_roll=page[3],
                          target_captures=page[4], flags=flags,
                          source_fresh=bool(flags & 1), position_ready=bool(flags & 2),
                          loop_active=bool(flags & 32), clipped=bool(flags & 64),
                          nonfinite=bool(flags & 128), attitude_valid=bool(flags & 256))
        elif i == 1:
            result.update(zip(('velocity_x_native', 'velocity_y_native', 'position_x_native',
                               'position_y_native', 'target_x_native', 'target_y_native'),
                              (signed(n, 10) for n in page)))
        elif i == 2:
            result.update(zip(('velocity_target_x_native', 'velocity_target_y_native',
                               'velocity_pid_x', 'velocity_pid_y', 'pitch_target_deg', 'roll_target_deg'),
                              (signed(n, 10 if j < 2 else 100) for j, n in enumerate(page))))
        elif i == 3:
            result.update(pitch_deg=signed(page[0], 100), roll_deg=signed(page[1], 100),
                          motor_pwm=page[2:])
        else:
            result.update(raw_dx=signed(page[0]), raw_dy=signed(page[1]),
                          invalidations=page[2], source_frames=page[3],
                          height_mm=None if page[4] == 65535 else page[4],
                          motor_low_mask=page[5] & 15, motor_high_mask=(page[5] >> 4) & 15)
    if mask & 2:
        for axis in ('x', 'y'):
            actual, target = result[f'position_{axis}_native'], result[f'target_{axis}_native']
            result[f'position_error_{axis}_native'] = None if actual is None or target is None else target - actual
    return result


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=300)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 1800:
        parser.error('--seconds must be 1..1800')
    args.output.mkdir(parents=True, exist_ok=True)
    if (args.output / 'raw.log').exists():
        parser.error('Use a new output directory to preserve previous tests')
    port = serial.Serial(port=None, baudrate=115200, timeout=.2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()
    start = time.monotonic()
    count = {'HC': 0, 'FD': 0, 'errors': 0}
    buffer = b''
    print('READ_ONLY_MONITOR_READY', args.port, str(args.output), flush=True)
    try:
        with (args.output / 'raw.log').open('wb', buffering=0) as raw, \
             (args.output / 'frames.jsonl').open('w', encoding='utf-8', buffering=1) as frames:
            while time.monotonic() - start < args.seconds and not (args.output / 'stop.request').exists():
                data = port.read(port.in_waiting or 1)
                if not data:
                    continue
                raw.write(data)
                buffer += data
                while b'\n' in buffer:
                    line, buffer = buffer.split(b'\n', 1)
                    starts = [(line.find(prefix), kind) for prefix, kind in ((b'@HC:', 'HC'), (b'@FD:', 'FD'))]
                    starts = [(index, kind) for index, kind in starts if index >= 0]
                    if not starts:
                        continue
                    index, kind = min(starts)
                    try:
                        frame = json.JSONDecoder().raw_decode(line[index+4:].decode('utf-8'))[0]
                        if kind == 'FD':
                            frame['decoded'] = decode_flow(frame)
                        frame['_kind'] = kind
                        frame['_received_at'] = datetime.datetime.now().astimezone().isoformat()
                        frame['_elapsed_s'] = round(time.monotonic() - start, 3)
                        frames.write(json.dumps(frame, ensure_ascii=False) + '\n')
                        count[kind] += 1
                    except (ValueError, TypeError, KeyError):
                        count['errors'] += 1
                if len(buffer) > 65536:
                    count['errors'] += 1
                    buffer = b''
    finally:
        port.close()
        count.update(read_only=True, duration_s=round(time.monotonic()-start, 3), running=False)
        (args.output / 'summary.json').write_text(json.dumps(count, indent=2), encoding='utf-8')
        print(json.dumps(count), flush=True)


if __name__ == '__main__':
    main()
