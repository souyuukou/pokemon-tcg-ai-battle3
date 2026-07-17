# V4 Evaluator — Status

## Current gate (P0 Exactness)

**V4 is not production.** Default search remains V3. `PTCG_EXACT_V4_PASSIVE_DRAW` is opt-in only and forces `certified=false` until oracles pass.

### P0 progress

| Item | Status |
|------|--------|
| Stage1: no auto-enable Passive draw from V4 version | done |
| Stage1: V4 / experimental draw → `certified=false` | done |
| P0-1: `PassiveProofV4` + mandatory `OperatorClosure` | done |
| P0-2: Passive pools per source continuation class | done |
| P0-3: `FeatureRecordV4` strip of Passive identity features | started |
| P0-4: `basePassive` + `ExpectedPassiveResidualWithBase` / multi-pool | done |
| P0-5: V4.0 context-free residual only | done |
| P0-6: `provenMin/MaxOutput` clamp gate in V4 header | done (schema 2) |
| P0-7: explicit V3+V4 paths + hash / checksum fields | done (schema 2) |
| Oracle suite (liveness counterexamples, e2e, bit-exact) | partial |

### Adoption checklist (must all pass)

- [ ] Liveness counterexample tests
- [ ] Deck-removal invariant tests
- [ ] Representative-card invariance
- [ ] Base×draw cross-pair oracle
- [ ] Clamp boundary
- [ ] Mini end-to-end enumeration
- [ ] Python/C++ bit match ≥10k
- [ ] required V3 hash enforcement
- [ ] chanceMassMismatches = 0
- [ ] seed4 certified + ≤90s (after Exactness)

## How to run (experimental only)

```bat
set PTCG_EXACT_EVALUATOR_VERSION=V3
rem Passive draw stays off unless:
set PTCG_EXACT_V4_PASSIVE_DRAW=1
rem Optional explicit dual files:
set ExactEvaluatorV3File=...\exact-evaluator-v3.bin
set ExactEvaluatorV4File=...\exact-evaluator-v4.bin
```

Bootstrap V4 (`tools/bootstrap_evaluator_v4.py`) is **not** an adoption model — outcome / distillation / ranking / QAT training is still required.
