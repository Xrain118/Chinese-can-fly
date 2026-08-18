import unittest

from ugv_bridge.protocol import (
    parse_ack,
    parse_bool_field,
    parse_frame,
    parse_int_field,
    twist_to_pwm,
)


class ProtocolTest(unittest.TestCase):
    def test_twist_to_pwm_straight(self):
        self.assertEqual(twist_to_pwm(0.4, 0.0, 0.28, 0.8), (500, 500))

    def test_twist_to_pwm_turn_and_clamp(self):
        left, right = twist_to_pwm(0.0, 20.0, 0.28, 0.8)
        self.assertEqual((left, right), (-1000, 1000))

    def test_parse_telemetry(self):
        prefix, fields = parse_frame("T R=1,F=4,BV=11820,ES=0")
        self.assertEqual(prefix, "T")
        self.assertEqual(fields["F"], "4")
        self.assertEqual(fields["BV"], "11820")

    def test_parse_ack(self):
        self.assertEqual(parse_ack("ERR C=START,M=FAULT"), (False, "START", "FAULT"))

    def test_parse_imu(self):
        prefix, fields = parse_frame("I AX=1,AY=-2,AZ=3,GX=4,GY=5,GZ=-6")
        self.assertEqual(prefix, "I")
        self.assertEqual(fields["AY"], "-2")
        self.assertEqual(fields["GZ"], "-6")

    def test_parse_int_field_fail_closed(self):
        fields = {"F": "0x04", "BV": "bad"}
        self.assertEqual(parse_int_field(fields, "F"), 4)
        self.assertIsNone(parse_int_field(fields, "BV"))
        self.assertIsNone(parse_int_field(fields, "MISSING"))

    def test_parse_bool_field_fail_closed(self):
        self.assertTrue(parse_bool_field({"ES": "ON"}, "ES"))
        self.assertFalse(parse_bool_field({"ES": "0"}, "ES"))
        self.assertIsNone(parse_bool_field({"ES": "abc"}, "ES"))


if __name__ == "__main__":
    unittest.main()
