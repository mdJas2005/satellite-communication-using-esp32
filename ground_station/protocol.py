"""Frame format shared with the firmware. | AA 55 | type | seq(2) | len(4) | payload | crc16 |"""
import binascii
import struct

MAGIC = b"\xAA\x55"
T_TELEMETRY, T_IMAGE, T_ALERT, T_ACK = 1, 2, 3, 4
MAX_PAYLOAD = 2_000_000


def crc16(data: bytes, init: int = 0xFFFF) -> int:
    return binascii.crc_hqx(data, init)          # CRC-16/CCITT-FALSE


def pack(ftype: int, seq: int, payload: bytes = b"") -> bytes:
    body = struct.pack("<BHI", ftype, seq & 0xFFFF, len(payload)) + payload
    return MAGIC + body + struct.pack("<H", crc16(body))


class FrameReader:
    """Resynchronising frame parser over any object with .read(n) (pyserial, socket wrapper...)."""

    def __init__(self, stream):
        self.s = stream
        self.bad_frames = 0

    def _read_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.s.read(n - len(buf))
            if not chunk:
                return None
            buf += chunk
        return buf

    def read_frame(self):
        """Return (type, seq, payload) or None on timeout / corrupt frame."""
        while True:
            b = self.s.read(1)
            if not b:
                return None
            if b == b"\xAA":
                b2 = self.s.read(1)
                if not b2:
                    return None
                if b2 == b"\x55":
                    break
        hdr = self._read_exact(7)
        if hdr is None:
            return None
        ftype, seq, length = struct.unpack("<BHI", hdr)
        if length > MAX_PAYLOAD:
            self.bad_frames += 1
            return None
        rest = self._read_exact(length + 2)
        if rest is None:
            return None
        payload, (crc,) = rest[:-2], struct.unpack("<H", rest[-2:])
        if crc16(hdr + payload) != crc:
            self.bad_frames += 1
            return None
        return ftype, seq, payload
