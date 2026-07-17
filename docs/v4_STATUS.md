# V4 Evaluator — Status

## Completed (this branch)

| Item | Status |
|------|--------|
| V4 model format (`PTCGEV4`) + bootstrap from V3 | done (`nnue_v4.py`, `exact-evaluator-v4.bin`) |
| Python integer Passive residual | done |
| C++ V4 Dual / V4 search path | done (`ExactSparseEvaluatorV4`, `ExactCpuEvaluator`) |
| Liveness classifier (conservative) | done (`ExactCardLivenessV4`) |
| Passive E[X], E[X_i X_j], E[C(X,2)] ExactFraction | done + pytest |
| Rich Passive integral (env-gated) | done (`PTCG_EXACT_V4_PASSIVE_DRAW=1`) |
| V3 kept + fallback | done |
| Seed6 / skeleton regression | passing |
| Seed4 outcome compression vs 9184 | **~678** Active outcomes (Passive integrated) |
| Seed4 ≤90s full certification | **not yet** (~10 min estimated at current node rate) |

## How to run V4 Passive draw

```bat
set PTCG_EXACT_EVALUATOR_VERSION=V4
set PTCG_EXACT_V4_PASSIVE_DRAW=1
```

Load `exact-evaluator-v3.bin` (V4 residual bootstraps from V3 OwnHand weights in-process). Optional: `exact-evaluator-v4.bin` via `tools/bootstrap_evaluator_v4.py`.

## Seed4 gap (honest)

Item-heavy Active hands still expand ~100k–600k nodes **per** Active multiset. Passive removes Energy / Stage / turn-locked Supporters / Rare Candy, cutting 9184 → ~678, but 90s needs either:

1. Further Transition-identity merging / Main-DAG speedups, or  
2. Shallower certified scopes (not Exact evaluator expectation).

Gate test: `tests/test_v4_seed4_rich_certification.py::test_seed4_v4_passive_draw_compresses_and_keeps_exact_mass`

## Phase checklist (spec §18)

- [x] V4 model format  
- [x] Python bootstrap training entry  
- [x] Python integer reference (Passive)  
- [x] C++ integer inference  
- [x] Liveness + Passive expectation oracles  
- [x] Mass-safe Rich Passive path  
- [ ] Seed4 certified ≤90s / RSS&lt;3GB (perf remaining)  
- [ ] Full ranking/outcome gates + multi-deck battles  
- [x] V3 fallback  
- [x] Docs (this file + baselines)

## Baselines

See `docs/v4_baselines/` for Phase-0 V3 snapshots.
