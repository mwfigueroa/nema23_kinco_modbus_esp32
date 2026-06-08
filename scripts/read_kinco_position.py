#!/usr/bin/env python3
"""Read Kinco PLC step counter over Modbus TCP.

Default target:
  %VD200 / holding registers 40201-40202
  Modbus base-0 address 200, quantity 2

The ESP32 bridge listens on TCP port 502 and forwards Unit ID 1 to the PLC.
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys
import time


FC_READ_HOLDING_REGISTERS = 0x03


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed while reading Modbus response")
        data.extend(chunk)
    return bytes(data)


def read_holding_registers(
    host: str,
    port: int,
    unit_id: int,
    address: int,
    quantity: int,
    timeout: float,
) -> list[int]:
    transaction_id = int(time.monotonic() * 1000) & 0xFFFF
    protocol_id = 0
    pdu = struct.pack(">BHH", FC_READ_HOLDING_REGISTERS, address, quantity)
    mbap = struct.pack(">HHHB", transaction_id, protocol_id, len(pdu) + 1, unit_id)
    request = mbap + pdu

    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall(request)

        header = recv_exact(sock, 7)
        rx_tid, rx_pid, rx_len, rx_unit = struct.unpack(">HHHB", header)
        if rx_tid != transaction_id:
            raise RuntimeError(f"transaction id mismatch: sent {transaction_id}, got {rx_tid}")
        if rx_pid != 0:
            raise RuntimeError(f"invalid Modbus protocol id: {rx_pid}")
        if rx_unit != unit_id:
            raise RuntimeError(f"unit id mismatch: sent {unit_id}, got {rx_unit}")
        if rx_len < 2:
            raise RuntimeError(f"invalid Modbus length: {rx_len}")

        pdu_resp = recv_exact(sock, rx_len - 1)

    function = pdu_resp[0]
    if function == (FC_READ_HOLDING_REGISTERS | 0x80):
        code = pdu_resp[1] if len(pdu_resp) > 1 else 0
        raise RuntimeError(f"Modbus exception 0x{code:02X}")
    if function != FC_READ_HOLDING_REGISTERS:
        raise RuntimeError(f"unexpected function code: 0x{function:02X}")

    byte_count = pdu_resp[1]
    expected_bytes = quantity * 2
    if byte_count != expected_bytes or len(pdu_resp) != 2 + expected_bytes:
        raise RuntimeError(
            f"invalid register payload: byte_count={byte_count}, len={len(pdu_resp)}"
        )

    return [
        struct.unpack(">H", pdu_resp[2 + i * 2 : 4 + i * 2])[0]
        for i in range(quantity)
    ]


def words_to_int32(registers: list[int], word_order: str) -> int:
    if len(registers) != 2:
        raise ValueError("DINT conversion needs exactly 2 registers")
    if word_order == "low-high":
        raw = (registers[1] << 16) | registers[0]
    else:
        raw = (registers[0] << 16) | registers[1]
    if raw & 0x80000000:
        raw -= 0x100000000
    return raw


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Read Kinco step counter from VD200 over Modbus TCP."
    )
    parser.add_argument("host", help="ESP32 IP address or Modbus TCP host")
    parser.add_argument("--port", type=int, default=502, help="Modbus TCP port")
    parser.add_argument("--unit", type=int, default=1, help="PLC Modbus Unit ID")
    parser.add_argument(
        "--address",
        type=int,
        default=200,
        help="base-0 holding-register address for %%VD200; default 200 = 40201",
    )
    parser.add_argument(
        "--human-register",
        type=int,
        help="human holding register number, e.g. 40201; overrides --address",
    )
    parser.add_argument(
        "--word-order",
        choices=("low-high", "high-low"),
        default="low-high",
        help="32-bit word order; Kinco project default is low-high",
    )
    parser.add_argument("--timeout", type=float, default=2.0, help="seconds")
    parser.add_argument(
        "--interval",
        type=float,
        default=0.0,
        help="poll interval in seconds; 0 reads once",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=0,
        help="number of reads when polling; 0 means forever",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    address = args.human_register - 40001 if args.human_register else args.address
    if address < 0 or address > 0xFFFF:
        print(f"invalid Modbus address: {address}", file=sys.stderr)
        return 2

    reads = 0
    while True:
        try:
            regs = read_holding_registers(
                args.host,
                args.port,
                args.unit,
                address,
                2,
                args.timeout,
            )
            position = words_to_int32(regs, args.word_order)
            print(
                f"position_steps={position} "
                f"regs=[0x{regs[0]:04X},0x{regs[1]:04X}] "
                f"address={address} human={40001 + address}"
            )
        except Exception as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

        reads += 1
        if args.interval <= 0 or (args.count and reads >= args.count):
            break
        time.sleep(args.interval)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
