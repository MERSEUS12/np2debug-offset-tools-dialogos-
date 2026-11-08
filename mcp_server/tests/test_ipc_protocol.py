import struct
import unittest

import server


class IpcProtocolTests(unittest.TestCase):
    def test_request_size_matches_packed_c_struct(self):
        self.assertEqual(server.REQUEST.size, 320)

    def test_response_size_matches_packed_c_struct(self):
        self.assertEqual(server.RESPONSE.size, 280)

    def test_response_result_is_signed(self):
        packed = server.RESPONSE.pack(
            server.MAGIC, server.VERSION, 0, 7, -1, 0, bytes(256)
        )
        magic, version, status, command_id, result, payload_length, payload = (
            server.RESPONSE.unpack(packed)
        )
        self.assertEqual((magic, version, status, command_id), (
            server.MAGIC, server.VERSION, 0, 7
        ))
        self.assertEqual(result, -1)
        self.assertEqual(payload_length, 0)
        self.assertEqual(len(payload), 256)

    def test_block_response_payload_round_trip(self):
        data = bytes(range(256))
        packed = server.RESPONSE.pack(
            server.MAGIC, server.VERSION, 0, 9, 1, len(data), data
        )
        unpacked = server.RESPONSE.unpack(packed)
        self.assertEqual(unpacked[4], 1)
        self.assertEqual(unpacked[5], 256)
        self.assertEqual(unpacked[6], data)

    def test_scalar_memory_payload_preserves_unsigned_32_bit_values(self):
        for value in (0x7FFFFFFF, 0x80000000, 0xFFFFFFFF):
            data = value.to_bytes(4, "little", signed=False)
            packed = server.RESPONSE.pack(
                server.MAGIC, server.VERSION, 0, 11, 1, 4, data + bytes(252)
            )
            unpacked = server.RESPONSE.unpack(packed)
            self.assertEqual(unpacked[4], 1)
            self.assertEqual(unpacked[5], 4)
            decoded = int.from_bytes(unpacked[6][:4], "little", signed=False)
            self.assertEqual(decoded, value)

    def test_dialogue_payload_layout(self):
        payload = struct.pack("<IiI", 0x12345678, 7, 0x90) + b"Hello\x00"
        esi, dialogue_id, block_start = struct.unpack_from("<IiI", payload, 0)
        self.assertEqual((esi, dialogue_id, block_start), (0x12345678, 7, 0x90))
        self.assertEqual(payload[12:-1].decode("ascii"), "Hello")
        self.assertEqual(payload[-1], 0)

    def test_command_ids_and_protocol_version(self):
        self.assertEqual(server.VERSION, 2)
        self.assertEqual(server.CMD_MEMORY_READ, 17)
        self.assertEqual(server.CMD_MEMORY_READ_BLOCK, 18)
        self.assertEqual(server.CMD_DIALOGUE_CURRENT, 19)


if __name__ == "__main__":
    unittest.main()
