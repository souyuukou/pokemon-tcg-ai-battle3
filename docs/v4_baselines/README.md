# V4 Phase 0 — V3 baselines

Captured before V4 introduction.

## Artifacts

| File | Contents |
|------|----------|
| `exact-evaluator-v3.bin` | Current V3 quantized model |
| `exact-evaluator-v3.report.json` | Training/export report (if present) |
| `pytest_baseline.txt` | Full Windows test suite (42 passed) |
| `seed4_seed6_profile.txt` | seed6 certified + seed4 Rich 5s profile |

## Seed 6 (submission gate)

- certified: true
- value: `14071521832061 / 433160`
- expandedNodes ≈ 7384
- elapsed ≈ 0.04s

## Seed 4 Rich Energy (5s slice)

- certified: false, timedOut
- outcomes ≈ 76 / 9184 (legacy path)
- expandedNodes ≈ 630k–760k
- chanceMassMismatches: 0

## Policy

- Keep V3 code and model until V4 gates pass.
- Search remains on V3 until Phase 6 adoption.
- `PTCG_EXACT_EVALUATOR_VERSION=V3|V4|Dual` (default V3).
- `PTCG_EXACT_V4_PASSIVE_DRAW=1` enables Phase 4 Rich Passive pool integration (default off).

## Implementation status (in progress)

| Phase | Status |
|-------|--------|
| 0 Baselines | done (`docs/v4_baselines/`) |
| 1 Liveness | `ExactCardLivenessV4.h` + `ExactCardLivenessV4Diagnostics` |
| 2 Dual eval | `ExactSparseEvaluatorV4.h` / Dual attach to V3 trunk; search still V3 |
| 3 Passive E[] | `ExactPassiveExpectationV4.h` + `tests/test_v4_passive_expectation.py` |
| 4 Rich Passive draw | env-gated `PTCG_EXACT_V4_PASSIVE_DRAW` (canonical Passive mass; residual weights TBD) |
| 5–6 | not started |
