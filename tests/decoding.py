"""Decoder matrix and configuration contracts."""

import copy
import unittest

from qsbit_backend.decoding import create, validate


class Decoding(unittest.TestCase):
    def setUp(self):
        self.decoder = {
            "id": 0,
            "measurements": 2,
            "outputs": 3,
            "latency": 100,
            "initiation_interval": 20,
            "backend": "pymatching",
            "options": {
                "check_matrix": [[1, 1, 0], [0, 1, 1]],
                "observables": [[1, 0, 0], [0, 1, 0], [0, 0, 1]],
                "measurement_to_detector": [[1, 0], [0, 1]],
                "weights": [1, 1, 1],
            },
        }

    def test_corrections(self):
        decode = create(self.decoder)
        for syndrome, mask in [
            ([0, 0], [0, 0, 0]),
            ([1, 0], [1, 0, 0]),
            ([1, 1], [0, 1, 0]),
            ([0, 1], [0, 0, 1]),
        ]:
            self.assertEqual(decode(syndrome), mask)
        with self.assertRaises(ValueError):
            decode([1])

    def test_matrices(self):
        for key, value in [
            ("check_matrix", [[2, 0]]),
            ("observables", [[1]]),
            ("measurement_to_detector", [[1, 0, 1]]),
            ("weights", [float("nan")] * 3),
        ]:
            invalid = copy.deepcopy(self.decoder)
            invalid["options"][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                create(invalid)

    def test_transport_config(self):
        config = {
            "mmio_base": 0x40000000,
            "request_capacity": 2,
            "result_capacity": 1,
            "link_latency": 5,
            "bytes_per_tick": 1,
            "packet_overhead": 16,
            "decoders": [self.decoder],
        }
        validate(config)
        for base in (1, 2**32 - 4):
            with self.assertRaises(ValueError):
                validate(dict(config, mmio_base=base))
        with self.assertRaises(ValueError):
            validate(dict(config, decoders=[self.decoder, self.decoder]))


if __name__ == "__main__":
    unittest.main()
