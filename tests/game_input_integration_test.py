#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""End-to-end TCP smoke test for fragmented and coalesced game controls.

The parser unit test verifies byte-stream edge cases in isolation. This test
starts from a real game session, sends terminal key bytes over TCP, and checks
that the server responds with redraw output instead of silently ignoring them.
"""

import argparse
import asyncio

from play_test import (
    AsyncConn,
    KEY_LEFT,
    KEY_RIGHT,
    PWD_FMT,
    USER_FMT,
    login_and_start,
)


async def discard_until_quiet(reader, idle_timeout=0.05):
    """Drop the remaining initial game-screen bytes before measuring a key."""
    while True:
        try:
            chunk = await asyncio.wait_for(reader.read(65536), timeout=idle_timeout)
        except asyncio.TimeoutError:
            return
        if not chunk:
            raise AssertionError("server closed the TCP connection after game start")


async def read_response(reader, label, timeout=0.6, idle_timeout=0.05):
    """Read one response and collect all chunks until the stream becomes idle."""
    try:
        first = await asyncio.wait_for(reader.read(65536), timeout=timeout)
    except asyncio.TimeoutError as exc:
        raise AssertionError(f"no server redraw after {label}") from exc
    if not first:
        raise AssertionError(f"server closed the TCP connection after {label}")

    chunks = [first]
    while True:
        try:
            chunk = await asyncio.wait_for(reader.read(65536), timeout=idle_timeout)
        except asyncio.TimeoutError:
            break
        if not chunk:
            raise AssertionError(f"server closed the TCP connection after {label}")
        chunks.append(chunk)
    return b"".join(chunks)


async def run_test(host, port, timeout):
    reader, writer = await asyncio.wait_for(
        asyncio.open_connection(host, port), timeout=timeout
    )
    try:
        conn = AsyncConn(reader)
        username = USER_FMT % 0
        password = PWD_FMT % 0
        if not await login_and_start(conn, writer, username, password, 1, timeout):
            raise AssertionError("could not log in and enter the easy game mode")

        # login_and_start keeps bytes after the Score marker in its own buffer.
        # Discard those and anything still queued on the socket so key responses
        # are measured independently from the initial board rendering.
        conn.buf.clear()
        conn.mark = 0
        await discard_until_quiet(reader)

        # Force the three-byte CSI left-arrow sequence across separate writes.
        # The short gaps let the server's persistent read event observe prefixes
        # before the final byte arrives.
        for part in (b"\x1b", b"[", b"D"):
            writer.write(part)
            await writer.drain()
            await asyncio.sleep(0.04)

        fragmented = await read_response(reader, "fragmented ESC [ D")
        fragmented_csi = fragmented.count(b"\x1b[")
        if fragmented_csi < 8:
            raise AssertionError(
                "fragmented left-arrow did not produce a game redraw "
                f"(only {fragmented_csi} ANSI sequences)"
            )

        # A single write carries many commands. In the loopback CI environment,
        # recv normally obtains this as one coalesced chunk; the command parser
        # must still execute the complete ordered batch.
        burst = (KEY_LEFT + KEY_RIGHT) * 8
        writer.write(burst)
        await writer.drain()
        coalesced = await read_response(reader, "coalesced left/right burst")
        coalesced_csi = coalesced.count(b"\x1b[")
        if coalesced_csi < 128:
            raise AssertionError(
                "coalesced game controls did not all produce redraw output "
                f"(only {coalesced_csi} ANSI sequences)"
            )

        print(
            "PASS: fragmented arrow produced "
            f"{fragmented_csi} ANSI sequences; coalesced 16-key burst produced "
            f"{coalesced_csi}; TCP session remained active"
        )
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser(description="Game input TCP integration smoke test")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=10010)
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()
    asyncio.run(run_test(args.host, args.port, args.timeout))


if __name__ == "__main__":
    main()
