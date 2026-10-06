#!/usr/bin/env python3
"""Compare reconstructed C against the original Thumb machine code.

Dependency: unicorn (tested with version printed in validation_report.json).
Hardware communication is stubbed; this does not validate electrical timing.
"""
import ctypes as ct
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import unicorn
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1

ROOT = Path(__file__).resolve().parent
BINARY = ROOT.parent / "STM32F103C8T6_backup.bin"
BUILD = ROOT / "build"
BUILD.mkdir(exist_ok=True)
data = BINARY.read_bytes()
assert hashlib.sha256(data).hexdigest() == "cbe3d9ff29cbe4ed8b158dc277fd6280a572135573af9f70b42d96420f9ad43d"
subprocess.run(["gcc", "-shared", "-fPIC", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-ffp-contract=off", str(ROOT / "remote_logic.c"), "-o", str(BUILD / "libremote_logic.so")], check=True)

class State(ct.Structure):
    _fields_ = [("channels", ct.c_uint16 * 8), ("packet", ct.c_uint8 * 32),
                ("previous_ch0", ct.c_uint16), ("previous_ch1", ct.c_uint16),
                ("previous_ch3", ct.c_uint16), ("trim", ct.c_int16 * 5),
                ("key_state", ct.c_uint8), ("control_divider", ct.c_uint8),
                ("tick_ms", ct.c_uint32), ("last_key_tick", ct.c_uint32)]

class Action(ct.Structure):
    _fields_ = [("status_pin", ct.c_int8), ("write_eeprom", ct.c_bool),
                ("eeprom_address", ct.c_uint8), ("eeprom_value", ct.c_uint16)]

lib = ct.CDLL(str(BUILD / "libremote_logic.so"))
lib.rc_compute_channels.argtypes = [ct.POINTER(State), ct.POINTER(ct.c_uint16)]
lib.rc_build_packet.argtypes = [ct.POINTER(State)]
lib.rc_scan_keys.argtypes = [ct.POINTER(State), ct.c_uint32, ct.c_uint32, ct.c_uint32]
lib.rc_scan_keys.restype = Action
lib.rc_tick.argtypes = [ct.POINTER(State)]
lib.rc_tick.restype = ct.c_bool
rng = random.Random(103)
counts = {"channel_cases": 0, "packet_cases": 0, "key_cases": 0, "systick_cases": 0}

def emulator(s, adc=(0, 0, 0, 0)):
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.mem_map(0x08000000, 0x10000)
    uc.mem_write(0x08000000, data)
    uc.mem_map(0x20000000, 0x5000)
    uc.mem_write(0x20000000, data[0x20f8:0x2160])
    uc.mem_map(0x40000000, 0x30000)
    uc.mem_map(0xe000e000, 0x1000)
    uc.mem_write(0x20000004, struct.pack('<I', s.tick_ms))
    uc.mem_write(0x20000008, bytes([s.control_divider]))
    uc.mem_write(0x2000001e, struct.pack('<4H', *adc))
    uc.mem_write(0x20000028, struct.pack('<8H', *s.channels))
    uc.mem_write(0x20000038, bytes(s.packet))
    uc.mem_write(0x2000005a, struct.pack('<3H', s.previous_ch0, s.previous_ch1, s.previous_ch3))
    uc.mem_write(0x20000060, bytes([s.key_state]))
    uc.mem_write(0x20000064, struct.pack('<I', s.last_key_tick))
    uc.mem_write(0x20000068, struct.pack('<5h', *s.trim))
    uc.reg_write(UC_ARM_REG_SP, 0x20004ff0)
    uc.reg_write(UC_ARM_REG_LR, 0x0800f001)
    return uc

def execute(uc, entry, stubs):
    calls = []
    def code_hook(u, address, size, _):
        if address == 0x0800f000:
            u.emu_stop()
        elif address in stubs:
            calls.append((address, u.reg_read(UC_ARM_REG_R0), u.reg_read(UC_ARM_REG_R1)))
            u.reg_write(UC_ARM_REG_R0, 0)
            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))
    uc.hook_add(UC_HOOK_CODE, code_hook)
    uc.emu_start(entry | 1, 0x0800f000, count=100000)
    assert uc.reg_read(UC_ARM_REG_PC) == 0x0800f000, "Original function failed to return"
    return calls

def initial():
    s = State()
    s.channels[:] = [1000] * 8
    s.packet[:4] = [0xaa, 0xaf, 0x03, 0x1b]
    return s

for i in range(600):
    s = initial()
    adc = [rng.randrange(4096) for _ in range(4)]
    if i < 8:
        adc = [[0]*4, [4095]*4, [2048]*4, [1]*4, [3]*4, [4]*4, [3999]*4, [4000]*4][i]
    s.previous_ch0, s.previous_ch1, s.previous_ch3 = [rng.randrange(65536) for _ in range(3)]
    s.trim[:] = [rng.randrange(-32768, 32768) for _ in range(5)]
    uc = emulator(s, adc)
    execute(uc, 0x080015c0, {0x080019f8, 0x08001188})
    lib.rc_compute_channels(ct.byref(s), (ct.c_uint16*4)(*adc))
    expected = struct.unpack('<8H', uc.mem_read(0x20000028, 16))
    history = struct.unpack('<3H', uc.mem_read(0x2000005a, 6))
    assert tuple(s.channels) == expected, (i, adc, list(s.channels), expected)
    assert (s.previous_ch0, s.previous_ch1, s.previous_ch3) == history
    counts['channel_cases'] += 1

for i in range(200):
    s = initial()
    s.channels[:] = [rng.randrange(65536) for _ in range(8)]
    if i > 0:
        s.packet[:] = [rng.randrange(256) for _ in range(32)]
    uc = emulator(s)
    execute(uc, 0x08001188, {0x08001098})
    lib.rc_build_packet(ct.byref(s))
    assert bytes(s.packet) == bytes(uc.mem_read(0x20000038, 32)), i
    counts['packet_cases'] += 1

for i in range(800):
    s = initial()
    s.key_state = i % 4
    s.tick_ms = rng.randrange(2**32)
    s.last_key_tick = (s.tick_ms - [0, 30, 31, 100, 2**32-10][i % 5]) & 0xffffffff
    s.trim[1], s.trim[2] = rng.choice([-255,-250,-249,0,245,249,250,255]), rng.choice([-255,-250,-249,0,245,249,250,255])
    a, b, c = 0xffff, 0xffff, 0xffff
    masks = [(0,0x8000),(1,0x0800),(2,0x4000),(2,0x8000),
             (0,0x0040),(0,0x0080),(1,0x0001),(1,0x0002)]
    values = [a,b,c]
    for port, bit in masks:
        if rng.randrange(4) == 0: values[port] &= ~bit
    a,b,c = values
    uc = emulator(s)
    for address, value in [(0x40010808,a),(0x40010c08,b),(0x40011008,c)]:
        uc.mem_write(address, struct.pack('<I', value))
    writes = []
    def write_hook(u, access, address, size, value, _):
        if address in (0x40010810,0x40010814): writes.append((address,value))
    uc.hook_add(UC_HOOK_MEM_WRITE, write_hook)
    calls = execute(uc, 0x080019f8, {0x08001d06})
    action = lib.rc_scan_keys(ct.byref(s), a,b,c)
    assert bytes([s.key_state]) == bytes(uc.mem_read(0x20000060,1))
    assert s.last_key_tick == struct.unpack('<I',uc.mem_read(0x20000064,4))[0]
    assert tuple(s.channels) == struct.unpack('<8H',uc.mem_read(0x20000028,16))
    assert tuple(s.trim) == struct.unpack('<5h',uc.mem_read(0x20000068,10))
    expected_calls = [] if not action.write_eeprom else [(0x08001d06,action.eeprom_address,action.eeprom_value)]
    assert calls == expected_calls, (i, calls, expected_calls)
    expected_writes = [] if action.status_pin == -1 else [(0x40010810 if action.status_pin == 1 else 0x40010814, 0x1000)]
    assert writes == expected_writes, (i, writes, expected_writes)
    counts['key_cases'] += 1

for i in range(256):
    s = initial()
    s.control_divider = i
    s.tick_ms = rng.randrange(2**32)
    uc = emulator(s)
    calls = execute(uc, 0x080018cc, {0x080015c0})
    should_update = lib.rc_tick(ct.byref(s))
    assert should_update == bool(calls)
    assert s.control_divider == uc.mem_read(0x20000008,1)[0]
    assert s.tick_ms == struct.unpack('<I',uc.mem_read(0x20000004,4))[0]
    counts['systick_cases'] += 1

report = {"source_sha256": hashlib.sha256(data).hexdigest(), "unicorn_version": unicorn.__version__,
          "result": "PASS", "cases": counts, "total_cases": sum(counts.values()),
          "validated": ["ADC scaling, float filtering, wrap and clamp", "32-byte packet including original adjacent-memory reads",
                        "key state, priority, trim bounds and EEPROM write requests", "SysTick post-increment schedule"],
          "not_validated": ["electrical pin wiring", "GPIO bit-bang timing", "live radio and EEPROM", "complete boot on hardware"]}
(ROOT / 'validation_report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
