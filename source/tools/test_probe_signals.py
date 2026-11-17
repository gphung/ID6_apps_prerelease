import argparse
import socket
import struct
import threading
import unittest

from probe_signals import clear_request, ddli_block, ddli_source, define_request, exchange, read_request


def frame(control, data):
    return struct.pack(">IH", len(data), control) + data


class ProbeTest(unittest.TestCase):
    def test_reply_filters_and_heartbeat(self):
        client, gateway = socket.socketpair()
        with client, gateway:
            gateway.settimeout(2)
            def respond():
                self.assertEqual(gateway.recv(11), frame(1, bytes.fromhex("f412224300")))
                gateway.sendall(frame(0x12, b""))
                self.assertEqual(gateway.recv(8), frame(0x13, bytes.fromhex("00f4")))
                for value in ("18f46243000000", "12f47f2278", "12f46243000bf8"):
                    gateway.sendall(frame(1, bytes.fromhex(value)))
            thread = threading.Thread(target=respond)
            thread.start()
            result = exchange(client, 0x12, read_request("224300"))
            thread.join(3)
            self.assertFalse(thread.is_alive())
            self.assertEqual(result, {"reply": "62 43 00 0b f8", "data": "0b f8"})

    def test_refuses_actuation_and_decodes_negative_reply(self):
        for text in ("2f0100", "2e430001", "14ffffff", "2243", "01050c"):
            with self.assertRaises(argparse.ArgumentTypeError):
                read_request(text)
        client, gateway = socket.socketpair()
        with client, gateway:
            gateway.sendall(frame(1, bytes.fromhex("12f47f2231")))
            self.assertEqual(exchange(client, 0x12, read_request("22586f"))["nrc"], "31")


    def test_multi_did_read_matches_first_did(self):
        self.assertEqual(read_request("2242055889"), bytes.fromhex("2242055889"))
        for text in ("22" + "4205" * 9, "224205588", "0105010c"):
            with self.assertRaises(argparse.ArgumentTypeError):
                read_request(text)
        client, gateway = socket.socketpair()
        with client, gateway:
            gateway.sendall(frame(1, bytes.fromhex("12f462420532975889ffff")))
            result = exchange(client, 0x12, read_request("2242055889"))
            self.assertEqual(result["data"], "32 97 58 89 ff ff")

    def test_ddli_requests_are_bounded(self):
        self.assertEqual(define_request(0xF3F0, [ddli_source("4205:2"), ddli_source("586F:2")]),
                         bytes.fromhex("2c01f3f04205010258 6f0102".replace(" ", "")))
        self.assertEqual(clear_request(0xF3F0), bytes.fromhex("2c03f3f0"))
        client, gateway = socket.socketpair()
        with client, gateway:
            gateway.sendall(frame(1, bytes.fromhex("12f46c01f3f0")))
            define = define_request(0xF3F0, [ddli_source("4205:2")])
            self.assertEqual(exchange(client, 0x12, define)["reply"], "6c 01 f3 f0")
        for text in ("F300", "F3FF", "F2F0", "1234"):
            with self.assertRaises(argparse.ArgumentTypeError):
                ddli_block(text)
        for text in ("4205", "4205:9", "42:2"):
            with self.assertRaises(argparse.ArgumentTypeError):
                ddli_source(text)


if __name__ == "__main__":
    unittest.main()
