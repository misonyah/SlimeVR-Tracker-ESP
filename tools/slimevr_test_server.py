"""
Minimal SlimeVR UDP test server.
Run instead of the full SlimeVR server to verify trackers send motion data.

Usage:
    python tools/slimevr_test_server.py

Press Ctrl+C to stop.
"""

import socket
import struct
import time

UDP_PORT = 6969

# Packet types tracker → server (SendPacketType enum)
SEND_HEARTBEAT = 0
SEND_HANDSHAKE = 3
SEND_ACCEL = 4
SEND_BATTERY = 12
SEND_SENSOR_INFO = 15
SEND_ROTATION_DATA = 17
SEND_SIGNAL_STRENGTH = 19
SEND_FEATURE_FLAGS = 22
SEND_BUNDLE = 100

# Packet types server → tracker (ReceivePacketType enum)
RECV_HEARTBEAT = 1
RECV_HANDSHAKE = 3
RECV_PING_PONG = 10
RECV_FEATURE_FLAGS = 22

# Tracker must receive a packet within 3 seconds or it drops connection
TRACKER_TIMEOUT_S = 2.5


def make_heartbeat():
    return struct.pack(">IQ", RECV_HEARTBEAT, 0)


def make_handshake_reply():
    # Tracker checks: m_Packet[0] == 3, then strncmp(m_Packet+1, "Hey OVR =D 5", 12)
    return bytes([RECV_HANDSHAKE]) + b"Hey OVR =D 5" + b"\x00" * 8


def make_feature_flags_reply():
    # type (4) + packet number (8) + 1 flag byte (all zero = no bundle support)
    # With no PROTOCOL_BUNDLE_SUPPORT, tracker sends standalone rotation packets
    return struct.pack(">IQ", RECV_FEATURE_FLAGS, 0) + bytes([0])


def dispatch(data, addr, sock, trackers, in_bundle=False):
    if len(data) < 4:
        return

    ptype = struct.unpack_from(">I", data, 0)[0]

    # Sub-packets in a bundle omit the 8-byte packet number; standalone have it.
    # payload_offset is where actual packet payload begins after type (+ pkt number).
    payload_offset = 4 if in_bundle else 12

    if ptype == SEND_HANDSHAKE:
        if addr not in trackers:
            trackers[addr] = {
                "name": addr[0],
                "last_rx": time.time(),
                "last_tx": 0.0,
                "count": 0,
                "features_sent": False,
            }
            print(f"[+] Tracker connected: {addr[0]}")
        else:
            trackers[addr]["last_rx"] = time.time()
        sock.sendto(make_handshake_reply(), addr)
        trackers[addr]["last_tx"] = time.time()

    elif ptype == SEND_FEATURE_FLAGS:
        if addr in trackers:
            trackers[addr]["last_rx"] = time.time()
            if not trackers[addr]["features_sent"]:
                sock.sendto(make_feature_flags_reply(), addr)
                trackers[addr]["last_tx"] = time.time()
                trackers[addr]["features_sent"] = True

    elif ptype == SEND_HEARTBEAT:
        if addr in trackers:
            trackers[addr]["last_rx"] = time.time()
        sock.sendto(make_heartbeat(), addr)
        if addr in trackers:
            trackers[addr]["last_tx"] = time.time()

    elif ptype == RECV_PING_PONG:
        sock.sendto(data, addr)
        if addr in trackers:
            trackers[addr]["last_rx"] = time.time()
            trackers[addr]["last_tx"] = time.time()

    elif ptype == SEND_ROTATION_DATA:
        if addr in trackers:
            trackers[addr]["last_rx"] = time.time()
            t = trackers[addr]
            t["count"] += 1
            if t["count"] % 50 == 1 and len(data) >= payload_offset + 18:
                sensor_id = data[payload_offset]
                x, y, z, w = struct.unpack_from(">ffff", data, payload_offset + 2)
                print(f"  {addr[0]}  sensor={sensor_id}  quat=({w:.3f}, {x:.3f}, {y:.3f}, {z:.3f})")

    elif ptype == SEND_BUNDLE:
        handle_bundle(data, addr, sock, trackers)

    else:
        if addr in trackers:
            trackers[addr]["last_rx"] = time.time()
        # Log unknown/unexpected types to help diagnose
        name = {
            0: "HEARTBEAT", 3: "HANDSHAKE", 4: "ACCEL", 12: "BATTERY",
            15: "SENSOR_INFO", 17: "ROTATION_DATA", 19: "SIGNAL_STRENGTH",
            22: "FEATURE_FLAGS", 24: "ACK_CONFIG", 100: "BUNDLE",
        }.get(ptype, f"UNKNOWN({ptype})")
        if not in_bundle:
            print(f"  {addr[0]}  pkt={name}  len={len(data)}")


def handle_bundle(data, addr, sock, trackers):
    # Bundle: [type 4B][pkt_num 8B] then N×([sub_len 2B][sub_pkt sub_len B])
    offset = 12
    while offset + 2 <= len(data):
        sub_len = struct.unpack_from(">H", data, offset)[0]
        offset += 2
        if sub_len == 0 or offset + sub_len > len(data):
            break
        dispatch(data[offset:offset + sub_len], addr, sock, trackers, in_bundle=True)
        offset += sub_len


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", UDP_PORT))
    sock.settimeout(0.5)
    print(f"Listening on UDP :{UDP_PORT}  (Ctrl+C to stop)")
    print("Waiting for trackers...\n")

    trackers = {}
    pkt_counts = {}   # {ptype: count} across all trackers
    last_status = time.time()

    TYPE_NAMES = {
        0: "HEARTBEAT", 3: "HANDSHAKE", 4: "ACCEL", 12: "BATTERY",
        15: "SENSOR_INFO", 17: "ROTATION_DATA", 19: "SIGNAL",
        22: "FEATURE_FLAGS", 100: "BUNDLE",
    }

    while True:
        now = time.time()

        # Send heartbeats to connected trackers before they time out
        for addr, t in list(trackers.items()):
            if now - t["last_tx"] > 1.0:
                sock.sendto(make_heartbeat(), addr)
                t["last_tx"] = now

        try:
            data, addr = sock.recvfrom(4096)
            if len(data) >= 4:
                ptype = struct.unpack_from(">I", data, 0)[0]
                pkt_counts[ptype] = pkt_counts.get(ptype, 0) + 1
            dispatch(data, addr, sock, trackers)
        except socket.timeout:
            pass
        except KeyboardInterrupt:
            break

        # Status summary every 5 seconds
        now = time.time()
        if now - last_status >= 5.0 and trackers:
            last_status = now
            print(f"\n--- {len(trackers)} tracker(s)  packet type breakdown ---")
            for pt, cnt in sorted(pkt_counts.items()):
                print(f"  type {pt:3d}  {TYPE_NAMES.get(pt,'?'):15s}  n={cnt}")
            for addr, t in trackers.items():
                age = now - t["last_rx"]
                print(f"  {addr[0]}  rot_pkts={t['count']}  last_rx={age:.1f}s ago")


if __name__ == "__main__":
    main()
