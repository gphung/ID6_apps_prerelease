"""Exercise the real C collector against a loopback HSFZ gateway; no car needed."""
import os
import re
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

BIN = str(Path(__file__).resolve().parents[1] / "build/boostdeck-collector")
VALUES = {0x4201: bytes.fromhex("63ca"), 0x4205: bytes.fromhex("31e5"),
          0x5889: bytes.fromhex("104e"), 0x4A49: bytes.fromhex("004e"),
          0x580F: bytes.fromhex("82"), 0xDA12: bytes.fromhex("008e")}
VALUES.update({0x56D7: bytes.fromhex("1169"), 0x586F: bytes.fromhex("09c1"), 0x4300: bytes.fromhex("0bf8")})
CAR_VALUES = {0x4201: bytes.fromhex("6589"), 0x4205: bytes.fromhex("32dd"),
              0x5889: bytes.fromhex("1015"), 0x4A49: bytes.fromhex("ffff"),
              0x580F: bytes.fromhex("8c"), 0xDA12: bytes.fromhex("005a")}
CAR_VALUES.update({did: VALUES[did] for did in (0x56D7, 0x586F, 0x4300)})
# F87 M2 N55, ignition on, engine off, 2026-11-16. OBD 01 05 is keyed by PID.
N55_VALUES = {0x4201: bytes.fromhex("652e"), 0x4205: bytes.fromhex("3297"),
              0x5889: bytes.fromhex("ffff"), 0x4A49: bytes.fromhex("0000"),
              0x580F: bytes.fromhex("4d"), 0x586F: bytes.fromhex("0417"),
              0x4300: bytes.fromhex("4b"), 0x05: bytes.fromhex("30"),
              0x4408: bytes.fromhex("0050"),   # oil temperature 8.0 C, not in the engine-off capture
              0x58EF: bytes.fromhex("1770"),   # rail pressure 30 bar, not in the engine-off capture
              # Read on the car 2026-11-19 (warm idle) for the --metrics tiles.
              0x4A4E: bytes.fromhex("ffc0"), 0x4A36: bytes.fromhex("00"), 0x480B: bytes.fromhex("0000"),
              0x5853: bytes.fromhex("3ab5"), 0x5852: bytes.fromhex("2308"), 0x4807: bytes.fromhex("0a2e"),
              0x4023: bytes.fromhex("5b803a84450005000000002122222222223a3c3e3b3d39413300000086"),
              0x4AB0: bytes.fromhex("64ac"), 0x4654: bytes.fromhex("fccd"), 0x576A: bytes.fromhex("560c"),
              0x576B: bytes.fromhex("f976")}


def frame(ctrl, body):
    return struct.pack(">IH", len(body), ctrl) + body


def exact(conn, n):
    out = b""
    while len(out) < n:
        data = conn.recv(n - len(out))
        if not data:
            raise EOFError
        out += data
    return out


class TransportTest(unittest.TestCase):
    def run_collector(self, mode="normal", extra=(), cycles=1, env=None, host="127.0.0.1", expect="NBG3"):
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.bind(("127.0.0.1", 0))
        udp.settimeout(4)
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        server.settimeout(4)
        requests, errors = [], []

        def gateway():
            try:
                conn, _ = server.accept()
                with conn:
                    conn.settimeout(3)
                    while True:
                        first = conn.recv(1)
                        if not first:
                            break
                        length, ctrl = struct.unpack(">IH", first + exact(conn, 5))
                        self.assertEqual(ctrl, 1)
                        req = exact(conn, length)
                        requests.append(req)
                        self.assertEqual(req[0], 0xF4)
                        obd = req[2] == 0x01
                        pair = req[2] == 0x22 and length == 7
                        self.assertEqual(length, 4 if obd else 7 if pair else 5)
                        self.assertIn(req[2], (0x01, 0x22))  # read only
                        ecu, did = req[1], int.from_bytes(req[3:5], "big")
                        if pair:
                            self.assertTrue(mode.startswith("n55"))
                            conn.sendall(frame(2, req))  # ack
                            second = int.from_bytes(req[5:7], "big")
                            if mode == "n55-nopair" or (mode == "n55-knock-nrc" and 0x4A36 in (did, second)):
                                result = bytes([ecu, 0xF4, 0x7F, 0x22, 0x31])
                            else:
                                second = int.from_bytes(req[5:7], "big")
                                result = (bytes([ecu, 0xF4, 0x62]) + req[3:5] + N55_VALUES[did]
                                          + req[5:7] + N55_VALUES[second])
                            raw = frame(1, result)
                            for part in (raw[:2], raw[2:7], raw[7:]):
                                conn.sendall(part)
                            continue
                        if obd:
                            self.assertTrue(mode.startswith("n55"))
                        self.assertEqual(ecu, 0x18 if did == 0xDA12 else 0x12)
                        if did in (0x4408, 0x58EF):
                            self.assertTrue(mode.startswith("n55"))
                        if mode == "timeout":
                            time.sleep(.8)
                            return
                        if mode == "oversized":
                            conn.sendall(struct.pack(">IH", 500000, 1))
                            return
                        if mode == "heartbeat":
                            conn.sendall(frame(0x12, bytes([0, 0xF4])))
                            # Read the full reply, including its declared body.
                            alive_header = exact(conn, 6)
                            alive_length, alive_ctrl = struct.unpack(">IH", alive_header)
                            self.assertEqual(alive_ctrl, 0x13)
                            self.assertEqual(alive_length, 2)
                            self.assertEqual(exact(conn, alive_length), bytes([0, 0xF4]))
                        conn.sendall(frame(2, req))  # ack
                        # Wrong source with a valid-looking payload must be ignored.
                        conn.sendall(frame(1, bytes([0x99, 0xF4, req[2] + 0x40]) + req[3:] + b"\0\0"))
                        conn.sendall(frame(1, bytes([ecu, 0xF4, 0x7F, req[2], 0x78])))
                        if mode == "n55-knock-nrc" and did == 0x4A36:
                            result = bytes([ecu, 0xF4, 0x7F, 0x22, 0x31])
                        elif mode.startswith("n55"):
                            result = bytes([ecu, 0xF4, req[2] + 0x40]) + req[3:] + N55_VALUES[did]
                        elif mode == "no_trans" and did == 0xDA12:
                            result = bytes([ecu, 0xF4, 0x7F, 0x22, 0x31])
                        else:
                            values = CAR_VALUES if mode == "car_snapshot" else VALUES
                            payload = values[did]
                            if mode == "extra_pressure" and did == 0x4205:
                                payload += b"\xaa"
                            if mode == "extra_iat" and did == 0x580F:
                                payload += b"\xaa"
                            result = bytes([ecu, 0xF4, 0x62]) + req[3:] + payload
                        if mode == "paced":
                            time.sleep(.030)
                        raw = frame(1, result)
                        # Fragment both header and body to catch partial-read bugs.
                        for part in (raw[:2], raw[2:7], raw[7:]):
                            conn.sendall(part)
            except Exception as exc:
                errors.append(exc)

        t = threading.Thread(target=gateway, daemon=True)
        t.start()
        try:
            result = subprocess.run([BIN, "--host", host, "--port", str(server.getsockname()[1]),
                                     "--udp-port", str(udp.getsockname()[1]), "--cycles", str(cycles), *extra],
                                    capture_output=True, text=True, timeout=10, env=env)
            self.assertEqual(result.returncode, 0, result.stderr)
            packets = [udp.recv(1024).decode().split() for _ in range(cycles)]
            packet = packets[-1]
            self.assertEqual(packet[0], expect)
            if expect == "NBG3":
                self.assertGreater(int(packet[4]), 0)
            t.join(2)
            self.assertFalse(t.is_alive())
            if errors:
                raise errors[0]
            return packet, requests, result.stderr
        finally:
            udp.close()
            server.close()

    def test_recorded_values_and_fragmented_transport(self):
        p, requests, _ = self.run_collector()
        self.assertEqual(len(requests), 9)
        self.assertEqual(int(p[2]), 0)  # not demo
        self.assertEqual(int(p[3]), 255)
        values = [float(p[i]) for i in (5, 7, 9, 11, 13)]
        self.assertAlmostEqual(values[0], 0)
        self.assertAlmostEqual(values[1], 4174*16/65535*14.7, places=5)
        self.assertAlmostEqual(values[2], 7.8)
        self.assertAlmostEqual(values[3], 49.5)
        self.assertAlmostEqual(values[4], 94)
        self.assertAlmostEqual(float(p[15]), 89.14)
        self.assertAlmostEqual(float(p[17]), 2.497)
        self.assertAlmostEqual(float(p[19]), 33.25)

    def test_fast_channels_and_staggered_slow_updates(self):
        packet, requests, trace = self.run_collector("paced", ("--trace",), cycles=32)
        self.assertEqual(int(packet[3]), 255)
        self.assertEqual(len(packet), 21)  # eight pairs of values and ages
        self.assertNotIn(bytes.fromhex("f4120152"), requests)
        for did in ("4205", "5889", "4A49", "56D7", "586F"):
            samples = re.findall(rf"RAW mono_ms=(\d+) ecu=12 did={did}", trace)
            self.assertEqual(len(samples), 32)
            self.assertLess((int(samples[-1])-int(samples[1]))/30, 215)
        for did in ("580F", "DA12", "4300"):
            samples = re.findall(rf"RAW mono_ms=(\d+) ecu=\w+ did={did}", trace)
            gaps = [int(b)-int(a) for a,b in zip(samples[1:], samples[2:])]
            self.assertTrue(gaps)
            self.assertTrue(all(1700 < gap < 2300 for gap in gaps), (did,gaps))

    def test_rejected_transmission_is_missing(self):
        p, _, _ = self.run_collector("no_trans")
        self.assertEqual(int(p[3]), 239)

    def test_heartbeat_reply_uses_full_ediabaslib_shape(self):
        p, requests, _ = self.run_collector("heartbeat")
        self.assertEqual(len(requests), 9)
        self.assertEqual(int(p[3]), 255)

    def test_extra_did_bytes_do_not_become_valid_readings(self):
        for mode, expected_mask in [("extra_pressure", 254), ("extra_iat", 247)]:
            with self.subTest(mode=mode):
                p, requests, _ = self.run_collector(mode)
                self.assertEqual(len(requests), 9)
                self.assertEqual(int(p[3]), expected_mask)

    def test_timeout_is_bounded(self):
        start = time.monotonic()
        p, _, _ = self.run_collector("timeout")
        self.assertEqual(int(p[3]), 0)
        self.assertLess(time.monotonic()-start, 2)

    def test_oversized_frame_is_rejected(self):
        p, _, _ = self.run_collector("oversized")
        self.assertEqual(int(p[3]), 0)

    def test_this_cars_snapshot_and_ambiguous_timing_trace(self):
        p, requests, trace = self.run_collector("car_snapshot", ("--trace",))
        self.assertEqual(len(requests), 9)
        self.assertEqual(int(p[3]), 251)  # timing alone remains unavailable
        self.assertAlmostEqual(float(p[5]), .027761, places=5)
        self.assertAlmostEqual(float(p[7]), 14.775593, places=5)
        self.assertEqual(float(p[11]), 57)
        self.assertEqual(float(p[13]), 42)
        self.assertIn("did=4A49 payload=FFFF unavailable reason=ambiguous-FFFF", trace)
        self.assertEqual(trace.count("RAW mono_ms="), 9)

    def test_no_trans_flag_never_queries_egs(self):
        p, requests, _ = self.run_collector(extra=("--no-trans",))
        self.assertEqual(int(p[3]), 239)
        self.assertEqual(len(requests), 8)
        self.assertTrue(all(r[1] == 0x12 for r in requests))

    def run_most(self, args, fake="up", cycles=12):
        """Run with the in-process fake DSI; return every packet, the exit status and stderr."""
        env = dict(os.environ)
        env.pop("ID6_FAKE_MOST", None)
        if fake:
            env["ID6_FAKE_MOST"] = fake
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(.5)
            r = subprocess.run([BIN, *args, "--udp-port", str(udp.getsockname()[1]), "--cycles", str(cycles)],
                               capture_output=True, text=True, timeout=20, env=env)
            packets = []
            try:
                while True:
                    packets.append(udp.recv(512).decode().split())
            except socket.timeout:
                pass
        return packets, r.returncode, r.stderr

    @staticmethod
    def closed_port():
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("127.0.0.1", 0))
            return s.getsockname()[1]

    def assert_most_values(self, p):
        self.assertEqual(int(p[3]), 239)  # every gauge but the EGS transmission temperature
        self.assertAlmostEqual(float(p[7]), 4174*16/65535*14.7, places=5)
        self.assertAlmostEqual(float(p[9]), 7.8)
        self.assertAlmostEqual(float(p[11]), 49.5)
        self.assertAlmostEqual(float(p[15]), 89.14)
        self.assertAlmostEqual(float(p[19]), 33.25)

    def test_unreachable_gateway_switches_to_most(self):
        packets, rc, err = self.run_most(["--host", "127.0.0.1", "--port", str(self.closed_port())])
        self.assertEqual(rc, 0, err)
        self.assertEqual([int(p[3]) for p in packets[:4]], [0]*4)  # the fifth failed connect opens MOST
        self.assertIn("transport: MOST", err)
        self.assert_most_values(packets[-1])
        self.assertIn("FAKE-MOST exit unregister_target=1 unregister_client=1 free=1", err)

    def test_failed_most_open_is_closed_and_hsfz_retried(self):
        start = time.monotonic()
        packets, rc, err = self.run_most(["--host", "127.0.0.1", "--port", str(self.closed_port())],
                                         fake="no-server", cycles=12)
        self.assertEqual(rc, 0, err)
        self.assertLess(time.monotonic()-start, 8)
        self.assertIn("MOST open failed: no server", err)
        self.assertEqual(err.count("MOST open failed"), 1)  # backed off, not retried every tick
        self.assertIn("free=1", err)  # the failed open's DSI client was freed
        self.assertTrue(all(int(p[3]) == 0 for p in packets))

    def test_missing_gateway_file_uses_most(self):
        packets, rc, err = self.run_most(["--trace"], cycles=3)
        self.assertEqual(rc, 0, err)
        self.assert_most_values(packets[-1])
        self.assertNotIn("did=DA12", err)
        self.assertIn("ecu=12 did=4205 payload=31E5", err)

    def test_boostdeck_skips_egs_on_most(self):
        packets, rc, err = self.run_most(["--transport", "most", "--trace"], cycles=4)
        self.assertEqual(rc, 0, err)
        self.assertNotIn("did=DA12", err)
        self.assertNotIn("ecu=18", err)
        self.assertTrue(all(int(p[3]) & (1 << 4) == 0 for p in packets))  # transmission temperature invalid
        self.assertEqual(int(packets[-1][3]), 239)

    def test_working_gateway_never_touches_most(self):
        env = dict(os.environ, ID6_FAKE_MOST="up")
        p, requests, err = self.run_collector(cycles=3, env=env)
        self.assertEqual(int(p[3]), 255)
        self.assertNotIn("FAKE-MOST", err)
        _, rc, err = self.run_most(["--transport", "hsfz", "--host", "127.0.0.1",
                                    "--port", str(self.closed_port())], cycles=10)
        self.assertEqual(rc, 0, err)
        self.assertNotIn("FAKE-MOST", err)

    def test_missing_dsi_library_keeps_emitting_no_data(self):
        packets, rc, err = self.run_most(["--transport", "most"], fake=None, cycles=4)
        self.assertEqual(rc, 0, err)
        self.assertEqual(len(packets), 4)
        self.assertTrue(all(int(p[3]) == 0 for p in packets))
        self.assertIn("MOST open failed: no library", err)
        self.assertEqual(err.count("FAKE-MOST bind"), 1)  # not retried every tick

    def test_lost_most_link_exits_for_a_fresh_worker(self):
        packets, rc, err = self.run_most(["--transport", "most"], fake="drop", cycles=50)
        self.assertEqual(rc, 1, err)
        self.assertLess(len(packets), 50)

    def test_busy_tunnel_keeps_fresh_values(self):
        """A read skipped for a busy lease leaves a still-fresh gauge shown."""
        env = dict(os.environ, ID6_FAKE_MOST="up")
        env.pop("ID6_FAKE_MOST_DELAY_MS", None)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(1)
            proc = subprocess.Popen([BIN, "--transport", "most", "--udp-port", str(udp.getsockname()[1]),
                                     "--cycles", "30"], stderr=subprocess.DEVNULL, env=env)
            masks, held = [], False
            try:
                while proc.poll() is None or len(masks) < 30:
                    try:
                        mask = int(udp.recv(512).decode().split()[3])
                    except socket.timeout:
                        break
                    masks.append(mask)
                    if mask == 239 and not held:
                        held = True
                        lock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        lock.bind(("127.0.0.1", 42861))
                        threading.Timer(1.0, lock.close).start()
            finally:
                if proc.poll() is None:
                    proc.kill()
                proc.wait()
        self.assertTrue(held, masks)
        shown = masks[masks.index(239):]
        # Boost needs a fresh ambient read too, so only the other MOST gauges are checked.
        self.assertTrue(all(m & 238 == 238 for m in shown), shown)

    def test_hsfz_needs_host(self):
        r = subprocess.run([BIN, "--transport", "hsfz"], capture_output=True)
        self.assertEqual(r.returncode, 2)

    def test_sigterm_closes_most_session(self):
        env = dict(os.environ, ID6_FAKE_MOST="up")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(3)
            proc = subprocess.Popen([BIN, "--session", "--udp-port", str(udp.getsockname()[1])],
                                    stderr=subprocess.PIPE, text=True, env=env)
            try:
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline:
                    if int(udp.recv(512).decode().split()[3]) == 239:
                        break
                else:
                    self.fail("no MOST values")
                start = time.monotonic()
                proc.send_signal(signal.SIGTERM)
                self.assertEqual(proc.wait(5), 0)
                self.assertLess(time.monotonic()-start, 3)
            finally:
                if proc.poll() is None:
                    proc.kill()
                _, err = proc.communicate()
        self.assertIn("FAKE-MOST exit unregister_target=1 unregister_client=1 free=1", err)

    def check_n55_values(self, p):
        # Everything but AFR (engine-off FFFF) is valid.
        self.assertEqual(int(p[3]), 0b11111101)
        self.assertAlmostEqual(float(p[5]), 0, places=1)
        self.assertAlmostEqual(float(p[11]), 9.75)
        self.assertAlmostEqual(float(p[13]), 8.0)                       # oil temp, signed x 0.1 C
        self.assertAlmostEqual(float(p[15]), 30.0)                      # rail, x 0.005 bar
        self.assertAlmostEqual(float(p[17]), 1.047 - 25902/256/100, places=5)  # oil, minus ambient
        self.assertAlmostEqual(float(p[19]), 8)

    def test_n55_profile_pairs_boost_with_afr_and_timing_with_oil_pressure(self):
        p, requests, trace = self.run_collector("n55", ("--profile", "n55", "--trace"))
        self.assertEqual(len(requests), 7)   # 9 values, two of them paired
        self.assertIn(bytes.fromhex("f4122242055889"), requests)
        self.assertIn(bytes.fromhex("f412224a49586f"), requests)
        self.assertIn("did=586F payload=0417 pair=4A49+586F decoded=1.047000", trace)
        self.check_n55_values(p)

    def test_n55_refused_pairs_fall_back_to_single_reads(self):
        p, requests, _ = self.run_collector("n55-nopair", ("--profile", "n55"), cycles=2)
        pairs = [r for r in requests if len(r) == 7]
        self.assertEqual(len(pairs), 1)      # refused once, never asked again
        for single in ("f412224205", "f412225889", "f412224a49", "f41222586f"):
            self.assertIn(bytes.fromhex(single), requests)
        self.check_n55_values(p)

    def test_n55_profile_reads_its_own_dids_and_no_egs(self):
        p, requests, trace = self.run_collector("n55", ("--profile", "n55", "--trace"))
        self.assertEqual(len(requests), 7)
        self.assertTrue(all(r[1] == 0x12 for r in requests))  # never the EGS
        for wanted in ("f4120105", "f412224408", "f4122258ef"):
            self.assertIn(bytes.fromhex(wanted), requests)
        for unwanted in ("f412224300", "f41222da12", "f4122256d7"):
            self.assertNotIn(bytes.fromhex(unwanted), requests)
        # Everything but AFR (engine-off FFFF) is valid.
        self.assertEqual(int(p[3]), 0b11111101)
        self.assertAlmostEqual(float(p[5]), 0, places=1)
        self.assertAlmostEqual(float(p[11]), 9.75)
        self.assertAlmostEqual(float(p[13]), 8.0)                       # oil temp, signed x 0.1 C
        self.assertAlmostEqual(float(p[15]), 30.0)                      # rail, x 0.005 bar
        self.assertAlmostEqual(float(p[17]), 1.047 - 25902/256/100, places=5)  # oil, minus ambient
        self.assertAlmostEqual(float(p[19]), 8)
        self.assertIn("did=4408 payload=0050 decoded=8.000000", trace)
        self.assertIn("pid=05 payload=30 decoded=8.000000", trace)
        self.assertIn("F87 M2 N55 candidate profile", trace)

    def test_n55_rail_every_500_ms_and_oil_pressure_every_scan(self):
        _, _, trace = self.run_collector("n55", ("--profile", "n55", "--trace"), cycles=40)
        times = lambda did: [int(t) for t in re.findall(rf"RAW mono_ms=(\d+) ecu=12 did={did}", trace)]
        rail, oil = times("58EF"), times("586F")
        self.assertEqual(len(oil), 40)                    # every scan
        self.assertLess(len(rail), len(oil) / 2)
        gaps = [b - a for a, b in zip(rail[1:], rail[2:])]  # the first two are the initial scan
        self.assertTrue(gaps and all(400 <= g <= 700 for g in gaps), gaps)  # 500 ms on 150 ms host ticks

    def run_metrics(self, keys, mode="n55", cycles=1, extra=()):
        p, requests, trace = self.run_collector(mode, ("--metrics", ",".join(keys), *extra), cycles=cycles,
                                                expect="NBN1")
        count = int(p[3])
        values = {}
        for k in range(count):
            metric, value, aux, age = p[4 + 4 * k: 8 + 4 * k]
            values[int(metric)] = (float(value), float(aux), int(age))
        return values, requests, trace

    METRIC = {"boost": 0, "timing": 6, "timing1": 7, "timing2": 8, "knock": 13, "oil_temp": 21,
              "wastegate": 27, "rpm": 31, "battery": 33, "battery_amps": 34, "battery_pct": 35}

    def test_metrics_read_each_did_once_two_per_request(self):
        keys = ("timing", "timing1", "timing2", "knock", "battery", "battery_amps", "battery_pct", "oil_temp", "rpm")
        values, requests, trace = self.run_metrics(keys, extra=("--trace",))
        dids = []
        for r in requests:
            dids += [r[3:5].hex()] + ([r[5:7].hex()] if len(r) == 7 else [])
        self.assertEqual(sorted(dids), sorted(set(dids)))       # nothing read twice
        self.assertEqual(dids.count("4a49"), 1)                 # Timing and Timing 1 share it
        self.assertEqual(len(requests), 5)                      # 4 pairs and the 4023 block alone
        m = self.METRIC
        self.assertAlmostEqual(values[m["timing"]][0], 0)
        self.assertEqual(values[m["timing"]][0], values[m["timing1"]][0])
        self.assertAlmostEqual(values[m["timing2"]][0], -6.4)
        self.assertAlmostEqual(values[m["battery"]][0], 15.029)
        self.assertAlmostEqual(values[m["battery_amps"]][0], 20.64)  # charging shown positive
        self.assertAlmostEqual(values[m["battery_pct"]][0], 58)
        self.assertAlmostEqual(values[m["rpm"]][0], 651.5)
        self.assertEqual(values[m["knock"]][:2], (0, 0))
        self.assertIn("did=4A49 payload=0000 pair=", trace)

    def test_metrics_boost_and_wastegate_need_their_helpers(self):
        values, requests, _ = self.run_metrics(("boost", "wastegate", "oil_temp"))
        dids = set()
        for r in requests:
            dids |= {r[3:5].hex()} | ({r[5:7].hex()} if len(r) == 7 else set())
        self.assertTrue({"4201", "576a", "576b"} <= dids)
        m = self.METRIC
        self.assertAlmostEqual(values[m["boost"]][0], (0x3297 * 0.078125 - 0x652e * 0.0390625) / 1000, places=4)
        self.assertAlmostEqual(values[m["wastegate"]][0], 3.6, places=1)

    def test_metrics_slow_values_wait_for_their_rate(self):
        _, _, trace = self.run_metrics(("boost", "battery_pct", "oil_temp"), cycles=6, extra=("--trace",))
        self.assertEqual(len(re.findall(r"did=4023 ", trace)), 1)   # every 10 s
        self.assertEqual(len(re.findall(r"did=4205 ", trace)), 6)   # every scan

    def test_metrics_one_refused_did_keeps_pairs(self):
        values, requests, trace = self.run_metrics(("knock", "timing", "rpm", "oil_temp"), mode="n55-knock-nrc",
                                                   cycles=3)
        self.assertNotIn(self.METRIC["knock"], values)
        self.assertIn(self.METRIC["timing"], values)
        self.assertIn("DID 4A36 NRC 31", trace)
        later = requests[3:]
        self.assertTrue(any(len(r) == 7 for r in later))        # still pairing
        self.assertNotIn(bytes.fromhex("f412224a36"), later)      # and not asking for knock again

    def test_metrics_refused_pairs_fall_back_to_single_reads(self):
        values, requests, _ = self.run_metrics(("timing", "rpm", "knock", "oil_temp"), mode="n55-nopair", cycles=2)
        self.assertEqual(sum(len(r) == 7 for r in requests), 1)
        self.assertEqual(len(values), 4)

    def test_metrics_rejects_bad_lists(self):
        for bad in ("boost,boost", "nope", ""):
            result = subprocess.run([BIN, "--demo", "--metrics", bad, "--cycles", "1"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2, bad)

    def test_gateway_list_tries_each_address_in_order(self):
        p, requests, _ = self.run_collector(host="127.0.0.2,not-an-ip,127.0.0.1")
        self.assertEqual(len(requests), 9)
        self.assertEqual(int(p[3]), 255)

    def test_discovery_finds_the_gateway_when_no_listed_address_answers(self):
        responder = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        responder.bind(("127.0.0.1", 0))
        responder.settimeout(4)
        seen = []

        def answer():
            data, peer = responder.recvfrom(64)
            seen.append(data)
            # The head unit replies too; only the gateway's DIAGADR10 counts.
            for tag in (b"DIAGADR63", b"DIAGADR10"):
                body = tag + b"BMWMAC001122334455BMWVINTESTVIN0000000000"
                responder.sendto(struct.pack(">IH", len(body), 0x11) + body, peer)

        threading.Thread(target=answer, daemon=True).start()
        with tempfile.TemporaryDirectory() as directory:
            cache = os.path.join(directory, "gateway")
            env = dict(os.environ, ID6_HSFZ_DISCOVERY="127.0.0.1:%d" % responder.getsockname()[1],
                       ID6_HSFZ_CACHE=cache)
            p, requests, err = self.run_collector(env=env, host="127.0.0.2")
            responder.close()
            self.assertEqual(seen, [bytes.fromhex("000000000011")])
            self.assertIn("gateway: discovered 127.0.0.1", err)
            self.assertEqual(int(p[3]), 255)
            with open(cache) as f:
                self.assertEqual(f.read().strip(), "127.0.0.1")
            # The next worker starts from the remembered address, without a broadcast.
            p, requests, err = self.run_collector(env=dict(env, ID6_HSFZ_DISCOVERY="127.0.0.1:1"), host="127.0.0.2")
            self.assertNotIn("discovered", err)
            self.assertEqual(int(p[3]), 255)

    def test_loop_worker_returns_from_most_to_hsfz(self):
        port = self.closed_port()
        requests = []

        def gateway_later():
            time.sleep(1.5)  # the worker is on MOST by then
            server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind(("127.0.0.1", port))
            server.listen(1)
            server.settimeout(6)
            try:
                conn, _ = server.accept()
                with conn:
                    conn.settimeout(3)
                    while True:
                        length, _ = struct.unpack(">IH", exact(conn, 6))
                        req = exact(conn, length)
                        requests.append(req)
                        did = int.from_bytes(req[3:], "big")
                        conn.sendall(frame(1, bytes([req[1], 0xF4, 0x62]) + req[3:] + VALUES[did]))
            except (OSError, EOFError):
                pass
            finally:
                server.close()

        t = threading.Thread(target=gateway_later, daemon=True)
        t.start()
        env = dict(os.environ, ID6_FAKE_MOST="up", ID6_HSFZ_FROM_MOST_MS="400")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(.5)
            r = subprocess.run([BIN, "--host", "127.0.0.1", "--port", str(port), "--udp-port",
                                str(udp.getsockname()[1]), "--cycles", "30"],
                               capture_output=True, text=True, timeout=30, env=env)
            packets = []
            try:
                while True:
                    packets.append(udp.recv(512).decode().split())
            except socket.timeout:
                pass
        t.join(8)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertLess(r.stderr.index("transport: MOST"), r.stderr.index("transport: HSFZ"))
        self.assertIn("FAKE-MOST exit", r.stderr)            # the tunnel was closed
        self.assertTrue(any(req[1] == 0x18 for req in requests))  # EGS is back on HSFZ
        self.assertEqual(int(packets[-1][3]), 255)

    def test_unknown_profile_is_rejected(self):
        r = subprocess.run([BIN, "--host", "127.0.0.1", "--profile", "b58"], capture_output=True)
        self.assertEqual(r.returncode, 2)

    def test_demo_never_uses_vehicle(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(2)
            r = subprocess.run([BIN, "--demo", "--cycles", "1", "--udp-port", str(udp.getsockname()[1])],
                               capture_output=True, timeout=2)
            self.assertEqual(r.returncode, 0)
            self.assertEqual(udp.recv(512).decode().split()[2:4], ["1", "255"])
        r = subprocess.run([BIN, "--demo", "--host", "127.0.0.1"], capture_output=True)
        self.assertEqual(r.returncode, 2)


if __name__ == "__main__":
    unittest.main()
