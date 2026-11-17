#!/usr/bin/env python3
"""Read explicit BMW diagnostic signals over wired HSFZ while parked."""

import argparse
from ipaddress import IPv4Address
import json
import re
import socket
import struct
import time


def enet_address(value):
    address = IPv4Address(value)
    if not address.is_link_local or address.packed[2] in (0, 255):
        raise argparse.ArgumentTypeError("use a usable 169.254.x.x ENET address")
    return str(address)


def read_request(value):
    """One OBD PID (0105), or UDS 22 with one to eight DIDs (224300 or 2242055889...)."""
    if not re.fullmatch(r"01[0-9a-fA-F]{2}|22(?:[0-9a-fA-F]{4}){1,8}", value):
        raise argparse.ArgumentTypeError("use a read-only OBD PID (0105) or UDS DIDs (224300, 2242055889)")
    return bytes.fromhex(value)


def ddli_block(value):
    """Dynamically defined DID F3F0-F3FE for --ddli (temporary, cleared after the reads)."""
    if not re.fullmatch(r"[fF]3[fF][0-9a-eA-E]", value):
        raise argparse.ArgumentTypeError("use a dynamic DID from F3F0 to F3FE")
    return int(value, 16)


def ddli_source(value):
    """Source DID and byte count for --ddli, e.g. 4205:2."""
    match = re.fullmatch(r"([0-9a-fA-F]{4}):([1-8])", value)
    if not match:
        raise argparse.ArgumentTypeError("use DID:bytes, e.g. 4205:2")
    return int(match.group(1), 16), int(match.group(2))


def define_request(block, sources):
    """2C 01 defineByIdentifier: each source DID from its first byte, for its size."""
    return bytes((0x2C, 0x01)) + struct.pack(">H", block) + b"".join(
        struct.pack(">HBB", did, 1, size) for did, size in sources)


def clear_request(block):
    return bytes((0x2C, 0x03)) + struct.pack(">H", block)


def exact(connection, length, deadline):
    data = bytearray()
    while len(data) < length:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("no matching ECU response")
        connection.settimeout(remaining)
        part = connection.recv(length - len(data))
        if not part:
            raise ConnectionError("gateway closed the connection")
        data.extend(part)
    return data


def exchange(connection, ecu, request):
    connection.settimeout(2)
    connection.sendall(struct.pack(">IHBB", len(request) + 2, 1, 0xF4, ecu) + request)
    deadline = time.monotonic() + 2
    # A 22 reply echoes each DID before its data, so match the first DID only. A 2C reply
    # echoes the sub-function and the dynamic DID.
    echo = request[1:3] if request[0] == 0x22 else request[1:4] if request[0] == 0x2C else request[1:]
    prefix = bytes((request[0] + 0x40,)) + echo
    while True:
        length, control = struct.unpack(">IH", exact(connection, 6, deadline))
        if length > 8192:
            raise ValueError("oversized HSFZ frame")
        payload = exact(connection, length, deadline)
        if control == 0x12:
            connection.sendall(struct.pack(">IHBB", 2, 0x13, 0, 0xF4))
        if control != 1 or payload[:2] != bytes((ecu, 0xF4)):
            continue
        reply = bytes(payload[2:])
        if reply.startswith(prefix):
            return {"reply": reply.hex(" "), "data": reply[len(prefix):].hex(" ")}
        if len(reply) == 3 and reply[:2] == bytes((0x7F, request[0])) and reply[2] != 0x78:
            return {"reply": reply.hex(" "), "nrc": f"{reply[2]:02X}"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--local", required=True, type=enet_address, help="computer's wired ENET IPv4")
    parser.add_argument("--gateway", required=True, type=enet_address, help="diagnostic gateway IPv4")
    parser.add_argument("--ecu", choices=("12", "18"), default="12", help="12 for DME or 18 for EGS")
    parser.add_argument("--repeat", type=int, default=1, choices=range(1, 21), metavar="1-20",
                        help="send each request this many times on one connection and report the round-trip times")
    parser.add_argument("--ddli", type=ddli_block, metavar="F3F0",
                        help="DME only: define this temporary DID from DID:bytes sources (2C 01), read it, then clear it (2C 03)")
    parser.add_argument("requests", nargs="+")
    args = parser.parse_args()
    try:
        if args.ddli:
            if args.ecu != "12":
                parser.error("--ddli is limited to the DME")
            sources = [ddli_source(value) for value in args.requests]
            if len(sources) > 8:
                parser.error("at most 8 sources")
            plan = [("define", define_request(args.ddli, sources), 1),
                    ("read", bytes((0x22,)) + struct.pack(">H", args.ddli), args.repeat),
                    ("clear", clear_request(args.ddli), 1)]
        else:
            plan = [("read", read_request(value), args.repeat) for value in args.requests]
    except argparse.ArgumentTypeError as error:
        parser.error(str(error))
    if args.local == args.gateway or len(plan) > 12:
        parser.error("use separate local and gateway IPs and at most 12 explicit requests")
    try:
        # One connection per plan step for plain reads; one connection for a whole --ddli plan,
        # so the temporary DID is always cleared on the connection that defined it.
        connection = None
        for step, request, repeat in plan:
            if connection is None or not args.ddli:
                if connection is not None:
                    connection.close()
                connection = socket.create_connection((args.gateway, 6801), timeout=2, source_address=(args.local, 0))
            result = {"ecu": args.ecu, "step": step, "request": request.hex(" ")}
            times = []
            try:
                for _ in range(repeat):
                    start = time.monotonic()
                    result.update(exchange(connection, int(args.ecu, 16), request))
                    times.append(round((time.monotonic() - start) * 1000, 1))
            except TimeoutError as error:
                result["error"] = str(error)
            result["ms"] = times
            print(json.dumps(result), flush=True)
            time.sleep(0.2)
        if connection is not None:
            connection.close()
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from None


if __name__ == "__main__":
    main()
