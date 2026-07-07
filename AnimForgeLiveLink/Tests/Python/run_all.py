"""Runs the whole AnimForgeLiveLink Python test suite.

Usage:  python run_all.py
Also works under mayapy for in-DCC validation.
"""

import os
import sys
import unittest


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    suite = unittest.defaultTestLoader.discover(here, pattern="test_*.py")
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
