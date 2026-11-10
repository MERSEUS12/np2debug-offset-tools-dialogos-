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

    def test_scene_history_record_layout(self):
        name = b"SCENE01.BIN".ljust(16, b"\0")
        record = struct.pack("<16sIBHH16s", name, 0x12345678, 1,
                             0x2345, 0x6789, bytes(range(16)))
        self.assertEqual(len(record), 41)
        decoded = struct.unpack("<16sIBHH16s", record)
        self.assertEqual(decoded[0].split(b"\0", 1)[0], b"SCENE01.BIN")
        self.assertEqual(decoded[1:5], (0x12345678, 1, 0x2345, 0x6789))
        self.assertEqual(decoded[5], bytes(range(16)))

    def test_jump_trace_record_layout(self):
        record=struct.pack("<HIII",0x1234,0x89ABCDEF,0x10203040,0x50607080)
        self.assertEqual(len(record),14)
        self.assertEqual(struct.unpack("<HIII",record),(0x1234,0x89ABCDEF,0x10203040,0x50607080))
        payload=struct.pack("<I",1)+record
        self.assertEqual(struct.unpack_from("<I",payload,0)[0],1)
        self.assertEqual(struct.unpack_from("<HIII",payload,4),(0x1234,0x89ABCDEF,0x10203040,0x50607080))

    def test_current_instruction_payload_layout(self):
        payload = struct.pack("<HHIIB15s24s44s",
                              0x1234, 0x5678, 0x89ABCDEF, 0x10203040,
                              3, b"\x90\x90\xC3".ljust(15, b"\0"),
                              b"ret".ljust(24, b"\0"),
                              b"".ljust(44, b"\0"))
        self.assertEqual(len(payload), 96)
        cs, ip, eip, linear, length = struct.unpack_from("<HHIIB", payload)
        self.assertEqual((cs, ip, eip, linear, length),
                         (0x1234, 0x5678, 0x89ABCDEF, 0x10203040, 3))
        self.assertEqual(payload[13:16], b"\x90\x90\xC3")
        self.assertEqual(payload[28:52].split(b"\0", 1)[0], b"ret")

    def test_cpu_register_payload_layout(self):
        values = (1, 2, 3, 4, 5, 6, 7, 8,
                  0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
                  0x12345678, 0x202, 1)
        payload = struct.pack("<8I6HIIB", *values)
        self.assertEqual(len(payload), 53)
        self.assertEqual(struct.unpack("<8I6HIIB", payload), values)

    def test_command_ids_and_protocol_version(self):
        self.assertEqual(server.VERSION, 2)
        self.assertEqual(server.NP2DN_IPC_TIMEOUT_STATUS, 4)
        self.assertEqual(server.CMD_MEMORY_READ, 17)
        self.assertEqual(server.CMD_MEMORY_READ_BLOCK, 18)
        self.assertEqual(server.CMD_DIALOGUE_CURRENT, 19)
        self.assertEqual(server.CMD_SCENE_HISTORY, 20)
        self.assertEqual(server.CMD_JUMP_TRACE, 21)
        self.assertEqual(server.CMD_CURRENT_INSTRUCTION, 22)
        self.assertEqual(server.CMD_CPU_REGISTERS, 23)
        self.assertEqual(server.CMD_BREAKPOINTS_LIST, 24)

    def test_breakpoint_list_payload_layout(self):
        entries = [(0x12345, 0x04), (0x23456, 0x0B)]
        records = b"".join(struct.pack("<IB", address, flags)
                           for address, flags in entries)
        payload = struct.pack("<III", 2, 0, len(entries)) + records
        self.assertEqual(len(records), 10)
        self.assertEqual(len(payload), 22)
        self.assertEqual(struct.unpack_from("<III", payload, 0), (2, 0, 2))
        self.assertEqual(struct.unpack_from("<IB", payload, 12), entries[0])
        self.assertEqual(struct.unpack_from("<IB", payload, 17), entries[1])

    def test_breakpoint_page_max_fits_ipc_payload(self):
        self.assertEqual(12 + 48 * 5, 252)
        self.assertLessEqual(12 + 48 * 5, 256)


if __name__ == "__main__":
    unittest.main()
