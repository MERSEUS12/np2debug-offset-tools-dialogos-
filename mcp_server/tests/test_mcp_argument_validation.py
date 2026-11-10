import json
import unittest
from unittest.mock import patch

import server


class McpArgumentValidationTests(unittest.TestCase):
    def assert_rejected_before_ipc(self, function, *args, **kwargs):
        with patch.object(server, "_send_command") as send_command:
            with self.assertRaises(ValueError):
                function(*args, **kwargs)
            send_command.assert_not_called()

    def test_memory_read_rejects_non_integer_and_unsupported_sizes(self):
        for size in (True, False, 1.5, "1", None, 0, 3, 8, -1):
            with self.subTest(size=size):
                self.assert_rejected_before_ipc(server.memory_read, 0x1000, size)

    def test_memory_read_block_rejects_non_integer_and_out_of_range_lengths(self):
        for length in (True, False, 1.5, "1", None, 0, 257, -1):
            with self.subTest(length=length):
                self.assert_rejected_before_ipc(
                    server.memory_read_block, 0x1000, length
                )

    def test_fast_forward_arm_rejects_invalid_wait_kind(self):
        for wait_kind in (True, False, 1.5, "1", None, -1, 3):
            with self.subTest(wait_kind=wait_kind):
                self.assert_rejected_before_ipc(
                    server.fast_forward_arm, 0x1000, 1, wait_kind
                )

    def test_memory_read_accepts_only_supported_sizes_and_masks_value(self):
        response = json.dumps({
            "command_id": 1,
            "result": 1,
            "payload_hex": "FFFFFFFF",
            "payload_length": 4,
        })
        for size, expected in ((1, 0xFF), (2, 0xFFFF), (4, 0xFFFFFFFF)):
            with self.subTest(size=size):
                with patch.object(server, "_send_command", return_value=response) as send_command:
                    result = json.loads(server.memory_read(0x1234, size, "physical"))
                send_command.assert_called_once_with(
                    server.CMD_MEMORY_READ, 0x1234, size, 0
                )
                self.assertEqual(result["value"], expected)

    def test_memory_read_block_accepts_length_boundaries(self):
        for length in (1, 256):
            with self.subTest(length=length):
                payload = bytes((index & 0xFF) for index in range(length))
                response = json.dumps({
                    "command_id": 1,
                    "result": 1,
                    "payload_hex": payload.hex().upper(),
                    "payload_length": length,
                })
                with patch.object(server, "_send_command", return_value=response) as send_command:
                    result = json.loads(
                        server.memory_read_block(0x2000, length, "physical")
                    )
                send_command.assert_called_once_with(
                    server.CMD_MEMORY_READ_BLOCK, 0x2000, length, 0
                )
                self.assertEqual(result["length"], length)
                self.assertEqual(result["bytes"], list(payload))

    def test_fast_forward_arm_accepts_all_documented_wait_kinds(self):
        with patch.object(server, "_send_command", return_value="{}") as send_command:
            for wait_kind in (0, 1, 2):
                with self.subTest(wait_kind=wait_kind):
                    server.fast_forward_arm(0x1000, 5, wait_kind)
                    self.assertEqual(
                        send_command.call_args.args,
                        (server.CMD_SKIP_ARM, 0x1000, 5, wait_kind),
                    )

    def test_memory_reads_reject_address_range_overflow_before_ipc(self):
        self.assert_rejected_before_ipc(server.memory_read, 0xFFFFFFFF, 2)
        self.assert_rejected_before_ipc(
            server.memory_read_block, 0xFFFFFF01, 256
        )


if __name__ == "__main__":
    unittest.main()
