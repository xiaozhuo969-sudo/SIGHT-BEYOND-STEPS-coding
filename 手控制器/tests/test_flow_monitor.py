"""Offline formatter/decoder checks; no serial port access."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('monitor', Path(__file__).parents[1]/'tools/monitor_flow.py')
monitor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(monitor)

class DecodeTests(unittest.TestCase):
    def test_partial_does_not_invent_zero_velocity(self):
        frame = dict(v=1, mask=1, pages=[[20511,65535,1500,1800,8,0],None,None,None,None])
        d = monitor.decode_flow(frame)
        self.assertIsNone(d['source_age_ms'])
        self.assertNotIn('velocity_y_native', d)
        self.assertFalse(d['loop_active'])

    def test_signed_scale_and_target_error(self):
        frame = dict(v=1, mask=31, pages=[[20511,5,1500,1500,8,319],
            [65526,20,300,400,280,410], [10,65516,100,65336,65436,200],
            [65413,456,0,500,999,1000], [65535,2,3,4,300,129]])
        d = monitor.decode_flow(frame)
        self.assertEqual(d['velocity_x_native'], -1)
        self.assertEqual(d['position_error_x_native'], -2)
        self.assertEqual(d['position_error_y_native'], 1)
        self.assertEqual(d['pitch_target_deg'], -1)
        self.assertEqual(d['pitch_deg'], -1.23)
        self.assertEqual(d['raw_dx'], -1)
        self.assertEqual(d['motor_low_mask'], 1)
        self.assertEqual(d['motor_high_mask'], 8)

    def test_unknown_numeric_and_malformed(self):
        d = monitor.decode_flow(dict(v=1,mask=2,pages=[None,[32768]*6,None,None,None]))
        self.assertIsNone(d['velocity_y_native'])
        self.assertIsNone(d['position_error_y_native'])
        with self.assertRaises(ValueError):
            monitor.decode_flow(dict(v=1,mask=0,pages=[[0]*6,None,None,None,None]))
        with self.assertRaises(ValueError):
            monitor.decode_flow(dict(v=1,mask=1,pages=[[65536]*6,None,None,None,None]))

if __name__ == '__main__':
    unittest.main()
