"""Exercise the real Tk widgets without opening a serial port."""
import importlib.util
import json
import logging
from pathlib import Path
import queue
import threading
import time
import unittest
from unittest.mock import MagicMock, patch

spec = importlib.util.spec_from_file_location("host_run", Path(__file__).parents[1] / "host/run.py")
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)
RealSerialLink = host.SerialLink
# Test commands must not rotate or mix into the live hardware trace.
host.TRACE.addHandler(logging.NullHandler())


class FakeLink:
    def __init__(self, events):
        self.commands = []

    def start(self, selected):
        pass

    def stop(self):
        pass

    def send_gesture(self, *args):
        self.commands.append(args)

    def send_output(self, *args):
        self.commands.append(args)

    def send_tx(self, action):
        self.commands.append(("TX", action))


class HostTests(unittest.TestCase):
    def setUp(self):
        host.SerialLink = FakeLink
        self.app = host.App(None)
        self.app.pages.select(0)
        self.app.update_idletasks()

    def tearDown(self):
        for timer in self.app.tk.call("after", "info"):
            self.app.after_cancel(timer)
        self.app.destroy()

    @staticmethod
    def estop_frame(**changes):
        frame = dict(ms=1000, out_valid=1, out=[1500, 1500, 1200, 1500, 2000, 1000, 1000, 1000],
                     gvalid=1, gcal=1, gesture=1, rc_ready=1, tx_phase=14, tx_reason=0, tx_v3=1,
                     tx_v2=1, tx_can_arm=1, air_valid=1, air_flags=1, air_cmd=0,
                     air_frames=20, air_age_ms=30)
        frame.update(changes)
        return frame

    def test_aircraft_telemetry_validity_and_aging(self):
        base = self.estop_frame(tx_phase=1, air_flags=0, air_telem_received=1,
            air_telem_age_ms=100, air_attitude_valid=1, air_pitch_cdeg=-1234,
            air_roll_cdeg=5678, air_distance_valid=0, air_distance_cm=65535,
            air_telem_frames=10, air_telem_gap_ms=300, pitch=99, roll=88)
        self.app._render(base)
        self.assertEqual(self.app.air_pitch_value['text'], '-12.34°')
        self.assertEqual(self.app.air_roll_value['text'], '+56.78°')
        self.assertEqual(self.app.air_distance_value['text'], '-- cm')
        self.assertIn('等待有效避障测距', self.app.air_distance_hint['text'])
        for cm, expected in ((20,'20 cm'), (720,'720 cm'), (19,'-- cm'), (721,'-- cm')):
            self.app._render(dict(base, air_distance_valid=1, air_distance_cm=cm))
            self.assertEqual(self.app.air_distance_value['text'], expected)
        for changes in (dict(air_telem_age_ms=1000), dict(air_telem_received=0),
                        dict(tx_phase=0), dict(tx_phase=9), dict(tx_phase=10),
                        dict(air_valid=0), dict(air_attitude_valid=0), dict(air_pitch_cdeg=-32768)):
            self.app._render(dict(base, **changes))
            self.assertEqual(self.app.air_pitch_value['text'], '--°')
        self.app._render(base)
        self.app.last_frame_at -= .6
        self.app._render_air_telemetry(base)
        self.assertEqual(self.app.air_pitch_value['text'], '--°')
        self.app._render(base)
        self.app._show_offline('offline')
        self.assertEqual(self.app.air_pitch_value['text'], '--°')
        self.assertEqual(self.app.air_distance_value['text'], '-- cm')
        self.assertFalse(self.app.link.commands)

    def test_ppg_stream_does_not_wait_for_500_samples(self):
        frame = self.estop_frame(max_ready=1, ppg_ok=1, red=12345, ir=23456,
            finger=1, spo2_stream=1, ppg_samples=2, samples=2, spo2_valid=1, spo2=97.8)
        self.app._render(frame)
        self.assertEqual(self.app.spo2['text'], '97.8 %')
        self.assertEqual(self.app.samples['text'], '已读 2 点')
        self.assertIn('12,345', self.app.ppg_raw['text'])
        self.app._render(dict(frame, spo2_valid=0))
        self.assertEqual(self.app.spo2['text'], '-- %')
        self.assertIn('原始数据已上报', self.app.oxygen_hint['text'])
        self.app._render(dict(frame, ppg_ok=0))
        self.assertEqual(self.app.spo2['text'], '-- %')
        self.assertEqual(self.app.ppg_raw['text'], '原始信号  RED --   IR --')

    def test_startup_gyro_calibration_progress_and_interlocks(self):
        base = self.estop_frame(tx_phase=0, air_flags=0, imu_cal_progress=45,
                               key_auto=1, imu=1, gvalid=1, gpitch=0.2, groll=-0.1)
        for state, label in ((0,'0%'), (1,'45%'), (2,'晃动'), (4,'读取异常')):
            self.app._render(dict(base, imu_cal=state, imu_cal_progress=0 if state==0 else 45))
            self.assertIn(label, self.app.imu_status['text'])
            self.assertIn(label, self.app.gesture_title['text'])
            self.assertIn(label, self.app.tx_status['text'])
            self.assertEqual(self.app.pitch_value['text'], '--.-°')
            self.assertEqual(self.app.roll_value['text'], '--.-°')
            self.assertEqual(str(self.app.tx_arm3['state']), 'disabled')
            self.assertEqual(str(self.app.tx_takeoff['state']), 'disabled')
            self.assertEqual(str(self.app.zero_button['state']), 'disabled')
        self.app._render(dict(base, tx_phase=1, imu_cal=3, imu_cal_progress=100))
        self.assertIn('零偏已校准', self.app.imu_status['text'])
        self.assertEqual(self.app.pitch_value['text'], '+0.2°')
        self.assertEqual(self.app.roll_value['text'], '-0.1°')
        self.assertFalse(self.app.link.commands)

    def test_ppg_holds_valid_value_until_update_or_finger_off(self):
        frame = self.estop_frame(max_ready=1, ppg_ok=1, finger=1,
            spo2_stream=1, spo2_valid=1, spo2=97.8, spo2_age_ms=10)
        self.app._render(frame)
        self.assertEqual(self.app.spo2['text'], '97.8 %')
        held = dict(frame, spo2_valid=0, spo2_held=1, spo2_age_ms=2300)
        self.app._render(held)
        self.assertEqual(self.app.spo2['text'], '97.8 %')
        self.assertIn('保持上次有效值', self.app.oxygen_hint['text'])
        self.assertIn('2.3 秒前', self.app.oxygen_hint['text'])
        self.app._render(dict(frame, spo2=96.9))
        self.assertEqual(self.app.spo2['text'], '96.9 %')
        self.assertIn('实时更新', self.app.oxygen_hint['text'])
        self.app._render(dict(held, finger=0))
        self.assertEqual(self.app.spo2['text'], '-- %')
        self.app._render(dict(frame, spo2_valid=0, spo2_held=0, spo2=0))
        self.assertEqual(self.app.spo2['text'], '-- %')
        self.assertIn('测量中', self.app.oxygen_hint['text'])
        self.app._render(dict(held, ppg_ok=0))
        self.assertEqual(self.app.spo2['text'], '-- %')
        self.app._render(held)
        self.app._show_offline('disconnected')
        self.assertEqual(self.app.spo2['text'], '-- %')
        self.assertEqual(self.app.oxygen_hint['text'], '等待传感器数据')

    def test_physical_flight_buttons_display_and_host_interlocks(self):
        frame = self.estop_frame(tx_phase=1, key_start=1, key_stop=0,
            key_start_count=2, key_stop_count=0, key_status=1, key_reason=0,
            key_pending=1, key_local=1, key_stop_latched=0)
        self.app._render(frame)
        self.assertIn('按下', self.app.flight_start_key['text'])
        self.assertIn('2 次', self.app.flight_start_key['text'])
        self.assertIn('连接中', self.app.flight_key_status['text'])
        self.assertEqual(str(self.app.tx_arm3['state']), 'disabled')
        self.assertEqual(str(self.app.tx_zero['state']), 'disabled')
        self.app._render(dict(frame, key_pending=0, key_stop=1, key_stop_latched=1,
                              key_stop_count=1, key_status=5, key_reason=7))
        self.assertIn('按下', self.app.flight_stop_key['text'])
        self.assertIn('正在发送锁定', self.app.flight_key_status['text'])
        self.assertFalse(self.app._hc3_action_allowed(self.app.last_frame, 'ARM'))
        self.app._render(dict(frame, tx_phase=0, key_stop=1, key_pending=0))
        self.assertEqual(str(self.app.tx_start3['state']), 'disabled')
        self.app._show_offline('disconnected')
        self.assertIn('未连接', self.app.flight_start_key['text'])
        self.assertIn('未连接', self.app.flight_stop_key['text'])
        self.assertFalse(self.app.link.commands)

    def test_flight_button_status_fits_smallest_window(self):
        self.app.geometry("1100x880")
        self.app._render(self.estop_frame(key_start=0, key_stop=1,
            key_status=7, key_reason=6, key_stop_count=12345))
        self.app.update_idletasks()
        for widget in (self.app.flight_start_key, self.app.flight_stop_key,
                       self.app.flight_key_status):
            self.assertTrue(widget.winfo_ismapped())
            self.assertLessEqual(widget.winfo_y() + widget.winfo_height(),
                                 widget.master.winfo_height())

    def test_refresh_preserves_explicit_serial_port(self):
        selected = "/dev/serial/by-id/hand-controller"
        self.app.selected_port.set(selected)
        camera = MagicMock(device="/dev/ttyACM1")
        for ports in ([camera], []):
            with patch.object(host.list_ports, "comports", return_value=ports):
                self.app.refresh_ports()
            self.assertEqual(self.app.selected_port.get(), selected)
            self.assertIn(selected, self.app.port_picker['values'])

    def begin_estop(self, **changes):
        self.app.estop = None
        self.app._render(self.estop_frame(**changes))
        self.app.emergency_stop()

    def test_connection_explains_radio_failure_and_recovers(self):
        frame = self.estop_frame(tx_phase=0, air_valid=0, rc_ready=0)
        self.app._render(frame)
        self.assertEqual(self.app.tx_start3['state'], 'disabled')
        self.assertIn('NRF24 未就绪', self.app.link_alert['text'])
        self.assertIn('自检未通过', self.app.tx_status['text'])
        self.assertIn('NRF24 未就绪', self.app.air_telem_status['text'])
        self.app.tx_action('START3')
        self.assertFalse(self.app.link.commands)
        self.app._render(dict(frame, rc_ready=1))
        self.assertEqual(self.app.tx_start3['state'], 'normal')
        self.assertNotIn('NRF24 未就绪', self.app.link_alert['text'])
        self.app.tx_action('START3')
        self.assertEqual(self.app.link.commands, [('TX', 'START3')])
        self.app._show_offline('USB disconnected')
        self.assertNotIn('NRF24 未就绪', self.app.air_telem_status['text'])

    def test_estop_visible_every_page_and_shortcut(self):
        for geometry in ("1280x930", "1100x880"):
            self.app.geometry(geometry)
            for page in range(len(self.app.pages.tabs())):
                self.app.pages.select(page)
                self.app.update_idletasks()
                for widget in (self.app.estop_button, self.app.estop_status):
                    self.assertTrue(widget.winfo_ismapped())
                    x = widget.winfo_rootx() - self.app.winfo_rootx()
                    y = widget.winfo_rooty() - self.app.winfo_rooty()
                    self.assertGreaterEqual(x, 0)
                    self.assertGreaterEqual(y, 0)
                    self.assertGreaterEqual(widget.winfo_width(), widget.winfo_reqwidth())
                    self.assertLessEqual(x + widget.winfo_width(), self.app.winfo_width())
                    self.assertLessEqual(y + widget.winfo_height(), self.app.winfo_height())
                self.app.link.commands.clear()
                self.app.estop_button.invoke()
                self.assertEqual(self.app.link.commands, [("TX", "STOP")])
        self.app.focus_force()
        self.app.update()
        self.app.link.commands.clear()
        self.app.event_generate("<Escape>", when="now")
        self.assertEqual(self.app.link.commands, [("TX", "STOP")])

    def test_estop_blocks_arm_until_explicit_restart(self):
        self.begin_estop(tx_phase=1, air_flags=0, air_cmd=2)
        for button in (self.app.tx_start3, self.app.tx_start3, self.app.tx_arm3, self.app.tx_arm3):
            self.assertEqual(str(button.cget("state")), "disabled")
        # A queued READY frame cannot undo the user's stop request.
        self.app._render(self.estop_frame(tx_phase=1, air_flags=0, air_cmd=2))
        for action in ("ARM", "START3", "START3"):
            self.app.tx_action(action)
        self.assertEqual(self.app.link.commands, [("TX", "STOP")])
        self.assertEqual(str(self.app.tx_arm3.cget("state")), "disabled")
        self.app._render(self.estop_frame(ms=1300, tx_phase=0, tx_reason=1, air_valid=0))
        self.assertTrue(self.app.estop["stopped"])
        self.assertIsNotNone(self.app.estop)  # OFF alone cannot auto-resume.
        self.app.tx_action("ARM")
        self.assertEqual(self.app.link.commands, [("TX", "STOP")])
        self.app.tx_action("START3")
        self.assertEqual(self.app.link.commands, [("TX", "STOP"), ("TX", "START3")])
        self.assertIsNone(self.app.estop)

    def test_estop_requires_new_disarm_reply(self):
        reply = self.estop_frame(ms=1200, tx_phase=9, tx_reason=1, air_cmd=2,
                                air_flags=0, air_frames=21, air_age_ms=30)
        invalid_cases = (dict(ms=1000), dict(air_frames=20), dict(air_cmd=1),
                         dict(air_age_ms=600), dict(air_age_ms=200), dict(air_flags=1),
                         dict(air_valid=0), dict(tx_phase=8), dict(tx_reason=7), dict(tx_v2=0))
        for changes in invalid_cases:
            with self.subTest(changes=changes):
                self.begin_estop()
                self.app._render(dict(reply, **changes))
                self.assertFalse(self.app.estop["confirmed"])
                self.assertNotIn("急停已确认", self.app.estop_status.cget("text"))
        for phase in (9, 0):
            with self.subTest(phase=phase):
                self.begin_estop()
                self.app._render(dict(reply, tx_phase=phase))
                self.assertTrue(self.app.estop["confirmed"])
                self.assertEqual(self.app.estop["stopped"], phase == 0)
                self.assertIn("飞机已回报锁定", self.app.estop_status.cget("text"))

    def test_estop_repeat_keeps_confirmation_and_baseline(self):
        self.begin_estop()
        self.app._render(self.estop_frame(ms=1200, tx_phase=9, tx_reason=1, air_cmd=2,
                                         air_flags=0, air_frames=21))
        self.assertTrue(self.app.estop["confirmed"])
        original = dict(self.app.estop)
        self.app.tx_action("STOP")  # The existing cancel button shares this path.
        self.assertEqual(self.app.estop, original)
        self.assertEqual(self.app.link.commands, [("TX", "STOP"), ("TX", "STOP")])
        self.app._render(self.estop_frame(ms=1400, tx_phase=0, tx_reason=1, air_valid=0,
                                         air_flags=0, air_cmd=2, air_frames=21, air_age_ms=700))
        self.assertTrue(self.app.estop["confirmed"])
        self.assertIn("手端已停发", self.app.estop_status.cget("text"))

    def test_estop_legacy_offline_and_write_error_never_confirm(self):
        self.begin_estop(tx_v2=0)
        self.app._render(self.estop_frame(ms=1200, tx_v2=0, tx_phase=0, tx_reason=1,
                                         air_flags=0, air_cmd=2, air_frames=21))
        self.assertFalse(self.app.estop["confirmed"])
        self.assertIn("未收到可验证的锁定回包", self.app.estop_status.cget("text"))
        self.app._show_offline("test disconnected")
        self.assertFalse(self.app.estop["confirmed"])
        self.assertIn("无法确认", self.app.estop_status.cget("text"))
        self.app.estop = None
        self.app._render(self.estop_frame())
        with patch.object(self.app.link, "send_tx", side_effect=ConnectionError("串口未连接")):
            self.app.emergency_stop()
        self.app._render(self.estop_frame(ms=1200, tx_phase=0, tx_reason=1, air_cmd=2,
                                         air_flags=0, air_frames=21))
        self.assertFalse(self.app.estop["sent"])
        self.assertFalse(self.app.estop["confirmed"])
        self.assertIn("急停未送达", self.app.estop_status.cget("text"))

    def test_estop_without_first_frame_can_recover_after_stopped_frame(self):
        self.app.last_frame = None
        self.app.emergency_stop()
        self.app._render(self.estop_frame(ms=100, tx_phase=0, tx_reason=1, air_valid=0))
        self.assertFalse(self.app.estop["confirmed"])
        self.app._render(self.estop_frame(ms=200, tx_phase=0, tx_reason=1, air_valid=0))
        self.assertTrue(self.app.estop["stopped"])
        self.app.tx_action("START3")
        self.assertIsNone(self.app.estop)
        self.assertEqual(self.app.link.commands, [("TX", "STOP"), ("TX", "START3")])

    def test_estop_new_armed_reply_invalidates_confirmation(self):
        self.begin_estop()
        self.app._render(self.estop_frame(ms=1200, tx_phase=9, tx_reason=1, air_cmd=2,
                                         air_flags=0, air_frames=21))
        self.assertTrue(self.app.estop["confirmed"])
        self.app._render(self.estop_frame(ms=1300, tx_phase=9, tx_reason=1, air_cmd=0,
                                         air_flags=1, air_frames=22))
        self.assertFalse(self.app.estop["confirmed"])
        self.assertNotIn("急停已确认", self.app.estop_status.cget("text"))

    def test_single_signal_and_offline(self):
        for code, name in host.GESTURES:
            frame = dict(imu=1, gvalid=1, gesture=code, gpitch=30, groll=-40, button=code == 6)
            self.app._render(frame)
            self.assertEqual(self.app.gesture_title.cget("text"), name)
            lit = [code for code, badge in self.app.gesture_badges.items() if badge.cget("bg") == host.ACCENT]
            self.assertEqual(lit, [code])
        self.app._render(dict(imu=0, gvalid=0, gesture=0))
        self.assertEqual(self.app.pitch_value.cget("text"), "--.-°")
        self.assertTrue(all(b.cget("bg") != host.ACCENT for b in self.app.gesture_badges.values()))
        self.app._render(dict(gvalid=0, gesture=6, button=1))
        self.assertEqual(self.app.gesture_title.cget("text"), "掌心按住")
        self.app._show_offline("test disconnected")
        self.assertTrue(all(b.cget("bg") != host.ACCENT for b in self.app.gesture_badges.values()))
        self.assertEqual(str(self.app.zero_button.cget("state")), "disabled")

    def test_device_calibration_and_layout(self):
        self.app.connected = True
        self.app._render(dict(gesture=1, gvalid=1, gcal=0))
        self.app.toggle_zero()
        self.assertEqual(self.app.link.commands[-1], ("ZERO",))
        self.assertFalse(self.app.zeroed)  # Wait for the device, do not fake success.
        self.app._render(dict(gesture=1, gvalid=1, gcal=1))
        self.app.toggle_zero()
        self.assertEqual(self.app.link.commands[-1], ("RESET",))
        self.app.invert_pitch.set(True)
        self.app.set_direction()
        self.assertEqual(self.app.link.commands[-1], ("DIR", True, False, False))
        for geometry in ("1280x930", "1100x880"):
            self.app.geometry(geometry)
            self.app.update_idletasks()
            for widget in (self.app.gesture_title, self.app.gesture_hint, *self.app.gesture_badges.values(),
                           self.app.button_state, self.app.stop_button):
                x = widget.winfo_rootx() - self.app.winfo_rootx()
                y = widget.winfo_rooty() - self.app.winfo_rooty()
                self.assertGreaterEqual(x, 0)
                self.assertGreaterEqual(y, 0)
                self.assertLessEqual(x + widget.winfo_width(), self.app.winfo_width())
                self.assertLessEqual(y + widget.winfo_height(), self.app.winfo_height())
                self.assertGreaterEqual(widget.winfo_width(), widget.winfo_reqwidth())

    def test_radio_display_and_timeout(self):
        self.app.pages.select(1)
        self.app.update_idletasks()
        frame = dict(rc_ready=1, rc_valid=1, rc_count=321, rc_bad=2, rc_age_ms=8,
                     rc_hz=99.5, rc=[1511, 1492, 1001, 1510, 1000, 2000, 1000, 4000])
        self.app._render(frame)
        self.assertEqual([v.cget("text") for v in self.app.rc_values],
                         [str(x) for x in frame["rc"]])
        self.assertIn("无线已连通", self.app.rc_status.cget("text"))
        self.app._render(dict(frame, rc_valid=0, rc_age_ms=501))
        self.assertTrue(all(v.cget("text") == "--" for v in self.app.rc_values))
        self.assertIn("超时", self.app.rc_status.cget("text"))
        self.app._render(frame)
        self.app._show_offline("disconnected")
        self.assertTrue(all(v.cget("text") == "--" for v in self.app.rc_values))
        for geometry in ("1280x930", "1100x880"):
            self.app.geometry(geometry)
            self.app.update_idletasks()
            for w in (self.app.rc_status, self.app.rc_stats, *self.app.rc_values):
                self.assertGreaterEqual(w.winfo_width(), w.winfo_reqwidth())
                self.assertLessEqual(w.winfo_rooty() - self.app.winfo_rooty() + w.winfo_height(),
                                     self.app.winfo_height())

    def test_aircraft_diagnostics_freshness_and_layout(self):
        self.app.pages.select(2)
        frame = dict(out_valid=1, out=[1500, 1500, 1000, 1500, 2000, 1000, 1000, 1000],
                     out_hex="AA" * 32, tx_v2=1, tx_phase=8, tx_count=999999,
                     air_valid=1, air_flags=19, air_reason=0, air_rx_count=145, air_bad=2,
                     air_thr=1412, air_seq=654, air_frames=83, air_age_ms=24,
                     air_gap_ms=51, air_max_gap_ms=237)
        self.app._render(frame)
        text = self.app.air_diagnostics.cget("text")
        for value in ("飞机收 145 / 坏 2", "回包 83", "序 654", "距今 24ms", "间隔 51ms / 最大 237ms",
                      "实收油门 1412", "定高 开 / 光流 开"):
            self.assertIn(value, text)
        self.assertNotIn("999999", text)  # Local TX completions are not aircraft RX.
        self.assertNotIn("1000", text)  # Show the aircraft's throttle, not the output preview.
        for flags, expected in ((2, "定高 开 / 光流 关"), (16, "定高 关 / 光流 开"), (1, "定高 关 / 光流 关")):
            self.app._render(dict(frame, air_flags=flags))
            self.assertIn(expected, self.app.air_diagnostics.cget("text"))
        for changes in (dict(air_valid=0, air_age_ms=601), dict(air_valid=1, air_age_ms=600), dict(tx_v2=0)):
            self.app._render(dict(frame, **changes))
            text = self.app.air_diagnostics.cget("text")
            self.assertIn("通信（历史）", text)
            self.assertIn("飞机收 145 / 坏 2", text)
            self.assertIn("实收油门 --", text)
            self.assertIn("定高 -- / 光流 --", text)
            self.assertNotIn("1412", text)
        self.app._render(dict(frame, air_valid=0, air_frames=0, air_age_ms=-1))
        text = self.app.air_diagnostics.cget("text")
        self.assertIn("通信（无回包）", text)
        self.assertIn("飞机收 -- / 坏 --", text)
        self.assertIn("距今 --", text)
        self.assertNotIn("序 654", text)

        # Maximum counters and long idle periods still fit the minimum window.
        wide = dict(frame, air_rx_count=65535, air_bad=65535, air_seq=65535,
                    air_frames=4294967295, air_age_ms=4294967295, air_gap_ms=4294967295,
                    air_max_gap_ms=4294967295, air_valid=0)
        self.app._render(wide)
        for geometry in ("1280x930", "1100x880"):
            self.app.geometry(geometry)
            self.app.update_idletasks()
            for w in (self.app.air_diagnostics, self.app.air_status, self.app.output_packet,
                      self.app.tx_start3, self.app.tx_arm3, self.app.tx_start3, self.app.tx_arm3,
                      *self.app.output_values):
                self.assertTrue(w.winfo_ismapped(), str(w))
                self.assertGreaterEqual(w.winfo_width(), w.winfo_reqwidth(), str(w))
                self.assertLessEqual(w.winfo_rootx() - self.app.winfo_rootx() + w.winfo_width(), self.app.winfo_width())
                self.assertLessEqual(w.winfo_rooty() - self.app.winfo_rooty() + w.winfo_height(), self.app.winfo_height())
        self.app._show_offline("USB disconnected")
        self.assertIn("实收油门 --", self.app.air_diagnostics.cget("text"))
        self.assertIn("飞机收 --", self.app.air_diagnostics.cget("text"))

    def test_aircraft_stop_reason_is_retained(self):
        for reason, expected in ((6, "IMU 连续失效超过 100 ms"), (7, "低油门落地锁定"),
                                 (8, "机身倾斜保护"), (9, "飞控无线硬件异常")):
            self.app._render(dict(tx_v2=1, tx_phase=8, air_valid=1, air_flags=0, air_reason=reason))
            self.assertIn(expected, self.app.air_status.cget("text"))
            self.app._render(dict(tx_v2=1, tx_phase=0, tx_reason=7, air_valid=0, air_reason=reason))
            self.assertIn(expected, self.app.tx_status.cget("text"))

    def test_serial_trace_preserves_aircraft_diagnostics(self):
        frame = dict(v=1, air_rx_count=145, air_bad=2, air_thr=1412, air_seq=654,
                     air_frames=83, air_age_ms=24, air_gap_ms=51, air_max_gap_ms=237)
        stopped = threading.Event()
        port = MagicMock()
        port.__enter__.return_value = port

        def read_one_frame():
            stopped.set()
            return ("@HC:" + json.dumps(frame) + "\n").encode()

        port.readline.side_effect = read_one_frame
        events = queue.SimpleQueue()
        link = RealSerialLink(events)
        with patch.object(host, "find_port", return_value="fake-port"), \
             patch.object(host.serial, "Serial", return_value=port), \
             self.assertLogs(host.TRACE, level="INFO") as captured:
            link._run("fake-port", stopped)
        logged = json.loads(next(line.split("FRAME ", 1)[1] for line in captured.output if "FRAME " in line))
        self.assertEqual({key: logged[key] for key in frame if key != "v"},
                         {key: value for key, value in frame.items() if key != "v"})
        self.assertEqual(events.get()[0], "status")
        self.assertEqual(events.get(), ("frame", frame, "fake-port"))
        port.write.assert_not_called()

    def test_serial_trace_preserves_local_radio_fault_and_readiness(self):
        fault = "E (1234) nrf24_demo: NRF24 local register fault: CONFIG=0x2E expected=0x2F"
        driver = "E (1235) NRF24: SPI transaction failed: ESP_FAIL"
        frame = dict(v=1, rc_ready=0, tx_phase=10, tx_reason=4)
        lines = iter([(fault + "\n").encode(), (driver + "\n").encode(),
                      ("@HC:" + json.dumps(frame) + "\n").encode()])
        stopped = threading.Event()
        port = MagicMock()
        port.__enter__.return_value = port

        def read_line():
            line = next(lines)
            if line.startswith(b"@HC:"):
                stopped.set()
            return line

        port.readline.side_effect = read_line
        events = queue.SimpleQueue()
        link = RealSerialLink(events)
        with patch.object(host, "find_port", return_value="fake-port"), \
             patch.object(host.serial, "Serial", return_value=port), \
             self.assertLogs(host.TRACE, level="INFO") as captured:
            link._run("fake-port", stopped)
        self.assertTrue(any("RADIO " + fault in line for line in captured.output))
        self.assertTrue(any("RADIO " + driver in line for line in captured.output))
        logged = json.loads(next(line.split("FRAME ", 1)[1] for line in captured.output if "FRAME " in line))
        self.assertEqual(logged["rc_ready"], 0)
        self.assertEqual(events.get()[0], "status")
        self.assertEqual(events.get(), ("frame", frame, "fake-port"))
        port.write.assert_not_called()

    def test_neutral_confirmation(self):
        for signal, phase, confirm, title in ((0, 0, 0, "过渡中 · 无动作"),
                                             (0, 1, 1, "平放确认中 · 1 秒"),
                                             (1, 3, 0, "平放前进")):
            self.app._render(dict(gesture=signal, gvalid=1, gphase=phase, gconfirm=confirm,
                                  gpitch=0, groll=0))
            self.assertEqual(self.app.gesture_title.cget("text"), title)
            self.assertEqual([code for code, badge in self.app.gesture_badges.items()
                              if badge.cget("bg") == host.ACCENT], [1] if signal == 1 else [])
        self.app._render(dict(gesture=6, gvalid=1, gphase=2, button=1))
        self.assertEqual(self.app.gesture_title.cget("text"), "掌心按住")

    @staticmethod
    def hc3_frame(**changes):
        frame = HostTests.estop_frame(tx_v3=1, tx_phase=12, air_stage=1,
                                    tx_can_takeoff=1, air_height_valid=1, air_ground_ready=1,
                                    air_height_mm=45, air_relative_mm=0, air_collective=0,
                                    air_radio_faults=2, air_detail_age_ms=250,
                                    out=[1500, 1500, 1500, 1500, 2000, 1000, 1000, 1000])
        frame.update(changes)
        return frame

    def test_hc3_explicit_connect_arm_takeoff_and_land(self):
        self.app._render(self.hc3_frame(tx_phase=0, tx_v3=0))
        self.assertEqual(str(self.app.tx_start3.cget("state")), "normal")
        self.app.tx_start3.invoke()
        self.assertEqual(self.app.link.commands, [("TX", "START3")])
        self.app._render(self.hc3_frame(tx_phase=1, air_flags=0, air_stage=0))
        self.assertEqual(str(self.app.tx_arm3.cget("state")), "normal")
        for button in (self.app.tx_takeoff, self.app.tx_land, self.app.tx_resume):
            self.assertEqual(str(button.cget("state")), "disabled")
        self.app.tx_arm3.invoke()
        self.assertEqual(self.app.link.commands[-1], ("TX", "ARM"))
        self.app._render(self.hc3_frame())
        self.assertEqual(str(self.app.tx_takeoff.cget("state")), "normal")
        self.assertEqual(self.app.link.commands, [("TX", "START3"), ("TX", "ARM")])
        self.app.tx_takeoff.invoke()
        self.assertEqual(self.app.link.commands[-1], ("TX", "TAKEOFF"))
        for phase in (13, 14):
            self.app._render(self.hc3_frame(tx_phase=phase, air_stage=2 if phase == 13 else 3))
            self.assertEqual(str(self.app.tx_takeoff.cget("state")), "disabled")
            self.assertEqual(str(self.app.tx_land.cget("state")), "normal")
        self.app.tx_land.invoke()
        self.assertEqual(self.app.link.commands[-1], ("TX", "LAND"))
        for phase in (0, 1, 12, 15, 16):
            self.app._render(self.hc3_frame(tx_phase=phase))
            self.assertEqual(str(self.app.tx_land.cget("state")), "disabled")

    def test_hc3_takeoff_gates_and_blocking_reasons(self):
        cases = ((dict(tx_phase=1), "尚未解锁"), (dict(gcal=0), "平放校准"),
                 (dict(gvalid=0), "平放校准"), (dict(gesture=4), "需保持平放"),
                 (dict(tx_can_takeoff=0), "条件尚未满足"),
                 (dict(air_valid=0), "无新鲜飞机回传"),
                 (dict(air_age_ms=600), "无新鲜飞机回传"),
                 (dict(air_age_ms=-1), "无新鲜飞机回传"))
        for changes, reason in cases:
            with self.subTest(changes=changes):
                self.app._render(self.hc3_frame(**changes))
                self.assertEqual(str(self.app.tx_takeoff.cget("state")), "disabled")
                self.assertIn(reason, self.app.height_status.cget("text"))
                self.app.tx_action("TAKEOFF")  # A queued callback cannot bypass gates.
                self.assertEqual(self.app.link.commands, [])
        self.app._render(self.hc3_frame())
        self.app.last_frame_at = time.monotonic() - 0.51
        self.app.tx_action("TAKEOFF")
        self.assertEqual(self.app.link.commands, [])
        self.app._render_output(self.app.last_frame)
        self.assertEqual(str(self.app.tx_takeoff.cget("state")), "disabled")
        self.assertIn("无新鲜飞机回传", self.app.height_status.cget("text"))
        self.app._render(self.hc3_frame(air_age_ms=500))
        self.app.last_frame_at = time.monotonic() - 0.2
        self.app.tx_action("TAKEOFF")
        self.assertEqual(self.app.link.commands, [])  # Age includes time since the USB frame.

    def test_hc3_takeoff_without_ground_dwell_or_height(self):
        for height_valid, ground_ready in ((1, 0), (0, 1), (0, 0)):
            self.app._render(self.hc3_frame(air_height_valid=height_valid,
                                           air_ground_ready=ground_ready))
            self.assertEqual(str(self.app.tx_takeoff.cget("state")), "normal")
            self.assertIn("无地面等待", self.app.height_status.cget("text"))
            self.assertNotIn("等待地面高度稳定", self.app.height_status.cget("text"))
            if not height_valid:
                self.assertIn("测距暂不可用", self.app.height_status.cget("text"))
            self.app.tx_takeoff.invoke()
            self.assertEqual(self.app.link.commands[-1], ("TX", "TAKEOFF"))

    def test_hc3_valid_ground_zero_allows_manual_takeoff(self):
        self.app._render(self.hc3_frame(air_height_valid=1, air_height_mm=0,
                                       air_relative_mm=0, air_ground_ready=1))
        self.assertEqual(str(self.app.tx_takeoff.cget("state")), "normal")
        self.assertIn("无地面等待，离地测距有效后接入定高", self.app.height_status.cget("text"))
        self.assertIn("原始 0 mm", self.app.height_details.cget("text"))
        self.assertIn("相对地面 0 mm（实时）", self.app.height_details.cget("text"))
        self.assertEqual(self.app.link.commands, [])
        self.app.tx_takeoff.invoke()
        self.assertEqual(self.app.link.commands, [("TX", "TAKEOFF")])
        self.app._render(self.hc3_frame(air_height_valid=0, air_height_mm=0))
        self.assertEqual(str(self.app.tx_takeoff.cget("state")), "normal")
        self.assertIn("测距暂不可用", self.app.height_status.cget("text"))

    def test_hc3_height_details_are_not_claimed_live_when_stale(self):
        self.app._render(self.hc3_frame(air_height_mm=723, air_relative_mm=680,
                                       air_collective=480, air_detail_age_ms=200))
        text = self.app.height_details.cget("text")
        for expected in ("原始 723 mm", "相对地面 680 mm", "实际推力 480", "无线故障 2"):
            self.assertIn(expected, text)
        for changes in (dict(air_detail_age_ms=1000), dict(air_detail_age_ms=-1),
                        dict(air_valid=0), dict(tx_v3=0)):
            self.app._render(self.hc3_frame(air_height_mm=723, air_relative_mm=680,
                                           air_collective=480, **changes))
            text = self.app.height_details.cget("text")
            self.assertIn("过期 / 未收到", text)
            for expected in ("原始 --", "实际推力 --"):
                self.assertIn(expected, text)
            if changes.get("air_valid") == 0 or changes.get("tx_v3") == 0:
                self.assertIn("相对地面 --", text)
            else:
                self.assertIn("相对地面 680 mm（实时）", text)
            self.assertNotIn("723", text)
        self.app._render(self.hc3_frame(air_height_mm=65535, air_relative_mm=-32768))
        self.assertIn("原始 --", self.app.height_details.cget("text"))
        self.assertNotIn("65535", self.app.height_details.cget("text"))
        self.app._render(self.hc3_frame(air_relative_mm=-32768))
        self.assertIn("相对地面 --", self.app.height_details.cget("text"))
        self.app._render(self.hc3_frame(air_relative_mm=-3))
        self.assertIn("相对地面 -3 mm", self.app.height_details.cget("text"))
        self.app._show_offline("offline")
        for widget in (self.app.tx_start3, self.app.tx_arm3, self.app.tx_takeoff, self.app.tx_land):
            self.assertEqual(str(widget.cget("state")), "disabled")
        self.assertIn("原始 --", self.app.height_details.cget("text"))

    def test_hc3_estop_blocks_all_new_actions_until_explicit_restart(self):
        self.app._render(self.hc3_frame())
        self.app.emergency_stop()
        for widget in (self.app.tx_start3, self.app.tx_arm3, self.app.tx_takeoff, self.app.tx_land):
            self.assertEqual(str(widget.cget("state")), "disabled")
        self.app._render(self.hc3_frame(ms=1100))  # Late pre-stop frame.
        for action in ("START3", "ARM", "TAKEOFF", "LAND"):
            self.app.tx_action(action)
        self.assertEqual(self.app.link.commands, [("TX", "STOP")])
        self.app._render(self.hc3_frame(ms=1200, tx_phase=0, tx_reason=1))
        for action in ("ARM", "TAKEOFF", "LAND"):
            self.app.tx_action(action)
        self.assertEqual(self.app.link.commands, [("TX", "STOP")])
        self.app.tx_start3.invoke()
        self.assertEqual(self.app.link.commands[-1], ("TX", "START3"))
        self.assertIsNone(self.app.estop)

    def test_default_yaw_and_explicit_axis_feedback(self):
        self.assertEqual(self.app.output_axis.get(), "偏航 Yaw")
        self.app._render(self.hc3_frame(out_axis=0))
        self.assertEqual(self.app.output_axis.get(), "横滚 Roll")
        self.app._render(self.hc3_frame(out_axis=1))
        self.assertEqual(self.app.output_axis.get(), "偏航 Yaw")
        self.assertFalse(self.app.link.commands)

    def test_auto_connection_waits_for_explicit_launch(self):
        base=self.hc3_frame(tx_phase=1, key_auto=1, key_local=1, key_pending=0,
            key_start=0, key_stop=0, key_status=8, gcal=0,
            air_valid=0, air_frames=0, air_age_ms=-1, link_warning=2)
        self.app._render(base)
        self.assertIn("自动连接中",self.app.tx_status['text'])
        self.assertIn("不会自动解锁或起飞",self.app.tx_status['text'])
        self.assertIn("只发送锁定帧",self.app.link_alert['text'])
        self.assertEqual(str(self.app.tx_arm3['state']),"disabled")
        self.assertEqual(str(self.app.tx_takeoff['state']),"disabled")
        self.assertEqual(str(self.app.zero_button['state']),"normal")
        self.app._render(dict(base, key_status=9, air_valid=1, air_age_ms=20,
            air_flags=0, air_cmd=2, air_frames=10, link_warning=0))
        self.assertIn("已连接 · 保持锁定",self.app.tx_status['text'])
        self.assertIn("按 LED1 起飞",self.app.flight_key_status['text'])
        self.app._render(dict(base,gcal=1))
        self.assertEqual(str(self.app.zero_button['state']),"disabled")
        self.assertFalse(self.app.link.commands) # Rendering a connected link sends no ARM/TAKEOFF.

    def test_hc3_mapping_failure_stages_and_minimum_layout(self):
        self.app.pages.select(2)
        self.app._render(self.hc3_frame(tx_phase=14, air_stage=3))
        for expected in ("1800 上升", "1500 保持", "1100 下降"):
            self.assertIn(expected, self.app.output_config.cget("text"))
        self.assertIn("左右 1300 / 1700", self.app.output_hint.cget("text"))
        self.assertIn("掌心按住：四轴 1500", self.app.output_hint.cget("text"))
        self.assertIn("平放满 1 秒：俯仰 1700 前进", self.app.output_hint.cget("text"))
        self.assertEqual(self.app.output_channel_names[2].cget("text"), "升降指令 VERTICAL")
        self.assertEqual(len(self.app.output_pickers), 1)
        self.app.set_output("RATE")
        self.app.set_output("RESET")
        self.assertEqual(self.app.link.commands, [])
        self.app._render(self.hc3_frame(tx_phase=16, air_stage=5, air_reason=10))
        self.assertIn("失败推力缓降（高度失效） ≠ 定高降落", self.app.height_status.cget("text"))
        self.assertIn("推力缓降", self.app.tx_status.cget("text"))
        self.app._render(self.hc3_frame(tx_phase=16, air_stage=5, air_reason=11))
        self.assertIn("失败推力缓降（起飞超时）", self.app.height_status.cget("text"))
        self.assertNotIn("高度失效", self.app.height_status.cget("text"))
        self.assertIn("起飞超时", self.app.tx_status.cget("text"))
        for reason, expected in ((10, "高度失效"), (11, "起飞超时"), (12, "条件不满足")):
            self.app._render(self.hc3_frame(tx_phase=0, tx_reason=7, air_reason=reason))
            self.assertIn(expected, self.app.tx_status.cget("text"))
        self.app._render(self.hc3_frame(tx_phase=1, air_height_valid=0, gcal=0,
                                      air_stage=0, air_height_mm=0, out_hex="AA" * 32))
        for geometry in ("1280x930", "1100x880"):
            self.app.geometry(geometry)
            self.app.update_idletasks()
            for widget in (self.app.tx_start3, self.app.tx_arm3, self.app.tx_takeoff, self.app.tx_land,
                           self.app.height_status, self.app.height_details, self.app.output_hint,
                           self.app.output_config, self.app.output_packet, self.app.output_notice, *self.app.output_values):
                self.assertTrue(widget.winfo_ismapped())
                self.assertGreaterEqual(widget.winfo_width(), widget.winfo_reqwidth())
                self.assertGreaterEqual(widget.winfo_height(), widget.winfo_reqheight())
                x = widget.winfo_rootx() - self.app.winfo_rootx()
                y = widget.winfo_rooty() - self.app.winfo_rooty()
                self.assertLessEqual(x + widget.winfo_width(), self.app.winfo_width(), str(widget))
                self.assertLessEqual(y + widget.winfo_height(), self.app.winfo_height(), str(widget))

    def test_hc3_serial_commands_and_trace(self):
        link = RealSerialLink(queue.SimpleQueue())
        link.port = MagicMock()
        for action in ("START3", "TAKEOFF", "LAND"):
            link.send_tx(action)
            link.port.write.assert_called_with(f"@T,{action}\n".encode("ascii"))
        frame = self.hc3_frame(v=1, out_rate=50)
        stopped = threading.Event()
        port = MagicMock()
        port.__enter__.return_value = port
        def read_one_frame():
            stopped.set()
            return ("@HC:" + json.dumps(frame) + "\n").encode()
        port.readline.side_effect = read_one_frame
        with patch.object(host, "find_port", return_value="fake-port"), \
             patch.object(host.serial, "Serial", return_value=port), \
             self.assertLogs(host.TRACE, level="INFO") as captured:
            link._run("fake-port", stopped)
        logged = json.loads(next(line.split("FRAME ", 1)[1] for line in captured.output if "FRAME " in line))
        for key in ("tx_v3", "out_rate", "tx_can_takeoff", "air_stage", "air_height_valid", "air_ground_ready",
                    "air_height_mm", "air_relative_mm", "air_collective", "air_radio_faults", "air_detail_age_ms"):
            self.assertEqual(logged[key], frame[key])
        port.write.assert_not_called()

    def test_manual_recovery_is_explicit_and_gated(self):
        for changes in ({},dict(air_resume_ready=1),dict(tx_can_resume=1),
                        dict(air_resume_ready=1,tx_can_resume=1,gesture=4),
                        dict(air_resume_ready=1,tx_can_resume=1,link_warning=1),
                        dict(air_resume_ready=1,tx_can_resume=1,air_age_ms=600)):
            self.app._render(self.hc3_frame(tx_phase=17,air_stage=7,**changes))
            self.assertEqual(str(self.app.tx_resume.cget("state")),"disabled")
            self.assertIn("手动油门",self.app.output_config.cget("text"))
        self.app._render(self.hc3_frame(tx_phase=17,air_stage=7,air_resume_ready=1,tx_can_resume=1))
        self.assertEqual(str(self.app.tx_resume.cget("state")),"normal")
        self.assertEqual(self.app.link.commands,[])
        self.app.tx_resume.invoke()
        self.assertEqual(self.app.link.commands,[("TX","RESUME")])
        self.app.emergency_stop()
        self.app.tx_action("RESUME")
        self.assertEqual(self.app.link.commands[-1],("TX","STOP"))
        self.assertEqual(str(self.app.tx_resume.cget("state")),"disabled")

    def test_single_protocol_and_downlink_warning(self):
        for attr in ("tx_start","tx_start2","tx_arm","tx_arm2","output_reset"):
            self.assertFalse(hasattr(self.app,attr))
        link=RealSerialLink(queue.SimpleQueue());link.port=MagicMock()
        for action in ("START","START2"):
            with self.assertRaises(ValueError):link.send_tx(action)
        for warning in (1,2):
            self.app._render(self.hc3_frame(tx_phase=14,air_stage=3,link_warning=warning))
            self.assertIn("控制包继续发送",self.app.link_alert.cget("text"))
            self.assertEqual(self.app.link.commands,[])
        self.app._render(self.hc3_frame(tx_phase=12,link_warning=2))
        self.assertEqual(str(self.app.tx_takeoff.cget("state")),"disabled")
        self.app._render(self.hc3_frame(tx_phase=14,air_stage=3,link_warning=0,
            tx_rx_max_us=812,tx_rx_late=0,air_rx_gap_ms=50,air_rx_max_gap_ms=120,air_first_event=10))
        self.assertIn("812μs",self.app.air_diagnostics.cget("text"))
        self.assertIn("120ms",self.app.air_diagnostics.cget("text"))
        self.assertIn("首个异常：高度异常",self.app.air_diagnostics.cget("text"))


if __name__ == "__main__":
    unittest.main()
