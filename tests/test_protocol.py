"""Independent wire-protocol checks for a running ThreadRunners server.

Uses Python's hashlib as an oracle; the C server must use lonesha256.
Run: python3 tests/test_protocol.py --host 192.168.101.10 --port 5003
"""

import argparse
import hashlib
import socket
import struct
import sys
import time
import unittest

HOST = '192.168.101.10'
PORT = 5003


def make_request(number, start, end, priority=7):
    digest = hashlib.sha256(struct.pack('<Q', number)).digest()
    return digest + struct.pack('>QQB', start, end, priority)


class ProtocolTests(unittest.TestCase):
    def connect(self):
        return socket.create_connection((HOST, PORT), timeout=5)

    def assert_answer(self, number, start, end, priority=7, fragmented=False):
        request = make_request(number, start, end, priority)
        self.assertEqual(len(request), 49)
        with self.connect() as connection:
            if fragmented:
                for part in (request[:13], request[13:37], request[37:]):
                    connection.sendall(part)
                    time.sleep(0.02)
            else:
                connection.sendall(request)
            response = b''
            while len(response) < 8:
                part = connection.recv(8 - len(response))
                self.assertTrue(part, 'Server closed before sending eight answer bytes')
                response += part
            self.assertEqual(struct.unpack('>Q', response)[0], number)
            self.assertEqual(connection.recv(1), b'', 'Expected closure after one answer')

    def assert_no_answer_then_recovery(self, number, start, end):
        with self.connect() as connection:
            connection.sendall(make_request(number, start, end))
            self.assertEqual(connection.recv(1), b'', 'No valid answer exists in this range')
        self.assert_answer(263, 256, 266)

    def test_start_is_included(self):
        self.assert_answer(256, 256, 266)

    def test_last_candidate_is_included(self):
        self.assert_answer(265, 256, 266)

    def test_single_candidate_range(self):
        self.assert_answer(263, 263, 264)

    def test_zero_is_a_valid_answer(self):
        self.assert_answer(0, 0, 1)

    def test_large_unsigned_values(self):
        start = (1 << 63) + 256
        self.assert_answer(start + 7, start, start + 10)

    def test_near_uint64_max(self):
        end = (1 << 64) - 1
        self.assert_answer(end - 1, end - 4, end)

    def test_fragmented_request(self):
        self.assert_answer(263, 256, 266, fragmented=True)

    def test_repeated_connections_and_priorities(self):
        for index in range(12):
            with self.subTest(connection=index):
                self.assert_answer(263, 256, 266, priority=(index % 16) + 1)

    def test_end_is_excluded_and_server_recovers(self):
        self.assert_no_answer_then_recovery(266, 256, 266)

    def test_empty_range_and_server_recovers(self):
        self.assert_no_answer_then_recovery(256, 256, 256)

    def test_reversed_range_and_server_recovers(self):
        self.assert_no_answer_then_recovery(263, 266, 256)

    def test_incomplete_request_and_server_recovers(self):
        packet = make_request(263, 256, 266)
        for length in (0, 1, 32, 48):
            with self.subTest(request_bytes=length):
                with self.connect() as connection:
                    connection.sendall(packet[:length])
                    connection.shutdown(socket.SHUT_WR)
                    self.assertEqual(connection.recv(1), b'')
                self.assert_answer(263, 256, 266)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default=HOST)
    parser.add_argument('--port', type=int, default=PORT)
    args, remaining = parser.parse_known_args()
    HOST, PORT = args.host, args.port
    unittest.main(argv=[sys.argv[0], *remaining], verbosity=2)
