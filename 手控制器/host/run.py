#!/usr/bin/env python3
"""Hand controller serial desktop monitor (Tkinter + pySerial)."""

import argparse
import json
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk

import serial
from serial.tools import list_ports


BG = "#0b121d"
PANEL = "#142131"
PANEL_ALT = "#1c2e3f"
LINE = "#304658"
TEXT = "#edf4f7"
MUTED = "#8ca3b0"
ACCENT = "#c7e765"
PINK = "#eb9697"
ESPRESSIF_USB_IDS = {(0x303A, 0x1001), (0x303A, 0x4001)}
PATTERNS = ("正向逐个点亮", "来回往返", "填满再熄灭")
GESTURES = ((1, "平放前进"), (2, "左倾"), (3, "右倾"),
            (4, "手抬起"), (5, "手下垂"), (6, "掌心按住"))


TRACE = logging.getLogger("hand_controller.tx")

def imu_calibration_text(frame):
    state = frame.get("imu_cal")
    if state is None or state == 3:
        return None  # Older firmware / completed startup calibration.
    if state == 2:
        return "检测到晃动 · 等待静止重新校准"
    if state == 4:
        return "IMU 读取异常 · 等待恢复后校准"
    progress = min(100, max(0, int(frame.get("imu_cal_progress", 0))))
    return f"陀螺仪上电校准 · {progress}%"

def setup_trace():
    if TRACE.handlers:
        return
    path = Path(__file__).resolve().parents[1] / "build" / "tx_runtime.log"
    path.parent.mkdir(exist_ok=True)
    handler = RotatingFileHandler(path, maxBytes=2_000_000, backupCount=1, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(asctime)s %(message)s"))
    TRACE.addHandler(handler)
    TRACE.setLevel(logging.INFO)


def find_port(selected):
    if selected != "自动":
        return selected
    for item in list_ports.comports():
        if (item.vid, item.pid) in ESPRESSIF_USB_IDS:
            return item.device
    return None


class SerialLink:
    def __init__(self, events):
        self.events = events
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.thread = None
        self.port = None

    def start(self, selected):
        self.stop()
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._run, args=(selected, self.stop_event), daemon=True)
        self.thread.start()

    def stop(self):
        self.stop_event.set()
        with self.lock:
            if self.port is not None:
                try:
                    self.port.close()
                except (OSError, serial.SerialException):
                    pass
                self.port = None
        if self.thread is not None:
            self.thread.join(timeout=1)
            self.thread = None

    def send_motor(self, channel, strength, duration_ms):
        if channel not in (0, 1) or not 0 <= strength <= 100 or not 0 <= duration_ms <= 2000:
            raise ValueError("电机命令超出允许范围")
        with self.lock:
            if self.port is None:
                raise ConnectionError("串口未连接")
            self.port.write(f"@M,{channel},{strength},{duration_ms}\n".encode("ascii"))

    def send_gesture(self, action, invert_pitch=False, invert_roll=False, swap=False):
        if action not in ("ZERO", "RESET", "DIR"):
            raise ValueError("未知手势设置")
        command = f"@G,{action}"
        if action == "DIR":
            command += f",{int(invert_pitch)},{int(invert_roll)},{int(swap)}"
        with self.lock:
            if self.port is None:
                raise ConnectionError("串口未连接")
            self.port.write((command + "\n").encode("ascii"))

    def send_output(self, action, value=None):
        if action != "AXIS" or not isinstance(value,int) or value not in (0,1):
            raise ValueError("输出设置仅支持左右轴选择")
        command=f"@O,AXIS,{value}"
        with self.lock:
            if self.port is None:
                raise ConnectionError("串口未连接")
            self.port.write((command + "\n").encode("ascii"))

    def send_tx(self, action):
        if action not in ("START3", "ARM", "TAKEOFF", "LAND", "RESUME", "STOP", "PING"):
            raise ValueError("未知发送操作")
        with self.lock:
            if self.port is None:
                raise ConnectionError("串口未连接")
            TRACE.info("COMMAND %s", action)
            self.port.write(f"@T,{action}\n".encode("ascii"))

    def _run(self, selected, stop_event):
        while not stop_event.is_set():
            name = find_port(selected)
            if name is None:
                self.events.put(("status", False, "未找到 ESP32-S3 USB 串口"))
                stop_event.wait(1)
                continue
            port = None
            try:
                with serial.Serial(name, 115200, timeout=0.35, write_timeout=1) as port:
                    with self.lock:
                        self.port = port
                    self.events.put(("status", True, f"{name} 已连接，等待状态帧"))
                    while not stop_event.is_set():
                        raw = port.readline()
                        if not raw:
                            continue
                        line = raw.decode("utf-8", "replace").strip()
                        if not line.startswith("@HC:"):
                            if any(tag in line for tag in ("nrf24_demo:", "NRF24:", "max30102_demo:", "MAX30102:", "flight_keys:", "imu_demo:")):
                                TRACE.info("RADIO %s", line)
                            continue
                        try:
                            frame = json.loads(line[4:])
                        except json.JSONDecodeError:
                            continue
                        if frame.get("v") == 1:
                            keys = ("ms", "gvalid", "gcal", "gesture", "rc_ready", "tx_v2", "tx_v3", "tx_phase", "tx_reason",
                                    "imu_cal", "imu_cal_progress", "gyro_bias_x", "gyro_bias_y", "gyro_bias_z",
                                    "tx_count", "tx_errors", "air_valid", "air_flags", "air_reason", "air_cmd",
                                    "air_rx_count", "air_bad", "air_thr", "air_seq", "air_frames",
                                    "air_age_ms", "air_gap_ms", "air_max_gap_ms", "out_rate", "out",
                                    "tx_can_arm", "tx_can_takeoff", "air_stage", "air_height_valid", "air_ground_ready",
                                    "air_height_mm", "air_relative_mm", "air_collective", "air_radio_faults", "air_detail_age_ms",
                                    "link_warning", "tx_can_resume", "air_resume_ready", "tx_rx_us", "tx_rx_max_us", "tx_rx_late",
                                    "reject_schema", "reject_session", "reject_sequence", "air_rx_gap_ms", "air_rx_max_gap_ms",
                                    "spo2", "spo2_valid", "finger", "max_ready", "ppg_ok", "red", "ir", "ppg_samples", "ppg_age_ms", "spo2_age_ms", "samples", "spo2_stream",
                                    "spo2_held", "ppg_correlation", "ppg_quality", "ppg_contact_resets",
                                    "key_start", "key_stop", "key_start_count", "key_stop_count", "key_status", "key_reason", "key_local", "key_pending", "key_stop_latched", "key_auto",
                                    "air_first_event", "air_diag_age_ms", "air_telem_received", "air_attitude_valid",
                                    "air_pitch_cdeg", "air_roll_cdeg", "air_distance_valid", "air_distance_cm",
                                    "air_telem_age_ms", "air_telem_frames", "air_telem_gap_ms", "air_telem_max_gap_ms")
                            TRACE.info("FRAME %s", json.dumps({k: frame.get(k) for k in keys}, ensure_ascii=False))
                            self.events.put(("frame", frame, name))
            except (OSError, serial.SerialException) as error:
                if not stop_event.is_set():
                    self.events.put(("status", False, f"串口连接失败：{error}"))
            finally:
                with self.lock:
                    if self.port is port:
                        self.port = None
                if not stop_event.is_set():
                    self.events.put(("status", False, "连接中断，正在重试"))
                    stop_event.wait(1)


class App(tk.Tk):
    def __init__(self, initial_port):
        super().__init__()
        setup_trace()
        self.tk.call("tk", "scaling", 1.0)
        self.title("手控制器 · 串口上位机")
        self.geometry("1280x930")
        self.minsize(1100, 880)
        self.configure(bg=BG)
        self.events = queue.SimpleQueue()
        self.link = SerialLink(self.events)
        self.last_frame = None
        self.last_frame_at = 0.0
        self.last_ping_at = 0.0
        self.estop = None
        self.connected = False
        self.zero_pitch = 0.0
        self.zero_roll = 0.0
        self.zeroed = False
        self.invert_pitch = tk.BooleanVar(value=False)
        self.invert_roll = tk.BooleanVar(value=False)
        self.swap_axes = tk.BooleanVar(value=False)
        self.output_axis = tk.StringVar(value="偏航 Yaw")
        self.selected_port = tk.StringVar(value=initial_port or "自动")
        self.strength = tk.IntVar(value=70)
        self.duration = tk.StringVar(value="300 ms")
        self._build()
        self.refresh_ports()
        self.link.start(self.selected_port.get())
        self.after(60, self._poll)
        self.protocol("WM_DELETE_WINDOW", self._close)
        self.bind("<Escape>", self.emergency_stop)

    def _label(self, parent, text, size=11, color=TEXT, weight="normal", **kwargs):
        return tk.Label(parent, text=text, bg=parent.cget("bg"), fg=color,
                        font=("Noto Sans CJK SC", -size, weight), **kwargs)

    def _button(self, parent, text, command, accent=False):
        return tk.Button(parent, text=text, command=command, relief="flat", cursor="hand2",
                         bg=ACCENT if accent else PANEL_ALT,
                         fg="#1d2c16" if accent else TEXT,
                         activebackground="#def38d" if accent else "#28465b",
                         activeforeground="#1d2c16" if accent else TEXT,
                         disabledforeground="#7b8c83", padx=15, pady=9,
                         font=("Noto Sans CJK SC", -12, "bold"), bd=0)

    def _panel(self, parent, row, col, **grid_options):
        outer = tk.Frame(parent, bg=PANEL, highlightbackground=LINE, highlightthickness=1)
        outer.grid(row=row, column=col, sticky="nsew", **grid_options)
        return outer

    def _build(self):
        self.columnconfigure(0, weight=1)
        self.rowconfigure(1, weight=1)
        header = tk.Frame(self, bg=BG, pady=16)
        header.grid(row=0, column=0, sticky="ew", padx=28)
        header.columnconfigure(1, weight=1)
        icon = tk.Label(header, text="✳", bg=ACCENT, fg="#253817", width=2,
                        font=("Noto Sans CJK SC", -22, "bold"))
        icon.grid(row=0, column=0, rowspan=2, padx=(0, 12))
        self._label(header, "手控制器", 17, weight="bold").grid(row=0, column=1, sticky="sw")
        self._label(header, "SERIAL CONTROL DESK", 9, MUTED).grid(row=1, column=1, sticky="nw")
        self.status_dot = self._label(header, "●", 17, MUTED)
        self.status_dot.grid(row=0, column=2, rowspan=2, padx=(15, 4))
        self.status = self._label(header, "正在连接", 10, MUTED)
        self.status.grid(row=0, column=3, rowspan=2, padx=(0, 16))
        ttk.Style(self).configure("TCombobox", font=("Noto Sans CJK SC", -12))
        self.option_add("*TCombobox*Listbox.font", ("Noto Sans CJK SC", -12))
        self.port_picker = ttk.Combobox(header, textvariable=self.selected_port,
                                         state="readonly", width=16,
                                         font=("Noto Sans CJK SC", -12))
        self.port_picker.grid(row=0, column=4, rowspan=2, padx=(0, 8))
        self._button(header, "刷新串口", self.refresh_ports).grid(row=0, column=5, rowspan=2, padx=(0, 8))
        self._button(header, "重新连接", self.reconnect).grid(row=0, column=6, rowspan=2)
        self.estop_button = self._button(header, "急停 · Esc", self.emergency_stop)
        self.estop_button.config(bg="#bf263c", fg="white", activebackground="#e13c52",
                                 activeforeground="white", font=("Noto Sans CJK SC", -15, "bold"))
        self.estop_button.grid(row=0, column=7, rowspan=2, padx=(16, 0))

        ttk.Style(self).configure("TNotebook.Tab", font=("Noto Sans CJK SC", -13), padding=(18, 8))
        self.pages = ttk.Notebook(self)
        self.pages.grid(row=1, column=0, sticky="nsew", padx=28, pady=(0, 12))
        main = tk.Frame(self.pages, bg=BG)
        self.pages.add(main, text="手控制器传感器")
        main.columnconfigure(0, weight=6, uniform="cards")
        main.columnconfigure(1, weight=4, uniform="cards")
        main.rowconfigure(0, weight=6)
        main.rowconfigure(1, weight=4)
        self._build_attitude(self._panel(main, 0, 0, padx=(0, 9), pady=(0, 9)))
        self._build_oxygen(self._panel(main, 0, 1, padx=(9, 0), pady=(0, 9)))
        self._build_button(self._panel(main, 1, 0, padx=(0, 9), pady=(9, 0)))
        self._build_motor(self._panel(main, 1, 1, padx=(9, 0), pady=(9, 0)))

        radio = tk.Frame(self.pages, bg=BG)
        self.pages.add(radio, text="原遥控器 · 无线接收")
        self._build_radio(radio)
        preview = tk.Frame(self.pages, bg=BG)
        self.pages.add(preview, text="手势 → 遥控输出")
        self._build_output(preview)
        aircraft = tk.Frame(self.pages, bg=BG)
        self.pages.add(aircraft, text="飞机遥测")
        self._build_air_telemetry(aircraft)
        self.pages.select(preview)

        foot = tk.Frame(self, bg=BG)
        foot.grid(row=2, column=0, sticky="ew", padx=28, pady=(0, 12))
        self._label(foot, "USB 串口 · 115200 baud · 血氧仅供功能演示", 9, MUTED).pack(side="left")
        self.estop_status = self._label(foot, "急停：锁定飞机并停止控制 · Esc（本窗口内）", 10, MUTED)
        self.estop_status.pack(side="left", padx=(24, 0))
        self.device_time = self._label(foot, "设备时间 --", 9, MUTED)
        self.device_time.pack(side="right")

    def _build_air_telemetry(self, panel):
        top = tk.Frame(panel, bg=PANEL)
        top.pack(fill="x", pady=(10, 12))
        self._heading(top, "AIRCRAFT / 无线回传", "飞机姿态与距离")
        self.air_telem_status = self._label(top, "未连接飞机", 18, MUTED, "bold")
        self.air_telem_status.pack(anchor="w", padx=25, pady=(16, 8))
        self._label(top, "飞机 → NRF24L01 → 手控制器 → USB 串口 · 每 300 ms 更新", 11, MUTED).pack(
            anchor="w", padx=25, pady=(0, 12))
        self._button(top, "前往连接飞机", lambda: self.pages.select(2)).pack(anchor="w", padx=25, pady=(0, 20))
        attitude = tk.Frame(panel, bg=BG)
        attitude.pack(fill="both", expand=True)
        attitude.columnconfigure((0, 1), weight=1, uniform="angles")
        attitude.rowconfigure(0, weight=1)
        values = []
        for column, title in enumerate(("俯仰 Pitch", "横滚 Roll")):
            card = self._panel(attitude, 0, column, padx=(0, 8) if column == 0 else (8, 0))
            self._heading(card, "飞机实际姿态 / °", title)
            value = self._label(card, "--°", 44, TEXT, "bold")
            value.pack(expand=True, pady=24)
            values.append(value)
        self.air_pitch_value, self.air_roll_value = values
        distance = tk.Frame(panel, bg=PANEL)
        distance.pack(fill="x", pady=(16, 0))
        self._heading(distance, "RANGE / 避障测距", "距离 · 20–720 cm")
        self.air_distance_value = self._label(distance, "-- cm", 36, TEXT, "bold")
        self.air_distance_value.pack(anchor="w", padx=25, pady=(10, 4))
        self.air_distance_hint = self._label(distance, "等待有效避障测距", 11, MUTED)
        self.air_distance_hint.pack(anchor="w", padx=25, pady=(0, 12))
        self.air_distance_bar = ttk.Progressbar(distance, maximum=700)
        self.air_distance_bar.pack(fill="x", padx=25, pady=(0, 20))
        self.air_telem_detail = self._label(panel, "等待连接飞机后接收遥测", 10, MUTED)
        self.air_telem_detail.pack(anchor="w", padx=4, pady=16)

    def _render_air_telemetry(self, frame):
        elapsed = max(0, time.monotonic()-self.last_frame_at)*1000
        phase = frame.get("tx_phase", 0)
        active = bool(frame.get("tx_v3")) and phase not in (0, 9, 10)
        linked = active and self._frame_fresh() and self._air_fresh(frame)
        age = frame.get("air_telem_age_ms", -1)
        fresh = linked and frame.get("air_telem_received") and isinstance(age, int) and 0 <= age and age+elapsed < 1000
        status = ("飞机遥测在线" if fresh else "遥测过期，等待更新" if linked and frame.get("air_telem_received") else
                  "已连接，等待首帧遥测" if linked else "等待飞机回传" if active else "未连接飞机")
        radio_unavailable = self._radio_not_ready(frame)
        if radio_unavailable:
            status = "手控 NRF24 未就绪，无法连接飞机"
        self.air_telem_status.config(text=status, fg=PINK if radio_unavailable else ACCENT if fresh else MUTED)
        pitch, roll = frame.get("air_pitch_cdeg"), frame.get("air_roll_cdeg")
        attitude_valid = fresh and frame.get("air_attitude_valid") and all(
            isinstance(x, int) and -18000 <= x <= 18000 for x in (pitch, roll))
        self.air_pitch_value.config(text=f"{pitch/100:+.2f}°" if attitude_valid else "--°")
        self.air_roll_value.config(text=f"{roll/100:+.2f}°" if attitude_valid else "--°")
        cm = frame.get("air_distance_cm")
        distance_valid = fresh and frame.get("air_distance_valid") and isinstance(cm, int) and 20 <= cm <= 720
        self.air_distance_value.config(text=f"{cm} cm" if distance_valid else "-- cm")
        self.air_distance_bar["value"] = cm-20 if distance_valid else 0
        hint = ("距离有效 · cm" if distance_valid else "距离超出 20–720 cm 或数据无效" if fresh and frame.get("air_distance_valid") else
                "等待有效避障测距" if fresh else "等待有效飞机遥测")
        self.air_distance_hint.config(text=hint, fg=ACCENT if distance_valid else MUTED)
        text = "等待连接飞机后接收遥测"
        if fresh:
            text = (f"遥测 {frame.get('air_telem_frames', 0)} 帧 · 数据年龄 {int(age+elapsed)} ms · "
                    f"最近间隔 {frame.get('air_telem_gap_ms', 0)} ms · 目标周期 300 ms")
            if not attitude_valid:
                text += " · 飞机姿态暂不可用"
        elif active:
            text = "旧读数已清空，正在等待有效回传"
        if radio_unavailable:
            text = "手控正在每 5 秒重新检测无线模块；请检查模块供电与接线"
        self.air_telem_detail.config(text=text)

    def _build_radio(self, panel):
        top = tk.Frame(panel, bg=PANEL)
        top.pack(fill="x", pady=(10, 12))
        self._heading(top, "NRF24L01 / 接收监测", "原遥控器摇杆数据")
        self.rc_status = self._label(top, "等待无线状态", 18, MUTED, "bold")
        self.rc_status.pack(anchor="w", padx=25, pady=(15, 8))
        self._label(top, "原遥控器 → 手控制器 → USB 串口 · 频道 1 · 仅接收", 11, MUTED).pack(
            anchor="w", padx=25)
        self.rc_stats = self._label(top, "有效包 -- · 错误包 -- · 接收速率 -- · 数据年龄 --", 11, MUTED)
        self.rc_stats.pack(anchor="w", padx=25, pady=(8, 18))
        grid = tk.Frame(panel, bg=BG)
        grid.pack(fill="both", expand=True)
        self.rc_values, self.rc_bars = [], []
        names = ("横滚 ROLL", "俯仰 PITCH", "油门 THROTTLE", "偏航 YAW",
                 "辅助 AUX1", "辅助 AUX2", "辅助 AUX3", "辅助 AUX4")
        for i, name in enumerate(names):
            grid.columnconfigure(i % 2, weight=1, uniform="radio")
            grid.rowconfigure(i // 2, weight=1)
            card = self._panel(grid, i // 2, i % 2, padx=(0, 6) if i % 2 == 0 else (6, 0), pady=5)
            row = tk.Frame(card, bg=PANEL)
            row.pack(fill="x", padx=20, pady=(12, 5))
            self._label(row, name, 12, MUTED, "bold").pack(side="left")
            value = self._label(row, "--", 25, TEXT, "bold")
            value.pack(side="right")
            self.rc_values.append(value)
            bar = tk.Canvas(card, height=12, bg=PANEL_ALT, bd=0, highlightthickness=0)
            bar.pack(fill="x", padx=20, pady=(0, 12))
            fill = bar.create_rectangle(0, 0, 0, 12, fill=ACCENT, outline="")
            self.rc_bars.append((bar, fill))
        self._label(panel, "显示收到的原始通道值 · 通常 1000–2000 · 超过 0.5 秒无有效包即清空", 10, MUTED).pack(
            anchor="w", pady=10)

    def _render_radio(self, frame):
        channels = frame.get("rc", [])
        valid = bool(frame.get("rc_valid")) and isinstance(channels, list) and len(channels) == 8
        ready = bool(frame.get("rc_ready"))
        self.rc_status.config(text="● 无线已连通 · 正在接收" if valid else
                              "等待原遥控器数据 / 无线超时" if ready else
                              "NRF24 模块未就绪" if "rc_ready" in frame else "等待无线状态",
                              fg=ACCENT if valid else MUTED)
        age = int(frame.get("rc_age_ms", -1))
        age_text = f"{age} ms" if age >= 0 else "--"
        self.rc_stats.config(text=f"有效包 {int(frame.get('rc_count', 0)):,} · "
                             f"错误包 {int(frame.get('rc_bad', 0)):,} · "
                             f"{float(frame.get('rc_hz', 0)):.1f} 包/秒 · 数据年龄 {age_text}")
        for i, (value, (bar, fill)) in enumerate(zip(self.rc_values, self.rc_bars)):
            value.config(text=str(channels[i]) if valid else "--")
            fraction = min(1, max(0, (channels[i] - 1000) / 1000)) if valid else 0
            bar.coords(fill, 0, 0, bar.winfo_width() * fraction, 12)

    def _build_output(self, panel):
        top = tk.Frame(panel, bg=PANEL)
        top.pack(fill="x", pady=(4, 4))
        self._heading(top, "OUTPUT / 八通道数据", "手势 → 遥控输出").pack_configure(pady=(4, 0))
        self.output_status = self._label(top, "等待设备输出", 16, MUTED, "bold")
        self.output_status.pack(anchor="w", padx=25, pady=(4, 2))
        self.tx_status = self._label(top, "未发送 · 上电默认关闭", 11, ACCENT)
        self.tx_status.pack(anchor="w", padx=25)
        tx_actions = tk.Frame(top, bg=PANEL)
        tx_actions.pack(fill="x", padx=25, pady=(4, 2))
        self.tx_zero = self._button(tx_actions, "平放校准", self.calibrate_output)
        self.tx_start3 = self._button(tx_actions, "连接飞机", lambda: self.tx_action("START3"), True)
        self.tx_arm3 = self._button(tx_actions, "解锁（电机不转）", lambda: self.tx_action("ARM"))
        self.tx_takeoff = self._button(tx_actions, "缓升起飞", lambda: self.tx_action("TAKEOFF"))
        self.tx_land = self._button(tx_actions, "降落", lambda: self.tx_action("LAND"))
        self.tx_resume = self._button(tx_actions, "恢复定高", lambda: self.tx_action("RESUME"))
        self.tx_stop = self._button(tx_actions, "锁定并停止", lambda: self.tx_action("STOP"))
        for button in (self.tx_zero, self.tx_start3, self.tx_arm3, self.tx_takeoff,
                       self.tx_land, self.tx_resume, self.tx_stop):
            button.pack(side="left", padx=(0, 7))
            button.config(state="disabled", pady=5, padx=10)
        self.link_alert = self._label(top, "通信告警：等待连接", 11, MUTED)
        self.link_alert.pack(anchor="w", padx=25, pady=(4, 0))
        self.height_status = self._label(top, "定高模式：连接后单独解锁、起飞；等待飞机状态", 11, MUTED)
        self.height_status.pack(anchor="w", padx=25, pady=(2, 0))
        self.height_details = self._label(top, "高度 -- · 相对地面 -- · 基准推力 -- · 无线故障 --", 10, MUTED)
        self.height_details.pack(anchor="w", padx=25, pady=(0, 2))
        self.air_status = self._label(top, "飞机状态：等待回传", 11, MUTED)
        self.air_status.pack(anchor="w", padx=25, pady=3)
        self.air_diagnostics = self._label(top, "通信：等待回包", 10, MUTED)
        self.air_diagnostics.pack(anchor="w", padx=25, pady=(0, 3))
        self._label(top, "首次电机测试拆桨并关闭原遥控器 · 回传中断会告警；飞控收不到控制仍执行失控保护", 10, MUTED).pack(
            anchor="w", padx=25)
        controls = tk.Frame(top, bg=PANEL)
        controls.pack(fill="x", padx=25, pady=(4, 3))
        self.output_pickers = []
        for label, variable, values, action, width in (
            ("左右倾斜", self.output_axis, ("偏航 Yaw", "横滚 Roll"), "AXIS", 12),):
            self._label(controls, label, 10, MUTED).pack(side="left", padx=(0, 6))
            picker = ttk.Combobox(controls, textvariable=variable, values=values, width=width,
                                  state="disabled", font=("Noto Sans CJK SC", -12))
            picker.pack(side="left", padx=(0, 14))
            picker.bind("<<ComboboxSelected>>", lambda event, key=action: self.set_output(key))
            self.output_pickers.append(picker)
        self.output_config = self._label(top, "配置保存在设备内存，重启恢复默认", 10, MUTED)
        self.output_config.pack(anchor="w", padx=25, pady=(0, 4))
        self.output_hint = self._label(top, "掌心按住保持 · 平放 1 秒后前进 · 左右默认偏航", 10, MUTED)
        self.output_hint.pack(anchor="w", padx=25, pady=(0, 4))
        grid = tk.Frame(panel, bg=BG)
        grid.pack(fill="both", expand=True)
        self.output_values, self.output_bars = [], []
        self.output_channel_names = []
        for i, name in enumerate(("横滚 ROLL", "俯仰 PITCH", "油门 THROTTLE", "偏航 YAW",
                                  "辅助 AUX1", "辅助 AUX2", "辅助 AUX3", "辅助 AUX4")):
            grid.columnconfigure(i % 4, weight=1, uniform="output")
            grid.rowconfigure(i // 4, weight=1)
            card = self._panel(grid, i // 4, i % 4, padx=4, pady=4)
            row = tk.Frame(card, bg=PANEL)
            row.pack(fill="x", padx=12, pady=(4, 3))
            channel_name = self._label(row, name, 11, MUTED, "bold")
            channel_name.pack(anchor="w")
            self.output_channel_names.append(channel_name)
            value = self._label(row, "--", 23, TEXT, "bold")
            value.pack(anchor="w", pady=1)
            self.output_values.append(value)
            bar = tk.Canvas(card, height=8, bg=PANEL_ALT, bd=0, highlightthickness=0)
            bar.pack(fill="x", padx=20, pady=(0, 8))
            fill = bar.create_rectangle(0, 0, 0, 8, fill=ACCENT, outline="")
            self.output_bars.append((bar, fill))
        self.output_packet = self._label(panel, "32 字节二进制帧（十六进制显示）：--", 11, TEXT, justify="left")
        self.output_packet.pack(anchor="w", pady=(8, 4))
        self.output_notice = self._label(panel, "旧协议 AUX1 变化切换辅助；定高模式以飞机状态确认接管。通道 1500 不是固定悬停推力。", 10, MUTED)
        self.output_notice.pack(anchor="w", pady=(0, 8))

    def calibrate_output(self):
        frame = self.last_frame or {}
        if not frame.get("gvalid") or frame.get("tx_phase", 0) not in (0, 1, 10):
            return
        try:
            self.link.send_gesture("ZERO")
            self.tx_status.config(text="校准已发送，请保持平放约 1 秒，等待设备确认", fg=MUTED)
        except (OSError, serial.SerialException, ConnectionError) as error:
            self.tx_status.config(text=str(error), fg=PINK)

    def tx_action(self, action):
        if action == "STOP":
            return self.emergency_stop()
        if self.estop is not None:
            if action in ("ARM", "TAKEOFF", "LAND", "RESUME") or (action in ("START3",) and not self.estop["stopped"]):
                return
        frame = self.last_frame or {}
        if action == "START3" and not ("tx_v3" in frame and frame.get("tx_phase") in (0, 10) and
                                        frame.get("rc_ready") and self._frame_fresh()):
            return
        if action in ("TAKEOFF", "LAND", "RESUME") or (action == "ARM" and frame.get("tx_v3")):
            if not self._hc3_action_allowed(frame, action):
                return
        try:
            self.link.send_tx(action)
            if action in ("START3",):
                self.estop = None
                self.estop_status.config(text="急停：锁定飞机并停止控制 · Esc（本窗口内）", fg=MUTED)
            self.tx_status.config(text="操作已发送，等待设备回传", fg=MUTED)
        except (ValueError, OSError, serial.SerialException, ConnectionError) as error:
            self.tx_status.config(text=str(error), fg=PINK)

    def emergency_stop(self, event=None):
        # Always attempt STOP, even if telemetry/IMU is stale. Never start TX
        # to manufacture an acknowledgement when the radio is already idle.
        if self.estop is None:
            frame = self.last_frame or {}
            self.estop = dict(ms=frame.get("ms"), frames=frame.get("air_frames", 0),
                              sent=False, confirmed=False, stopped=False)
        for button in (self.tx_start3, self.tx_arm3, self.tx_takeoff, self.tx_land, self.tx_resume):
            button.config(state="disabled")
        try:
            self.link.send_tx("STOP")
            self.estop["sent"] = True
            self.estop_status.config(text="急停请求已发送 · 等待手控和飞机确认", fg=PINK)
            TRACE.info("EMERGENCY_STOP requested")
        except (OSError, serial.SerialException, ConnectionError) as error:
            self.estop["sent"] = False
            self.estop_status.config(text=f"急停未送达：{error} · 无法确认飞机锁定", fg=PINK)
        return "break"

    def _render_estop(self, frame):
        stop = self.estop
        if stop is None:
            return
        if not frame:
            self.estop_status.config(text=("此前已确认锁定 · " if stop["confirmed"] else "") +
                                     "串口离线，当前急停结果无法确认", fg=PINK)
            return
        if stop["ms"] is None:
            stop["ms"] = frame.get("ms")
            stop["frames"] = frame.get("air_frames", 0)
        baseline = stop["ms"]
        fresh_frame = baseline is not None and 0 < ((int(frame.get("ms", 0)) - baseline) & 0xffffffff) < 0x80000000
        if fresh_frame and frame.get("tx_phase") in (0, 10):
            stop["stopped"] = True
        age = int(frame.get("air_age_ms", -1))
        reply_after_click = (baseline is not None and age >= 0 and
                            0 < ((int(frame.get("ms", 0)) - age - baseline) & 0xffffffff) < 0x80000000)
        if (stop["sent"] and fresh_frame and frame.get("tx_v2") and frame.get("air_valid") and
                0 <= age < 600 and reply_after_click and frame.get("air_frames", 0) > stop["frames"] and
                frame.get("tx_phase") in (0, 9) and frame.get("tx_reason") == 1 and
                frame.get("air_cmd") == 2 and not (int(frame.get("air_flags", 0)) & 1)):
            stop["confirmed"] = True
        if not stop["sent"]:
            return  # Retain the serial-write error instead of suggesting success.
        if fresh_frame and frame.get("tx_v2") and frame.get("air_valid") and 0 <= age < 600 and int(frame.get("air_flags", 0)) & 1:
            stop["confirmed"] = False
            self.estop_status.config(text="急停尚未确认：飞机仍回报已解锁", fg=PINK)
            return
        if stop["confirmed"]:
            suffix = "手端已停发" if stop["stopped"] else "正在重复发送锁定"
            self.estop_status.config(text=f"急停已确认：飞机已回报锁定 · {suffix}", fg=ACCENT)
        elif stop["stopped"]:
            detail = "未收到可验证的锁定回包" if not frame.get("tx_v2") else "未收到飞机锁定确认"
            self.estop_status.config(text=f"手端已停发 · {detail}", fg=PINK)
        else:
            self.estop_status.config(text="急停请求已发送 · 等待飞机锁定确认", fg=PINK)

    def set_output(self, action):
        if action != "AXIS":
            return
        try:
            value = int(self.output_axis.get() == "偏航 Yaw")
            self.link.send_output(action, value)
            self.output_config.config(text="设置已发送，等待设备回传", fg=MUTED)
        except (ValueError, OSError, serial.SerialException, ConnectionError) as error:
            self.output_config.config(text=str(error), fg=PINK)

    def _render_output(self, frame):
        self._render_estop(frame)
        supported = "out" in frame
        channels = frame.get("out", [])
        valid = bool(frame.get("out_valid")) and isinstance(channels, list) and len(channels) == 8
        if supported and "out_axis" in frame:
            self.output_axis.set("偏航 Yaw" if frame.get("out_axis") else "横滚 Roll")
        phase = int(frame.get("tx_phase", 0))
        configuring = phase in (0, 10)
        for i, picker in enumerate(self.output_pickers):
            picker.config(state="readonly" if supported and configuring and not (i == 1 and frame.get("tx_v3")) else "disabled")
        capable = "tx_phase" in frame
        restart_allowed = self.estop is None or self.estop["stopped"]
        self.tx_start3.config(state="normal" if "tx_v3" in frame and configuring and restart_allowed and
                              frame.get("rc_ready") and not frame.get("key_stop") and not frame.get("key_pending") and self._frame_fresh() else "disabled")
        self.tx_zero.config(state="normal" if capable and frame.get("gvalid") and not frame.get("key_pending") and phase in (0, 1, 10) else "disabled")
        self.tx_arm3.config(state="normal" if self._hc3_action_allowed(frame, "ARM") else "disabled")
        self.tx_takeoff.config(state="normal" if self._hc3_action_allowed(frame, "TAKEOFF") else "disabled")
        self.tx_land.config(state="normal" if self._hc3_action_allowed(frame, "LAND") else "disabled")
        self.tx_resume.config(state="normal" if self._hc3_action_allowed(frame, "RESUME") else "disabled")
        warning=frame.get("link_warning",0)
        active=phase not in (0,9,10)
        auto_wait = bool(frame.get("key_auto")) and phase == 1
        message="通信正常" if self._air_fresh(frame) else "等待飞机回传"
        if active and warning:
            message=("回传中断" if warning==2 else "回传延迟")+" · 控制包继续发送，飞机状态待确认"
        if auto_wait and not self._air_fresh(frame):
            message="自动连接中 · 只发送锁定帧，等待飞机回传"
        radio_unavailable = self._radio_not_ready(frame)
        if radio_unavailable:
            message = "手控 NRF24 未就绪，连接按钮暂不可用 · 检查模块供电与接线，每 5 秒自动重试"
        self.link_alert.config(text=message, fg=PINK if radio_unavailable or (active and warning and not auto_wait) else MUTED)
        air_reasons = {0: "指令已接受", 1: "请等待低油门锁定满 0.5 秒", 2: "飞机倾斜超过 15°，请放平",
                       3: "解锁通道必须居中、油门 1000", 4: "无线超时已锁定", 5: "飞机已锁定",
                       6: "飞机 IMU 连续失效超过 100 ms", 7: "低油门落地锁定",
                       8: "机身倾斜保护", 9: "飞控无线硬件异常", 10: "高度失效", 11: "起飞超时", 12: "条件不满足"}
        if frame.get("tx_v2") and frame.get("air_valid"):
            armed = bool(int(frame.get("air_flags", 0)) & 1)
            self.air_status.config(text="飞机回传：" + ("已解锁" if armed else "已锁定") + " · " +
                                   air_reasons.get(frame.get("air_reason"), "未知结果"), fg=ACCENT if armed else MUTED)
        else:
            self.air_status.config(text="飞机状态：无新鲜回传，无法确认解锁", fg=MUTED)
        self._render_air_diagnostics(frame)
        self._render_height(frame)
        self.tx_stop.config(state="normal" if capable and not configuring else "disabled")
        stages = {0: "未发送 · 输出预览", 1: "锁定连接中 · 平放校准后可解锁",
                  9: "正在发送锁定帧，随后停止", 10: "无线故障 · 已停止，需手动重连",
                  11: "解锁请求中 · 等待飞机确认", 12: "已解锁 · 电机为 0 · 等待人工起飞",
                  13: "缓升起飞中", 14: "定高运行 · 平放前进 / 掌心按住保持", 15: "降落中",
                  16: "起飞/降落中止 · 推力缓降", 17: "高度异常 · 自稳手动油门 · 先平放接管",
                  18: "恢复定高请求中 · 等待飞机确认"}
        if phase == 16:
            stages[16] += " · " + air_reasons.get(frame.get("air_reason"), "原因待确认")
        reasons = {1: "手动停止", 2: "上位机心跳超时", 3: "姿态失效", 4: "无线故障"}
        if phase == 1 and not frame.get("gcal"):
            stages[1] = "低油门锁定中 · 请平放手控制器，点击本页「平放校准」"
        elif phase == 1 and frame.get("gesture") != 1:
            stages[1] = "校准已完成 · 请保持平放约 1 秒，再点击一键解锁"
        elif phase == 0 and not frame.get("gcal"):
            stages[0] = "未发送 · 先平放校准，再开始发送"
        if frame.get("key_auto"):
            if phase == 1:
                stages[1] = ("已连接 · 保持锁定 · 平放后按 LED1 起飞" if self._air_fresh(frame) and
                             not (int(frame.get("air_flags",0))&1) else
                             "自动连接中 · 不会自动解锁或起飞")
            elif phase in (0,10):
                stages[phase] = "等待无线与姿态就绪，随后自动连接（保持锁定）"
        if radio_unavailable and configuring:
            stages[phase] = "无法连接 · 手控无线模块自检未通过"
        calibration = imu_calibration_text(frame)
        if calibration and phase == 0:
            stages[phase] = calibration + " · 请平放静止，等待 100%"
        reasons.update({5: "飞机回传超时", 6: "解锁等待超时", 7: "飞机已自行锁定"})
        reason = reasons.get(frame.get("tx_reason"), "")
        if frame.get("tx_reason") == 7 and frame.get("air_reason") in (4, 6, 7, 8, 9, 10, 11, 12):
            reason += "：" + air_reasons[frame["air_reason"]]
        self.tx_status.config(text=stages.get(phase, "未知阶段") + (f" · {reason}" if reason else "") +
                              f" · 本地发出 {int(frame.get('tx_count', 0))} / 失败 {int(frame.get('tx_errors', 0))}",
                              fg=PINK if radio_unavailable or phase in (9, 10, 16, 17) else ACCENT)
        signal = int(frame.get("gesture", 0))
        action = dict(GESTURES).get(signal, "平放确认中" if frame.get("gconfirm") else "过渡中")
        self.output_status.config(text=f"当前手势：{action} · " + ("无线输出" if frame.get("out_tx") else "输出预览") if valid else
                                  "姿态无效 · 暂停预览，油门已复位" if supported else "等待设备输出",
                                  fg=ACCENT if valid else MUTED)
        self.output_config.config(text=f"左右 → {self.output_axis.get()} · 定高：1800 上升 / 1500 保持 / 1100 下降", fg=MUTED)
        self.output_hint.config(text="掌心按住：四轴 1500 · 平放满 1 秒：俯仰 1700 前进 · 左右 1300 / 1700 · AUX1 开启")
        if phase in (17,18):
            self.output_config.config(text="自稳手动油门：先平放接管，再抬手增加 / 下垂减少；平放保持推力", fg=PINK)
            self.output_hint.config(text="已失去定高能力 · 公共推力 25 单位/秒 · 高度连续恢复 500 ms 后可人工恢复定高")
        self.output_channel_names[2].config(text="升降指令 VERTICAL")
        for i, (value, (bar, fill)) in enumerate(zip(self.output_values, self.output_bars)):
            value.config(text=str(channels[i]) if valid else "--")
            fraction = min(1, max(0, (channels[i] - 1000) / 1000)) if valid else 0
            bar.coords(fill, 0, 0, bar.winfo_width() * fraction, 8)
        data = frame.get("out_hex", "")
        if valid and len(data) == 64:
            rows = [" ".join(data[i:i+2] for i in range(start, start+32, 2)) for start in (0, 32)]
            self.output_packet.config(text="32 字节二进制帧（十六进制显示）：\n" + "\n".join(rows))
        else:
            self.output_packet.config(text="32 字节二进制帧（十六进制显示）：--")

    def _frame_fresh(self):
        return self.last_frame is not None and time.monotonic() - self.last_frame_at < 0.5

    def _radio_not_ready(self, frame):
        return self._frame_fresh() and "rc_ready" in frame and not frame["rc_ready"]

    def _air_fresh(self, frame):
        age = frame.get("air_age_ms", -1)
        elapsed_ms = max(0, time.monotonic() - self.last_frame_at) * 1000
        return bool(frame.get("air_valid") and isinstance(age, int) and 0 <= age and age + elapsed_ms < 600)

    def _hc3_action_allowed(self, frame, action):
        if (self.estop is not None or frame.get("key_stop") or frame.get("key_stop_latched") or
            frame.get("key_pending") or not frame.get("tx_v3") or not self._frame_fresh() or not self._air_fresh(frame)):
            return False
        phase = frame.get("tx_phase")
        if action == "LAND":
            return phase in (13, 14)
        flat = frame.get("gvalid") and frame.get("gcal") and frame.get("gesture") == 1
        if frame.get("link_warning",0):
            return False
        if action == "RESUME":
            return bool(phase == 17 and flat and frame.get("tx_can_resume") and frame.get("air_resume_ready"))
        if action == "ARM":
            return bool(phase == 1 and flat and frame.get("tx_can_arm"))
        if action == "TAKEOFF":
            return bool(phase == 12 and flat and frame.get("tx_can_takeoff"))
        return False

    def _render_height(self, frame):
        fresh = bool(frame.get("tx_v3") and self._frame_fresh() and self._air_fresh(frame))
        phase = frame.get("tx_phase", 0)
        stages = {0: "已锁定", 1: "已解锁待起飞 · 电机为 0", 2: "缓升起飞 · 待定高生效",
                  3: "定高运行", 4: "降落中", 5: "中止推力缓降", 6: "故障", 7: "自稳手动油门（高度异常）"}
        if not frame.get("tx_v3"):
            text = "定高模式：请先点击「定高模式：连接」，再单独解锁、起飞"
        elif not fresh:
            text = "定高模式：无新鲜飞机回传 · 无法确认高度或飞行状态"
        else:
            text = "定高模式：" + stages.get(frame.get("air_stage"), "等待状态")
            if phase == 16 or frame.get("air_stage") == 5:
                reason = {10: "高度失效", 11: "起飞超时", 12: "条件不满足"}.get(frame.get("air_reason"), "原因待确认")
                text = f"定高模式：失败推力缓降（{reason}） ≠ 定高降落"
            elif phase in (17,18):
                text += " · " + ("请平放，点击恢复定高" if frame.get("air_resume_ready") else "等待高度连续有效 500 ms")
            elif phase in (0, 1, 11, 12):
                blocks = []
                if phase != 12:
                    blocks.append("尚未解锁")
                if not frame.get("gvalid") or not frame.get("gcal"):
                    blocks.append("需平放校准")
                elif frame.get("gesture") != 1:
                    blocks.append("需保持平放")
                if blocks:
                    text += " · " + "；".join(blocks)
                elif self._hc3_action_allowed(frame, "TAKEOFF"):
                    text += " · 可点击缓升起飞 · 无地面等待，离地测距有效后接入定高"
                    if not frame.get("air_height_valid"):
                        text += "（测距暂不可用）"
                else:
                    text += " · 起飞条件尚未满足"
            elif phase == 13:
                text += " · 待定高生效"
        self.height_status.config(text=text, fg=PINK if phase in (16,17) else ACCENT if fresh else MUTED)
        age = frame.get("air_detail_age_ms", -1)
        elapsed_ms = max(0, time.monotonic() - self.last_frame_at) * 1000
        detail_fresh = fresh and isinstance(age, int) and 0 <= age and age + elapsed_ms < 1000
        def detail_number(key, signed=False):
            value = frame.get(key)
            value_fresh = bool(fresh and frame.get("air_height_valid")) if signed else detail_fresh
            if (not value_fresh or not isinstance(value, int) or value == -32768 or
                    (not signed and value < 0) or (key == "air_height_mm" and value == 65535)):
                return "--"
            return str(value)
        prefix = "高度详情" if detail_fresh else "高度详情（过期 / 未收到）"
        relative = detail_number('air_relative_mm', True)
        self.height_details.config(text=f"相对地面 {relative} mm" + ("（实时）" if relative != "--" else "") +
                                   f" · {prefix}：原始 {detail_number('air_height_mm')} mm · "
                                   f"实际推力 {detail_number('air_collective')} · 无线故障 {detail_number('air_radio_faults')}" +
                                   (f" · {age} ms 前" if detail_fresh else ""), fg=TEXT if detail_fresh else MUTED)

    def _render_air_diagnostics(self, frame):
        # These counters come from aircraft replies, never from local TX completion.
        def number(key):
            value = frame.get(key)
            return str(value) if isinstance(value, int) and value >= 0 else "--"

        def duration(key):
            value = frame.get(key)
            if not isinstance(value, int) or value < 0:
                return "--"
            if value < 10000:
                return f"{value}ms"
            for unit, scale, limit in (("s", 1000, 60000), ("m", 60000, 3600000),
                                       ("h", 3600000, 86400000), ("d", 86400000, float("inf"))):
                if value < limit:
                    return f"{value / scale:.1f}{unit}"

        age = frame.get("air_age_ms", -1)
        received = (isinstance(age, int) and age >= 0 and frame.get("air_frames", 0) > 0)
        fresh = bool(frame.get("tx_v2") and frame.get("air_valid") and received and age < 600)
        prefix = "通信" if fresh else "通信（历史）" if received else "通信（无回包）"
        flags = int(frame.get("air_flags", 0))
        throttle = number("air_thr") if fresh else "--"
        height = ("开" if flags & (1 << 1) else "关") if fresh else "--"
        flow = ("开" if flags & (1 << 4) else "关") if fresh else "--"
        rx = number("air_rx_count") if received else "--"
        bad = number("air_bad") if received else "--"
        seq = number("air_seq") if received else "--"
        first_event = {0:"无",4:"控制包超时",6:"IMU失效",7:"接地",8:"倾角保护",
                       9:"无线硬件异常",10:"高度异常",11:"起飞超时",12:"条件不满足"}.get(frame.get("air_first_event"),"--")
        throttle_label = "实收升降" if frame.get("tx_v3") else "实收油门"
        self.air_diagnostics.config(
            text=f"{prefix}：飞机收 {rx} / 坏 {bad} · 回包 {number('air_frames')} · 序 {seq} · "
                 f"距今 {duration('air_age_ms')} · 间隔 {duration('air_gap_ms')} / 最大 {duration('air_max_gap_ms')} · "
                 f"{throttle_label} {throttle} · 定高 {height} / 光流 {flow}\n"
                 f"上行间隔 {duration('air_rx_gap_ms')} / 最大 {duration('air_rx_max_gap_ms')}（最近诊断回报） · "
                 f"切回接收最大 {number('tx_rx_max_us')}μs · 迟于20ms {number('tx_rx_late')}次 · 首个异常：{first_event}",
            fg=TEXT if fresh else MUTED)

    def _heading(self, panel, eyebrow, title):
        head = tk.Frame(panel, bg=PANEL)
        head.pack(fill="x", padx=25, pady=(16, 0))
        self._label(head, eyebrow, 9, ACCENT, "bold").pack(anchor="w")
        self._label(head, title, 17, TEXT, "bold").pack(anchor="w", pady=(3, 0))
        return head

    def _build_attitude(self, panel):
        head = self._heading(panel, "MOTION / 姿态", "手势与倾斜角度")
        self.zero_button = self._button(head, "手平放后校准", self.toggle_zero)
        self.zero_button.place(relx=1, y=3, anchor="ne")
        self.zero_button.config(state="disabled")
        gestures = tk.Frame(panel, bg=PANEL)
        gestures.pack(side="bottom", fill="x", padx=22, pady=(0, 16))
        self.gesture_title = self._label(gestures, "等待手势数据", 21, TEXT, "bold")
        self.gesture_title.pack(anchor="w", pady=(0, 8))
        badges = tk.Frame(gestures, bg=PANEL)
        badges.pack(fill="x")
        self.gesture_badges = {}
        for bit, name in GESTURES:
            badge = tk.Label(badges, text=name, bg=PANEL_ALT, fg=MUTED, padx=7, pady=7,
                             font=("Noto Sans CJK SC", -12, "bold"))
            badge.pack(side="left", expand=True, fill="x", padx=(0, 4))
            self.gesture_badges[bit] = badge
        directions = tk.Frame(gestures, bg=PANEL)
        directions.pack(fill="x", pady=(7, 1))
        self.direction_checks = []
        for label, variable in (("上下反向", self.invert_pitch), ("左右反向", self.invert_roll),
                                ("交换俯仰/横滚", self.swap_axes)):
            check = tk.Checkbutton(directions, text=label, variable=variable, command=self.set_direction,
                                   bg=PANEL, fg=MUTED, selectcolor=PANEL_ALT, activebackground=PANEL,
                                   activeforeground=TEXT, font=("Noto Sans CJK SC", -11), state="disabled")
            check.pack(side="left", padx=(0, 8))
            self.direction_checks.append(check)
        self.gesture_hint = self._label(gestures, "掌心按住保持 · 平放持续 1 秒才前进", 10, MUTED)
        self.gesture_hint.pack(anchor="w")
        body = tk.Frame(panel, bg=PANEL)
        body.pack(fill="both", expand=True, padx=20, pady=8)
        body.columnconfigure(0, weight=1)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)
        self.gauge = tk.Canvas(body, width=330, height=235, bg=PANEL, bd=0, highlightthickness=0)
        self.gauge.grid(row=0, column=0, sticky="nsew")
        cx, cy, radius = 162, 117, 91
        self.gauge.create_oval(cx-radius-10,cy-radius-10,cx+radius+10,cy+radius+10,
                               fill="#1c3444",outline="#456375",width=2)
        for r in (34, 63):
            self.gauge.create_oval(cx-r,cy-r,cx+r,cy+r,outline="#416073",dash=(3,5))
        self.gauge.create_line(cx-radius,cy,cx+radius,cy,fill="#547588")
        self.gauge.create_line(cx,cy-radius,cx,cy+radius,fill="#547588")
        self.gauge.create_text(cx,9,text="抬起  +",fill=MUTED,font=("Noto Sans CJK SC",-11))
        self.gauge.create_text(cx,225,text="下垂  −",fill=MUTED,font=("Noto Sans CJK SC",-11))
        self.gauge.create_text(14,cy,text="左",fill=MUTED,font=("Noto Sans CJK SC",-11))
        self.gauge.create_text(311,cy,text="右",fill=MUTED,font=("Noto Sans CJK SC",-11))
        self.gauge.create_oval(cx-4,cy-4,cx+4,cy+4,outline="#9bb4c3")
        self.point_glow = self.gauge.create_oval(cx-27,cy-27,cx+27,cy+27,
                                                 fill="#536c44",outline="")
        self.point = self.gauge.create_oval(cx-17,cy-17,cx+17,cy+17,fill=ACCENT,outline="")

        numbers = tk.Frame(body, bg=PANEL)
        numbers.grid(row=0, column=1, sticky="nsew", padx=(10, 0), pady=(28, 15))
        for row, (label, axis) in enumerate((("俯仰角  PITCH", "pitch"), ("横滚角  ROLL", "roll"))):
            block = tk.Frame(numbers, bg=PANEL)
            block.pack(fill="x", expand=True)
            self._label(block, label, 11, MUTED, "bold").pack(anchor="w")
            value = self._label(block, "--.-°", 34, TEXT, "bold")
            value.pack(anchor="w", pady=(3, 0))
            direction = self._label(block, "等待姿态数据", 10, MUTED)
            direction.pack(anchor="w")
            setattr(self, f"{axis}_value", value)
            setattr(self, f"{axis}_direction", direction)
        self.imu_status = self._label(numbers, "IMU 等待中", 10, MUTED)
        self.imu_status.pack(anchor="w", pady=(7, 0))

    def _build_oxygen(self, panel):
        self._heading(panel, "BIOMETRIC / 传感器", "血氧估计")
        self.finger = self._label(panel, "未检测到手指", 10, MUTED)
        self.finger.pack(anchor="e", padx=25)
        center = tk.Frame(panel, bg=PANEL)
        center.pack(fill="both", expand=True)
        self._label(center, "◉", 23, PINK).pack(pady=(13, 0))
        self.spo2 = self._label(center, "-- %", 53, TEXT, "bold")
        self.spo2.pack()
        self._label(center, "SpO₂  ·  MAX30102", 10, MUTED).pack()
        bottom = tk.Frame(panel, bg=PANEL)
        bottom.pack(fill="x", padx=25, pady=(0, 22))
        self.ppg_raw = self._label(bottom, "原始信号  RED --  IR --", 10, MUTED)
        self.ppg_raw.pack(anchor="w", pady=(0, 8))
        row = tk.Frame(bottom, bg=PANEL)
        row.pack(fill="x")
        self._label(row, "实时采样", 10, MUTED).pack(side="left")
        self.samples = self._label(row, "等待数据", 10, MUTED)
        self.samples.pack(side="right")
        self.progress = tk.Canvas(bottom, height=9, bg=PANEL_ALT, bd=0, highlightthickness=0)
        self.progress.pack(fill="x", pady=(8, 10))
        self.progress_bar = self.progress.create_rectangle(0, 0, 0, 9, fill=PINK, outline="")
        self.oxygen_hint = self._label(bottom, "红光/红外实时读取，血氧有有效结果即显示", 10, MUTED)
        self.oxygen_hint.pack(anchor="w")

    def _build_button(self, panel):
        self._heading(panel, "INPUT / GPIO 9 · 10 · 11", "按键与流水灯")
        state = tk.Frame(panel, bg=PANEL)
        state.pack(fill="x", padx=25, pady=(8, 6))
        self.button_indicator = tk.Label(state, text="●", bg=PANEL_ALT, fg=MUTED,
                                          width=3, height=1, font=("Noto Sans CJK SC", -16))
        self.button_indicator.pack(side="left", padx=(0, 10))
        self.button_state = self._label(state, "未连接", 14, TEXT, "bold")
        self.button_state.pack(side="left")
        fields = tk.Frame(panel, bg=PANEL)
        fields.pack(fill="x", padx=25)
        for label, attr in (("累计按下", "presses"), ("流水灯", "leds")):
            cell = tk.Frame(fields, bg=PANEL_ALT, padx=12, pady=5)
            cell.pack(side="left", expand=True, fill="x", padx=(0, 7) if attr == "presses" else (7, 0))
            self._label(cell, label, 10, MUTED).pack(side="left")
            value = self._label(cell, "--", 13, TEXT, "bold")
            value.pack(side="right")
            setattr(self, attr, value)
        self.pattern = self._label(panel, "当前灯效：--", 11, ACCENT)
        self.pattern.pack(anchor="w", padx=25, pady=(6, 0))
        self._label(panel, "GPIO47 为定高按键，不参与灯光输出", 10, MUTED).pack(anchor="w", padx=25)

        self.flight_start_key = self._label(panel, "LED1 · 一键起飞：未连接", 10, MUTED)
        self.flight_start_key.pack(anchor="w", padx=25, pady=(5, 0))
        self.flight_stop_key = self._label(panel, "LED2 · 急停：未连接", 10, MUTED)
        self.flight_stop_key.pack(anchor="w", padx=25)
        self.flight_hold_key = self._label(panel, "GPIO47 · 定高按键：未连接", 10, MUTED)
        self.flight_hold_key.pack(anchor="w", padx=25)
        self.flight_key_status = self._label(panel, "上电自动连接 · 平放后按 LED1 起飞", 10, MUTED)
        self.flight_key_status.pack(anchor="w", padx=25, pady=(3, 0))

    def _build_motor(self, panel):
        self._heading(panel, "HAPTICS / GPIO 18 · 17", "震动电机")
        rows = tk.Frame(panel, bg=PANEL)
        rows.pack(fill="x", padx=25, pady=(8, 4))
        self.motor_buttons = []
        self.motor_states = []
        for channel, header in ((0, "电机 0 · H3 / MOT0"), (1, "电机 1 · H4 / MOT1")):
            row = tk.Frame(rows, bg=PANEL_ALT, padx=12, pady=4)
            row.pack(fill="x", pady=4)
            self._label(row, header, 11, TEXT, "bold").pack(side="left")
            button = self._button(row, "震动一下", lambda n=channel: self.pulse(n), True)
            button.pack(side="right")
            status = self._label(row, "空闲", 10, MUTED)
            status.pack(side="right", padx=12)
            self.motor_buttons.append(button)
            self.motor_states.append(status)
        settings = tk.Frame(panel, bg=PANEL)
        settings.pack(fill="x", padx=25)
        self.strength_label = self._label(settings, "强度 70%", 10, MUTED)
        self.strength_label.pack(anchor="w")
        slider = tk.Scale(settings, from_=10, to=100, orient="horizontal", variable=self.strength,
                          command=self._strength_changed, bg=PANEL_ALT, fg=TEXT,
                          troughcolor="#4b6759", activebackground=ACCENT,
                          highlightthickness=0, bd=0, showvalue=False,
                          sliderlength=22, width=15, sliderrelief="raised")
        slider.pack(side="left", fill="x", expand=True)
        self.duration_picker = ttk.Combobox(settings, textvariable=self.duration, state="readonly",
                                             values=("150 ms", "300 ms", "500 ms", "1000 ms"), width=8,
                                             font=("Noto Sans CJK SC", -12))
        self.duration_picker.pack(side="right", padx=(15, 0))
        actions = tk.Frame(panel, bg=PANEL)
        actions.pack(fill="x", padx=25, pady=(1, 8))
        self.motor_message = self._label(actions, "最长 2 秒 · 自动停止", 9, MUTED)
        self.motor_message.pack(side="left")
        self.stop_button = self._button(actions, "全部停止", self.stop_motors)
        self.stop_button.pack(side="right")
        self._enable_motor_buttons(False)

    def _strength_changed(self, value):
        self.strength_label.config(text=f"强度 {int(float(value))}%")

    def refresh_ports(self):
        names = [item.device for item in list_ports.comports()]
        selected = self.selected_port.get()
        # Preserve an explicit by-id path, including while it is unplugged.
        # Falling back to auto can silently select the camera's ESP32.
        if selected != "自动" and selected not in names:
            names.append(selected)
        self.port_picker["values"] = ("自动", *names)

    def reconnect(self):
        if self.connected:
            self.tx_action("STOP")
        self._show_offline("正在停止发送并重新连接")
        self.after(800, lambda: self.link.start(self.selected_port.get()))

    def _enable_motor_buttons(self, enabled):
        for button in self.motor_buttons + [self.stop_button]:
            button.config(state="normal" if enabled else "disabled")

    def toggle_zero(self):
        if not self.connected or self.last_frame is None or not self.last_frame.get("gvalid"):
            return
        try:
            self.link.send_gesture("RESET" if self.zeroed else "ZERO")
            self.gesture_hint.config(text="校准指令已发送，等待设备状态更新", fg=MUTED)
        except (OSError, serial.SerialException, ConnectionError) as error:
            self.gesture_hint.config(text=str(error), fg=PINK)

    def set_direction(self):
        try:
            self.link.send_gesture("DIR", self.invert_pitch.get(), self.invert_roll.get(), self.swap_axes.get())
        except (OSError, serial.SerialException, ConnectionError) as error:
            self.gesture_hint.config(text=str(error), fg=PINK)

    def pulse(self, channel):
        try:
            strength = self.strength.get()
            duration = int(self.duration.get().split()[0])
            self.link.send_motor(channel, strength, duration)
            self.motor_message.config(text=f"电机 {channel}：{strength}% · {duration} ms", fg=ACCENT)
        except (ValueError, OSError, serial.SerialException, ConnectionError) as error:
            self.motor_message.config(text=str(error), fg=PINK)

    def stop_motors(self):
        try:
            self.link.send_motor(0, 0, 0)
            self.link.send_motor(1, 0, 0)
            self.motor_message.config(text="两路电机已发送停止命令", fg=ACCENT)
        except (OSError, serial.SerialException, ConnectionError) as error:
            self.motor_message.config(text=str(error), fg=PINK)

    def _render(self, frame):
        calibration = imu_calibration_text(frame)
        if calibration:
            frame = dict(frame, gvalid=0, imu=0)
        self.last_frame = frame
        self.last_frame_at = time.monotonic()
        self._render_radio(frame)
        self._render_output(frame)
        self._render_air_telemetry(frame)
        supported = "gesture" in frame
        valid_pose = bool(frame.get("gvalid"))
        signal = int(frame.get("gesture", 0))
        self.zeroed = bool(frame.get("gcal"))
        can_zero = frame.get("tx_phase",0) in (0,10) or (
            frame.get("tx_phase")==1 and not self.zeroed and not frame.get("key_pending"))
        self.zero_button.config(text="清除平放校准" if self.zeroed else "手平放后校准",
                                state="normal" if valid_pose and can_zero else "disabled")
        for check in self.direction_checks:
            check.config(state="normal" if supported and frame.get("tx_phase", 0) in (0, 10) else "disabled")
        self.invert_pitch.set(bool(frame.get("ginvp")))
        self.invert_roll.set(bool(frame.get("ginvr")))
        self.swap_axes.set(bool(frame.get("gswap")))
        name = dict(GESTURES).get(signal)
        active = not calibration and name is not None and (signal == 6 or valid_pose)
        title = name if active else ("姿态识别中…" if valid_pose else
                                    "姿态数据无效" if supported else "请更新手势固件")
        phase = frame.get("gphase")
        if signal == 0 and valid_pose:
            title = "平放确认中 · 1 秒" if frame.get("gconfirm") else "过渡中 · 无动作"
        self.gesture_title.config(text=calibration or title, fg=ACCENT if active else MUTED)
        for code, badge in self.gesture_badges.items():
            on = active and signal == code
            badge.config(bg=ACCENT if on else PANEL_ALT, fg="#253817" if on else MUTED)
        if signal == 6:
            hint = "掌心按住优先 · 停止方向和升降指令，定高保持"
        elif frame.get("gconfirm"):
            hint = "平放满 1 秒才前进 · 等待期间不输出方向或升降"
        elif signal == 1:
            hint = "平放前进 · 按住掌心立即保持，倾斜切换方向"
        elif phase == 2:
            hint = "方向快速响应 · 可直接切换，无需先回平放"
        else:
            hint = "倾斜 ≥20° 快速响应 · 平放持续 1 秒才前进"
        if not self.zeroed:
            hint = "先点平放校准 · " + hint
        if calibration:
            hint = "请平放静止，等待进度达到 100% · 完成后自动设置平放零点"
        self.gesture_hint.config(text=hint, fg=MUTED)
        if valid_pose or (not supported and frame.get("imu")):
            pitch = float(frame.get("gpitch", frame.get("pitch", 0)))
            roll = float(frame.get("groll", frame.get("roll", 0)))
            self.pitch_value.config(text=f"{pitch:+.1f}°")
            self.roll_value.config(text=f"{roll:+.1f}°")
            self.pitch_direction.config(text="当前信号：" + (name or "识别中"))
            self.roll_direction.config(text="单信号 · 掌心按住优先")
            self.imu_status.config(text="●  IMU 正常" + (" · 零偏已校准" if frame.get("imu_cal") == 3 else
                                                     " · 已校准" if self.zeroed else ""), fg=ACCENT)
            x = 162 + max(-73, min(73, roll / 45 * 73))
            y = 117 - max(-73, min(73, pitch / 45 * 73))
            self.gauge.coords(self.point_glow, x-27, y-27, x+27, y+27)
            self.gauge.coords(self.point, x-17, y-17, x+17, y+17)
        else:
            self.imu_status.config(text="●  " + (calibration or "IMU 无有效数据"), fg=MUTED)
            self.pitch_value.config(text="--.-°")
            self.roll_value.config(text="--.-°")
            self.pitch_direction.config(text="等待姿态数据")
            self.roll_direction.config(text="等待姿态数据")

        sensor_ready = bool(frame.get("max_ready"))
        reading_ok = bool(frame.get("ppg_ok"))
        finger = bool(frame.get("finger"))
        valid = finger and reading_ok and bool(frame.get("spo2_valid"))
        held = finger and reading_ok and not valid and bool(frame.get("spo2_held"))
        self.finger.config(text="已检测到手指" if finger else
                           "未检测到手指" if reading_ok else
                           "传感器读取失败" if sensor_ready else "传感器未就绪",
                           fg=ACCENT if finger else MUTED)
        self.spo2.config(text=f"{float(frame.get('spo2', 0)):.1f} %" if valid or held else "-- %")
        red = int(frame.get("red", 0))
        ir = int(frame.get("ir", 0))
        self.ppg_raw.config(text=f"原始信号  RED {red:,}   IR {ir:,}" if reading_ok else
                            "原始信号  RED --   IR --")
        samples = min(500, max(0, int(frame.get("samples", 0))))
        streaming = bool(frame.get("spo2_stream"))
        self.samples.config(text=f"已读 {int(frame.get('ppg_samples', 0)):,} 点" if streaming else f"{samples} / 500")
        width = self.progress.winfo_width()
        # In streaming mode this is a data-available indicator, not a 5s wait.
        fraction = (1 if reading_ok else 0) if streaming else samples / 500
        self.progress.coords(self.progress_bar, 0, 0, width * fraction, 9)
        age_s = max(0, int(frame.get("spo2_age_ms", 0))) / 1000
        self.oxygen_hint.config(text=f"保持上次有效值 · 等待更新（{age_s:.1f} 秒前）" if held else
                                "血氧实时更新 · 轻贴并保持平稳" if valid else
                                "测量中 · 原始数据已上报，等待首次有效血氧" if finger and reading_ok else
                                "原始数据已上报 · 请用指腹盖住光窗" if reading_ok else
                                "等待传感器数据")

        pressed = bool(frame.get("button"))
        self.button_indicator.config(bg=ACCENT if pressed else PANEL_ALT,
                                      fg="#263817" if pressed else MUTED)
        self.button_state.config(text="掌心按住 · 保持" if pressed else "掌心松开 · 手势控制")
        self.presses.config(text=f"{int(frame.get('presses', 0))} 次")
        self.leds.config(text="已开启" if frame.get("leds") else "已关闭")
        pattern = int(frame.get("pattern", 0))
        self.pattern.config(text="当前灯效：" + (PATTERNS[pattern] if pattern in range(3) else "未知"))
        keys_supported = "key_start" in frame
        self.flight_start_key.config(text=(
            f"LED1 · 一键起飞：{'按下' if frame.get('key_start') else '松开'}  ·  {int(frame.get('key_start_count', 0))} 次"
            if keys_supported else "LED1 · 一键起飞：等待新固件"),
            fg=ACCENT if frame.get("key_start") else MUTED)
        self.flight_stop_key.config(text=(
            f"LED2 · 急停：{'按下' if frame.get('key_stop') else '松开'}  ·  {int(frame.get('key_stop_count', 0))} 次"
            if keys_supported else "LED2 · 急停：等待新固件"),
            fg=PINK if frame.get("key_stop") else MUTED)
        hold_reason = {0: "手势控制", 1: "当前非可定高飞行状态", 2: "定高不可用", 3: "飞机回传不新鲜"}
        self.flight_hold_key.config(text=(
            f"GPIO47 · {'按下' if frame.get('key_hold') else '松开'} · {frame.get('key_hold_count', 0)} 次 · "
            + ("定高保持中" if frame.get('height_hold') else hold_reason.get(frame.get('hold_reason', 0), "等待状态"))
            if "key_hold" in frame else "GPIO47 · 定高按键：等待新固件"),
            fg=ACCENT if frame.get("height_hold") else MUTED)
        key_states = {0: "平放后按下并松开 LED1，连接并起飞", 1: "一键起飞：连接中 · 请保持平放",
                      2: "一键起飞：等待解锁确认", 3: "一键起飞：已请求缓升", 4: "实体按键控制中 · LED2 可急停",
                      5: "急停：正在发送锁定", 6: "急停流程已结束 · 锁定状态见飞机回传", 7: "操作未完成",
                      8: "自动连接中 · 保持锁定，等待飞机回传", 9: "已连接并锁定 · 平放后按 LED1 起飞"}
        key_reasons = {1: "NRF24 未就绪", 2: "IMU 无新数据", 3: "请松开手掌按钮", 4: "当前状态不接受重复起飞",
                       5: "手未保持平放", 6: "连接/解锁超时，请重新按键", 7: "急停已触发"}
        key_text = key_states.get(frame.get("key_status", 0), "等待按键状态")
        if frame.get("key_reason", 0) in key_reasons:
            key_text += " · " + key_reasons[frame["key_reason"]]
        self.flight_key_status.config(text=key_text if keys_supported else "等待实体按键状态",
                                      fg=PINK if frame.get("key_reason") else MUTED, wraplength=490)
        for channel in (0, 1):
            value = int(frame.get(f"motor{channel}", 0))
            self.motor_states[channel].config(text=f"{value}%" if value else "空闲",
                                               fg=ACCENT if value else MUTED)
        self.device_time.config(text=f"设备时间 {int(frame.get('ms', 0)) / 1000:.1f} s")

    def _poll(self):
        while not self.events.empty():
            event = self.events.get_nowait()
            if event[0] == "frame":
                _, frame, name = event
                self.last_frame_at = time.monotonic()
                self.connected = True
                self.status.config(text=f"{Path(name).resolve().name} · 实时连接", fg=ACCENT)
                self.status_dot.config(fg=ACCENT)
                self._enable_motor_buttons(True)
                self._render(frame)
            elif event[0] == "status":
                _, online, message = event
                if not online:
                    self._show_offline(message)
                elif not self.connected:
                    self.status.config(text=message, fg=MUTED)
                    self.status_dot.config(fg=MUTED)
        now = time.monotonic()
        if self.connected and now - self.last_frame_at < 0.5 and now - self.last_ping_at >= 0.2:
            if self.last_frame is not None and "tx_phase" in self.last_frame:
                try:
                    self.link.send_tx("PING")
                    self.last_ping_at = now
                except (OSError, serial.SerialException, ConnectionError):
                    self._show_offline("发送心跳失败")
        if self.connected and time.monotonic() - self.last_frame_at > 1.5:
            self._show_offline("状态帧超时，等待设备")
        elif self.connected and now - self.last_frame_at >= 0.5:
            self._render_output(self.last_frame or {})
        self._render_air_telemetry(self.last_frame or {})
        self.after(60, self._poll)

    def _show_offline(self, message):
        self.connected = False
        self._render_radio({})
        self._render_output({})
        self._render_air_telemetry({})
        self.output_status.config(text="USB 串口离线 · 预览已清空", fg=MUTED)
        self.rc_status.config(text="USB 串口离线", fg=MUTED)
        self.last_frame = None
        self.status.config(text=message, fg=PINK)
        self.status_dot.config(fg=PINK)
        self._enable_motor_buttons(False)
        self.imu_status.config(text="●  IMU 离线", fg=MUTED)
        self.pitch_value.config(text="--.-°")
        self.roll_value.config(text="--.-°")
        self.spo2.config(text="-- %")
        self.ppg_raw.config(text="原始信号  RED --   IR --")
        self.finger.config(text="串口离线", fg=MUTED)
        self.samples.config(text="等待数据")
        self.progress.coords(self.progress_bar, 0, 0, 0, 9)
        self.oxygen_hint.config(text="等待传感器数据")
        self.button_state.config(text="未连接")
        self.flight_start_key.config(text="LED1 · 一键起飞：未连接", fg=MUTED)
        self.flight_stop_key.config(text="LED2 · 急停：未连接", fg=MUTED)
        self.flight_hold_key.config(text="GPIO47 · 定高按键：未连接", fg=MUTED)
        self.flight_key_status.config(text="等待实体按键状态", fg=MUTED)
        self.button_indicator.config(bg=PANEL_ALT, fg=MUTED)
        self.gesture_title.config(text="等待手势数据", fg=MUTED)
        self.zero_button.config(state="disabled")
        self.pitch_direction.config(text="等待姿态数据")
        self.roll_direction.config(text="等待姿态数据")
        for badge in self.gesture_badges.values():
            badge.config(bg=PANEL_ALT, fg=MUTED)
        for check in self.direction_checks:
            check.config(state="disabled")
        for item in self.motor_states:
            item.config(text="--", fg=MUTED)

    def _close(self):
        try:
            if self.connected:
                self.link.send_tx("STOP")
                self.link.send_motor(0, 0, 0)
                self.link.send_motor(1, 0, 0)
        except (OSError, serial.SerialException, ConnectionError):
            pass
        self.after(800, self._finish_close)

    def _finish_close(self):
        self.link.stop()
        self.destroy()


def main():
    parser = argparse.ArgumentParser(description="手控制器串口桌面上位机")
    parser.add_argument("--port", help="串口路径；默认自动发现 ESP32-S3")
    args = parser.parse_args()
    App(args.port).mainloop()


if __name__ == "__main__":
    main()
