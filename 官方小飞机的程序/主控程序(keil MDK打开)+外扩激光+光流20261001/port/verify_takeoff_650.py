"""Execute the built Thumb firmware offline; no serial, SWD or hardware writes."""
from pathlib import Path
import hashlib, json

src = Path(__file__).with_name('verify.py')
ns = {'__file__': str(src), '__name__': 'takeoff_650_offline'}
exec(compile(src.read_text(encoding='utf-8').split('checks=[]')[0], str(src), 'exec'), ns)
Machine, fb, ROOT = ns['Machine'], ns['fb'], ns['ROOT']
checks = []


class ScheduledMachine(Machine):
    """Run the6ms height task independently of arbitrary absolute clock offsets."""
    def __init__(self):
        super().__init__()
        self.height_tick = None

    def tick(self, now, mm=None):
        self.now(now)
        self.h('last_ms', now)
        self.call('hand_link_imu', 1)
        if mm is not None:
            self.call('hand_link_height', 1, mm)
            if mm:
                self.put('VL53L01_high', 'I', mm)
        self.call('hand_link_tick')
        self.call('FlightPidControl', fb(.003))
        self.call('MotorControl')
        self.call('hand_link_motor_observe')
        if self.height_tick is None or ((now - self.height_tick) & 0xffffffff) >= 6:
            self.call('HeightPidControl', fb(.006))
            self.height_tick = now


Machine = ScheduledMachine


def check(name, ok):
    assert ok, name
    checks.append(name)


def started(ground=0, start=1600):
    m = Machine()
    m.now((start - 600) & 0xffffffff)
    m.call('hand_link_imu', 1)
    m.call('hand_link_height', 1, ground)
    m.send(2, 1)
    m.now(start)
    m.call('hand_link_imu', 1)
    m.call('hand_link_height', 1, ground)
    m.send(1, 2)
    check('explicit arm still required', m.h('stage') == 1 and m.pwm() == (0, 0, 0, 0))
    m.send(3, 3)
    check('takeoff starts at the original threshold', m.h('stage') == 2 and m.get('Remote', '8H')[2] == 1101)
    return m


def advance(m, start, stop, height):
    for t in range(start, stop, 3):
        m.tick(t & 0xffffffff, height)


def ramp(m):
    return m.get('ramp_collective', 'f')[0]


# Ground / missing range cannot unlock the new headroom.
m = started()
advance(m, 1603, 3604, 0)
check('zero ground reaches only original 500 cap', m.h('collective') == 500 and m.get('Remote', '8H')[2] == 1667)
advance(m, 3604, 5605, 0)
check('zero ground never grows to 650', ramp(m) == 500 and m.h('stage') == 2)
m = started(ground=100)
advance(m, 1603, 4604, 149)
check('absolute range alone is insufficient: relative49mm blocks boost', ramp(m) == 500)
advance(m, 4604, 5906, 150)
check('relative50mm enables boost up to650 without false hold', ramp(m) == 650 and m.h('stage') == 2)
check('650 maps to existing maximum channel2000', m.get('Remote', '8H')[2] == 2000 and m.h('collective') == 650)
check('mixed PWM remains within the unchanged hardware range', all(0 <= x <= 1000 for x in m.pwm()))

# Preserve the original ramp rate, including the part above500.
m = started()
advance(m, 1603, 3601, 100)
check('lifted before2s does not ramp faster', 498 < ramp(m) < 500)
advance(m, 3601, 4003, 100)
check('2.4s approximately550 rather than instantaneous650', 549 <= ramp(m) <= 552)
advance(m, 4003, 4780, 100)
check('just before3.18s still below650', 648 <= ramp(m) < 650)
advance(m, 4780, 4810, 100)
check('original slope reaches650 around3.18s', ramp(m) == 650)
advance(m, 4810, 5500, 100)
check('upper cap is hard650 even with continued valid data', ramp(m) == 650 and m.h('collective') == 650)

# Invalid / stale data pause further increases; never step a boosted base down.
m = started()
advance(m, 1603, 4402, 100)
t = m.get('hand_milliseconds', 'I')[0]
before = ramp(m)
m.tick(t + 99, None)
check('fresh sample99ms allows only ordinary ramp increment', before < ramp(m) < before + 13)
m.tick(t + 100, None)
at_boundary = ramp(m)
m.tick(t + 101, None)
check('stale101ms freezes existing boosted base', ramp(m) == at_boundary and ramp(m) > 500)
for mm in (0, 49, 3500):
    t = m.get('hand_milliseconds', 'I')[0] + 3
    before = ramp(m)
    m.tick(t, mm)
    check('unusable boost range' + str(mm) + ' cannot increase or drop output', ramp(m) == before)
t = m.get('hand_milliseconds', 'I')[0] + 3
m.now(t)
m.h('last_ms', t)
m.call('hand_link_imu', 1)
m.call('hand_link_height', 0, 0)
m.call('hand_link_tick')
check('failed range read also freezes boost', ramp(m) == before)
t += 3
m.tick(t, 100)
check('fresh range resumes at original slope without catch-up', 0 < ramp(m) - before < .5)
before = ramp(m)
m.tick(t + 150, 100)
check('scheduler stall has no accumulated jump', ramp(m) == before)
m.tick(t + 153, 100)
check('normal ticks resume gradually after scheduler stall', 0 < ramp(m) - before < .5)

# Existing mode transition remains relative200mm with its100ms confirmation.
m = started(ground=100)
advance(m, 1603, 5002, 150)
check('raised ground can still reach new cap', ramp(m) == 650)
advance(m, 5002, 5302, 299)
check('relative199mm does not enable assist', m.h('stage') == 2)
t = m.get('hand_milliseconds', 'I')[0] + 3
for elapsed in range(0, 100, 3):
    m.tick(t + elapsed, 300)
    check('relative200mm waits for dwell' + str(elapsed), m.h('stage') == 2)
m.tick(t + 102, 300)
check('existing100ms dwell enters height/flow mode', m.h('stage') == 3 and m.get('ALL_flag', 'B')[0] == 19)
check('native height entry retains boosted650 base', m.get('thr_hold', 'H')[0] == 650)
advance(m, t + 105, t + 150, 300)
check('height target captures measured300mm', abs(m.get('pidHeightHigh', 'f')[0] - 300) < 1)
check('held assist never continues the takeoff ramp', ramp(m) == 650)

# Native/manual control keeps its original default; HC3 retains only bounded bases.
for owned in (0, 1):
    for seed in (0, 200, 205, 245, 500, 550, 600, 650, 651, 1000):
        m = Machine()
        gains = {n: m.get(n, '13f')[4:7] for n in ('pidHeightHigh', 'pidHeightRate')}
        m.put('ALL_flag', 'B', 1)
        m.call('HeightPidControl', fb(.006))
        m.h('owned', owned)
        m.put('thr_hold', 'H', seed)
        m.put('ALL_flag', 'B', 3)
        m.put('VL53L01_high', 'I', 300)
        m.call('HeightPidControl', fb(.006))
        expected = seed if owned and 205 <= seed <= 650 else 500
        check('handoff base owner' + str(owned) + ' seed' + str(seed), m.get('thr_hold', 'H')[0] == expected)
        check('handoff keeps PID gains owner' + str(owned) + ' seed' + str(seed), all(m.get(n, '13f')[4:7] == v for n, v in gains.items()))

# If200mm is reached sooner, assist takes over below650 rather than waiting for cap.
m = started()
advance(m, 1603, 2803, 100)
advance(m, 2803, 2930, 200)
check('early verified liftoff enters assist without climbing to650', m.h('stage') == 3 and 390 < m.get('thr_hold', 'H')[0] < 430)

# Emergency / lease / IMU / landing / no-liftoff safeguards at the new output.
for reason in ('disarm', 'lease', 'imu'):
    m = started()
    advance(m, 1603, 5002, 100)
    check('setup new output before ' + reason, ramp(m) == 650)
    if reason == 'disarm':
        m.send(2, 4)
    elif reason == 'lease':
        m.now(m.get('hand_milliseconds', 'I')[0] + 601)
        m.call('hand_link_tick')
    else:
        now = m.get('hand_milliseconds', 'I')[0] + 101
        m.now(now)
        m.h('last_ms', now)
        m.call('hand_link_tick')
    check('new650 output cannot bypass ' + reason, m.pwm() == (0, 0, 0, 0) and not(m.get('ALL_flag', 'B')[0] & 1))
m = started()
advance(m, 1603, 5002, 100)
m.send(4, 4)
before = m.h('collective')
advance(m, 5002, 6004, 100)
check('landing from650 keeps original25units_per_second descent', 24 <= before - m.h('collective') <= 26 and m.h('stage') == 4)
m = started()
advance(m, 1603, 16702, 0)
check('no liftoff keeps existing15s manual fallback and does not boost', m.h('stage') == 7 and m.h('first_event') == 11 and m.h('collective') == 500)

# Unsigned scheduler clock wrap must not reset or accelerate the ramp.
start = 0xffffff00
m = started(start=start)
advance(m, start + 3, start + 3402, 100)
check('new ramp survives uint32 clock wrap', ramp(m) == 650 and m.h('stage') == 2)

result = {'passed': len(checks), 'failed': 0, 'hardware_access': False,
          'tests': checks, 'firmware_sha256': hashlib.sha256((ROOT/'Output/gcc/official_hand.template.bin').read_bytes()).hexdigest()}
(ROOT/'Output/gcc/takeoff_650_verification.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps({k: v for k, v in result.items() if k != 'tests'}))
