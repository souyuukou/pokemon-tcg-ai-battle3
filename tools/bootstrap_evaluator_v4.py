#!/usr/bin/env python3
"""Bootstrap a V4 Passive residual table from a V3 quantized model."""
from __future__ import annotations

import argparse
from pathlib import Path

from exact_solver.nnue_v3 import load_quantized
from exact_solver.nnue_v4 import bootstrap_from_v3, export_quantized


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--v3", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    v3 = load_quantized(args.v3)
    v4 = bootstrap_from_v3(v3)
    export_quantized(args.out, v4)
    nonzero = int((v4.passive_bias != 0).sum())
    print(f"wrote {args.out} tokens={len(v4.tokens)} nonzero_passive_bias={nonzero}")


if __name__ == "__main__":
    main()
