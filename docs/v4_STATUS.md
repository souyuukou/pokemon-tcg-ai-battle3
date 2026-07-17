# V4 Evaluator — Status

## Gate verdict

Experimental only. `certified=false` until full identity oracles + safe residual bounds.

## P0f (this push)

| Item | Status |
|------|--------|
| `AnyReachableFurtherChance` disables Passive integral (nested double-count guard) | done |
| Attack / delay footprints in operator closure | done |
| Global scan: tool / energy / trigger stacks; attacks only for active player | done |
| Selection scan uses real `options` + lists (not trusted `selectContext`) | done |
| `targetMayMatchCandidate` per-card footprint filter | done |
| Artificial deck asserts `passiveCardsIntegrated > 0` | done |
| Residual timer + outcome-weight `richPassiveIntegratedWeight` | done |
| `PassiveMomentStateV4` (leaf-once residual) | **not yet** |
| seed4 90s certified | still Phase-6 |

Bootstrap V3→V4 models may fail `analyticIntegralSafe()` (60-copy residual bound). Experimental Passive draw still integrates; certification stays false.
