#!/usr/bin/env python3
import argparse
import os
import select
import subprocess
import sys
import time


class MsgpackCodec:
    def pack(self, value):
        if value is None:
            return b"\xc0"
        if value is False:
            return b"\xc2"
        if value is True:
            return b"\xc3"
        if isinstance(value, int):
            return self._pack_int(value)
        if isinstance(value, str):
            data = value.encode("utf-8")
            return self._pack_bytes_header(len(data), 0xA0, 0xD9, 0xDA, 0xDB) + data
        if isinstance(value, (bytes, bytearray)):
            data = bytes(value)
            return self._pack_bytes_header(len(data), 0xC4, 0xC4, 0xC5, 0xC6) + data
        if isinstance(value, (list, tuple)):
            return self._pack_array_header(len(value)) + b"".join(self.pack(item) for item in value)
        if isinstance(value, dict):
            chunks = [self._pack_map_header(len(value))]
            for key, item in value.items():
                chunks.append(self.pack(key))
                chunks.append(self.pack(item))
            return b"".join(chunks)
        raise TypeError(f"unsupported msgpack value: {type(value)!r}")

    def unpack_stream(self, data):
        values = []
        offset = 0
        while offset < len(data):
            try:
                value, offset = self._unpack_one(data, offset)
            except ValueError:
                break
            values.append(value)
        return values, data[offset:]

    def _pack_int(self, value):
        if 0 <= value <= 0x7F:
            return bytes([value])
        if -32 <= value < 0:
            return bytes([0x100 + value])
        if 0 <= value <= 0xFF:
            return b"\xcc" + value.to_bytes(1, "big")
        if 0 <= value <= 0xFFFF:
            return b"\xcd" + value.to_bytes(2, "big")
        if 0 <= value <= 0xFFFFFFFF:
            return b"\xce" + value.to_bytes(4, "big")
        if 0 <= value <= 0xFFFFFFFFFFFFFFFF:
            return b"\xcf" + value.to_bytes(8, "big")
        if -0x80 <= value < 0:
            return b"\xd0" + value.to_bytes(1, "big", signed=True)
        if -0x8000 <= value < 0:
            return b"\xd1" + value.to_bytes(2, "big", signed=True)
        if -0x80000000 <= value < 0:
            return b"\xd2" + value.to_bytes(4, "big", signed=True)
        return b"\xd3" + value.to_bytes(8, "big", signed=True)

    def _pack_bytes_header(self, length, fix_base, code8, code16, code32):
        if fix_base == 0xA0 and length <= 31:
            return bytes([fix_base | length])
        if length <= 0xFF:
            return bytes([code8, length])
        if length <= 0xFFFF:
            return bytes([code16]) + length.to_bytes(2, "big")
        return bytes([code32]) + length.to_bytes(4, "big")

    def _pack_array_header(self, length):
        if length <= 15:
            return bytes([0x90 | length])
        if length <= 0xFFFF:
            return b"\xdc" + length.to_bytes(2, "big")
        return b"\xdd" + length.to_bytes(4, "big")

    def _pack_map_header(self, length):
        if length <= 15:
            return bytes([0x80 | length])
        if length <= 0xFFFF:
            return b"\xde" + length.to_bytes(2, "big")
        return b"\xdf" + length.to_bytes(4, "big")

    def _need(self, data, offset, count):
        if offset + count > len(data):
            raise ValueError("need more data")

    def _unpack_one(self, data, offset):
        self._need(data, offset, 1)
        marker = data[offset]
        offset += 1

        if marker <= 0x7F:
            return marker, offset
        if marker >= 0xE0:
            return marker - 0x100, offset
        if 0xA0 <= marker <= 0xBF:
            return self._read_text(data, offset, marker & 0x1F)
        if 0x90 <= marker <= 0x9F:
            return self._read_array(data, offset, marker & 0x0F)
        if 0x80 <= marker <= 0x8F:
            return self._read_map(data, offset, marker & 0x0F)
        if marker == 0xC0:
            return None, offset
        if marker == 0xC2:
            return False, offset
        if marker == 0xC3:
            return True, offset
        if marker == 0xCC:
            self._need(data, offset, 1)
            return data[offset], offset + 1
        if marker == 0xCD:
            return self._read_uint(data, offset, 2)
        if marker == 0xCE:
            return self._read_uint(data, offset, 4)
        if marker == 0xCF:
            return self._read_uint(data, offset, 8)
        if marker == 0xD0:
            return self._read_int(data, offset, 1)
        if marker == 0xD1:
            return self._read_int(data, offset, 2)
        if marker == 0xD2:
            return self._read_int(data, offset, 4)
        if marker == 0xD3:
            return self._read_int(data, offset, 8)
        if marker == 0xD9:
            self._need(data, offset, 1)
            length = data[offset]
            return self._read_text(data, offset + 1, length)
        if marker == 0xDA:
            length, offset = self._read_uint(data, offset, 2)
            return self._read_text(data, offset, length)
        if marker == 0xDB:
            length, offset = self._read_uint(data, offset, 4)
            return self._read_text(data, offset, length)
        if marker == 0xC4:
            self._need(data, offset, 1)
            length = data[offset]
            return self._read_bytes(data, offset + 1, length)
        if marker == 0xC5:
            length, offset = self._read_uint(data, offset, 2)
            return self._read_bytes(data, offset, length)
        if marker == 0xC6:
            length, offset = self._read_uint(data, offset, 4)
            return self._read_bytes(data, offset, length)
        if marker == 0xC7:
            self._need(data, offset, 1)
            length = data[offset]
            return self._read_ext(data, offset + 1, length)
        if marker == 0xC8:
            length, offset = self._read_uint(data, offset, 2)
            return self._read_ext(data, offset, length)
        if marker == 0xC9:
            length, offset = self._read_uint(data, offset, 4)
            return self._read_ext(data, offset, length)
        if marker == 0xD4:
            return self._read_ext(data, offset, 1)
        if marker == 0xD5:
            return self._read_ext(data, offset, 2)
        if marker == 0xD6:
            return self._read_ext(data, offset, 4)
        if marker == 0xD7:
            return self._read_ext(data, offset, 8)
        if marker == 0xD8:
            return self._read_ext(data, offset, 16)
        if marker == 0xDC:
            length, offset = self._read_uint(data, offset, 2)
            return self._read_array(data, offset, length)
        if marker == 0xDD:
            length, offset = self._read_uint(data, offset, 4)
            return self._read_array(data, offset, length)
        if marker == 0xDE:
            length, offset = self._read_uint(data, offset, 2)
            return self._read_map(data, offset, length)
        if marker == 0xDF:
            length, offset = self._read_uint(data, offset, 4)
            return self._read_map(data, offset, length)

        raise NotImplementedError(f"unsupported msgpack marker 0x{marker:02x}")

    def _read_uint(self, data, offset, size):
        self._need(data, offset, size)
        return int.from_bytes(data[offset:offset + size], "big"), offset + size

    def _read_int(self, data, offset, size):
        self._need(data, offset, size)
        return int.from_bytes(data[offset:offset + size], "big", signed=True), offset + size

    def _read_text(self, data, offset, length):
        raw, offset = self._read_bytes(data, offset, length)
        return raw.decode("utf-8", errors="replace"), offset

    def _read_bytes(self, data, offset, length):
        self._need(data, offset, length)
        return data[offset:offset + length], offset + length

    def _read_ext(self, data, offset, length):
        self._need(data, offset, length + 1)
        ext_type = int.from_bytes(data[offset:offset + 1], "big", signed=True)
        payload = data[offset + 1:offset + 1 + length]
        return ("ext", ext_type, payload), offset + 1 + length

    def _read_array(self, data, offset, length):
        items = []
        for _ in range(length):
            item, offset = self._unpack_one(data, offset)
            items.append(item)
        return items, offset

    def _read_map(self, data, offset, length):
        result = {}
        for _ in range(length):
            key, offset = self._unpack_one(data, offset)
            value, offset = self._unpack_one(data, offset)
            result[key] = value
        return result, offset


class NvimRpc:
    def __init__(self, proc):
        self.proc = proc
        self.codec = MsgpackCodec()
        self.next_msgid = 1
        self.pending = {}
        self.buffer = b""

    def request(self, method, params=None):
        msgid = self.next_msgid
        self.next_msgid += 1
        payload = [0, msgid, method, params or []]
        self.proc.stdin.write(self.codec.pack(payload))
        self.proc.stdin.flush()
        return msgid

    def notify(self, method, params=None):
        payload = [2, method, params or []]
        self.proc.stdin.write(self.codec.pack(payload))
        self.proc.stdin.flush()

    def read_available(self, timeout):
        fd = self.proc.stdout.fileno()
        ready, _, _ = select.select([fd], [], [], timeout)
        if not ready:
            return []
        chunk = os.read(fd, 65536)
        if not chunk:
            return []
        self.buffer += chunk
        messages, self.buffer = self.codec.unpack_stream(self.buffer)
        return messages


class Grid:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.rows = [[" "] * width for _ in range(height)]
        self.cursor = (0, 0)

    def resize(self, width, height):
        self.width = width
        self.height = height
        self.rows = [[" "] * width for _ in range(height)]

    def clear(self):
        self.rows = [[" "] * self.width for _ in range(self.height)]

    def line(self, row, col_start, cells):
        if row < 0 or row >= self.height:
            return
        col = col_start
        for cell in cells:
            if not cell:
                continue
            text = str(cell[0])
            repeat = 1
            if len(cell) >= 3 and isinstance(cell[2], int):
                repeat = cell[2]
            for _ in range(repeat):
                if 0 <= col < self.width:
                    self.rows[row][col] = text[:1] if text else " "
                col += max(1, len(text))

    def scroll(self, top, bot, left, right, rows, cols):
        if cols != 0 or left != 0 or right != self.width:
            self.clear()
            return
        top = max(0, top)
        bot = min(self.height, bot)
        region = self.rows[top:bot]
        if rows > 0:
            region = region[rows:] + [[" "] * self.width for _ in range(rows)]
        elif rows < 0:
            region = [[" "] * self.width for _ in range(-rows)] + region[:rows]
        self.rows[top:bot] = region[: bot - top]

    def text(self):
        return "\n".join("".join(row).rstrip() for row in self.rows).rstrip() + "\n"


class NvimSurfaceProbe:
    def __init__(self, width, height):
        self.grid = Grid(width, height)
        self.notifications = 0
        self.responses = {}

    def handle(self, message):
        if not isinstance(message, list) or not message:
            return
        kind = message[0]
        if kind == 1 and len(message) >= 4:
            self.responses[message[1]] = (message[2], message[3])
            return
        if kind != 2 or len(message) < 3:
            return
        method = message[1]
        params = message[2]
        if method != "redraw":
            return
        self.notifications += 1
        for event in params:
            self._handle_redraw_event(event)

    def _handle_redraw_event(self, event):
        if not event:
            return
        name = event[0]
        for update in event[1:]:
            self._dispatch(name, update if isinstance(update, list) else [update])

    def _dispatch(self, name, args):
        if name == "grid_resize" and len(args) >= 3:
            self.grid.resize(args[1], args[2])
        elif name == "grid_line" and len(args) >= 5:
            self.grid.line(args[1], args[2], args[3])
        elif name == "grid_cursor_goto" and len(args) >= 3:
            self.grid.cursor = (args[1], args[2])
        elif name == "grid_scroll" and len(args) >= 7:
            self.grid.scroll(args[1], args[2], args[3], args[4], args[5], args[6])
        elif name in {"grid_clear", "clear"}:
            self.grid.clear()


def expand_input(text):
    replacements = {
        "<Esc>": "\x1b",
        "<CR>": "\r",
        "<Tab>": "\t",
        "<BS>": "\x7f",
    }
    for key, value in replacements.items():
        text = text.replace(key, value)
    return text


def wait_for_response(rpc, surface, msgid, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        for message in rpc.read_available(0.05):
            surface.handle(message)
        if msgid in surface.responses:
            error, result = surface.responses[msgid]
            if error:
                raise RuntimeError(f"nvim request failed: {error}")
            return result
    raise TimeoutError(f"timed out waiting for nvim response {msgid}")


def pump(rpc, surface, duration):
    deadline = time.time() + duration
    while time.time() < deadline:
        for message in rpc.read_available(0.05):
            surface.handle(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--file", default=None)
    parser.add_argument("--width", type=int, default=100)
    parser.add_argument("--height", type=int, default=32)
    parser.add_argument("--input", default="")
    parser.add_argument("--snapshot", default="")
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    cmd = ["nvim", "--embed", "-n"]
    if args.file:
        cmd.append(args.file)

    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    rpc = NvimRpc(proc)
    surface = NvimSurfaceProbe(args.width, args.height)

    try:
        attach_id = rpc.request("nvim_ui_attach", [
            args.width,
            args.height,
            {
                "rgb": True,
                "ext_linegrid": True,
            },
        ])
        wait_for_response(rpc, surface, attach_id, args.timeout)
        redraw_id = rpc.request("nvim_command", ["redraw!"])
        wait_for_response(rpc, surface, redraw_id, args.timeout)
        pump(rpc, surface, 0.25)

        if args.input:
            input_id = rpc.request("nvim_input", [expand_input(args.input)])
            wait_for_response(rpc, surface, input_id, args.timeout)
            pump(rpc, surface, 0.5)

        if args.snapshot:
            with open(args.snapshot, "w", encoding="utf-8") as f:
                f.write(surface.grid.text())
        else:
            sys.stdout.write(surface.grid.text())

        if proc.poll() is None:
            rpc.request("nvim_command", ["qa!"])
            pump(rpc, surface, 0.1)
        return 0
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
