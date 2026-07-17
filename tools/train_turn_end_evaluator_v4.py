#!/usr/bin/env python3
"""V4 turn-end evaluator training entry (distill/bootstrap path).

Full ranking/outcome QAT remains iterative; this script bootstraps a V4 residual
table from V3 and optionally fine-tunes Passive biases on Passive-difference pairs.
"""
from __future__ import annotations

import argparse
from pathlib import Path

from exact_solver.nnue_v3 import load_quantized
from exact_solver.nnue_v4 import bootstrap_from_v3, export_quantized


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--v3", type=Path, required=True, help="Path to exact-evaluator-v3.bin")
    parser.add_argument("--out", type=Path, required=True, help="Output exact-evaluator-v4.bin")
    args = parser.parse_args()
    v3 = load_quantized(args.v3)
    v4 = bootstrap_from_v3(v3)
    export_quantized(args.out, v4)
    nonzero = int((v4.passive_bias != 0).sum())
    print(f"bootstrapped V4 -> {args.out} tokens={len(v4.tokens)} nonzero_bias={nonzero}")
    print("Next: generate Passive-difference datasets and run QAT (see docs/v4_baselines/README.md).")


if __name__ == "__main__":
    main()
