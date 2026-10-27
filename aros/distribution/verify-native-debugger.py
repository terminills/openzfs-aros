#!/usr/bin/env python3
"""Shared native-debugger acceptance gate for both bundle builders."""
import argparse
import importlib.util
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('run', type=Path)
parser.add_argument('launcher', type=Path)
parser.add_argument('engine', type=Path)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('stage', Path(__file__).with_name('stage-mmake.py'))
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)
stage.require_native_debugger(args.run.resolve(strict=True),
                              args.launcher.resolve(strict=True),
                              args.engine.resolve(strict=True))
print('PASS native debugger runtime evidence matches selected launcher and shared engine')
