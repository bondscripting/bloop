import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "bloop",
    Path(__file__).resolve().parents[1] / "bloop.py",
)
bloop = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bloop)


class TileSizeTests(unittest.TestCase):
    def test_parse_tile_size_accepts_supported_values(self):
        self.assertEqual(bloop.ParseTileSize("8"), 8)
        self.assertEqual(bloop.ParseTileSize("4x4"), 4)
        self.assertEqual(bloop.ParseTileSize("2"), 2)
        self.assertEqual(bloop.ParseTileSize("1x1"), 1)

    def test_parse_tile_size_rejects_invalid_values(self):
        with self.assertRaises(bloop.BloopException):
            bloop.ParseTileSize("16")

        with self.assertRaises(bloop.BloopException):
            bloop.ParseTileSize("3")


class StackTests(unittest.TestCase):
    class FakeChild:
        def __init__(self, color, calls=None):
            self.color = color
            self.calls = calls

        def Probe(self, x, y):
            if self.calls is not None:
                self.calls.append((x, y))
            return self.color

    def test_stack_returns_none_when_no_visible_children(self):
        stack = bloop.Stack({"x": 0, "y": 0, "color": 0xffffffff}, [])
        self.assertIsNone(stack.Probe(0, 0))

    def test_stack_composites_children_bottom_to_top(self):
        stack = bloop.Stack(
            {"x": 0, "y": 0, "color": 0xffffffff},
            [
                self.FakeChild((255, 0, 0, 128)),
                self.FakeChild((0, 255, 0, 128)),
            ],
        )

        self.assertEqual(stack.Probe(0, 0), (85, 170, 0, 192))

    def test_stack_skips_none_children_and_stops_at_full_opacity(self):
        calls = []
        stack = bloop.Stack(
            {"x": 0, "y": 0, "color": 0xffffffff},
            [
                self.FakeChild(None, calls),
                self.FakeChild((255, 0, 0, 255), calls),
                self.FakeChild((0, 255, 0, 64), calls),
            ],
        )

        self.assertEqual(stack.Probe(0, 0), (255, 0, 0, 255))
        self.assertEqual(calls, [(0.0, 0.0), (0.0, 0.0)])

    def test_stack_is_registered_as_a_list_child_composite(self):
        self.assertIn("stack", bloop.CORE_PARSERS)
        self.assertIsInstance(bloop.CORE_PARSERS["stack"], bloop.ObjectParser)


if __name__ == "__main__":
    unittest.main()
