# V4 Evaluator — Status

## Gate verdict

**V4 Phase 4 experimental path: usable for single Rich Energy multi-draw trials.**  
**Not Exact-certified for general search with nested draws / extra chances.**

Default remains V3. `PTCG_EXACT_V4_PASSIVE_DRAW` is opt-in and forces `certified=false`.

## Latest P0b fixes (nested / zone / binding)

| Item | Status |
|------|--------|
| Nested-chance safety: no Passive integral if further Draw/TakePrize/zone move reachable | done |
| Exclude current pending skill card from “further” check | done |
| Residual add only when this chance has `passiveIntegrated` takes | done |
| `BuildFromV3`: OwnHand-only strip (deck/prize/combo kept) | done |
| `partitionSchemaHash` = StableHash(schema) | done |
| Unknown token index = -1 (no token-0 alias) | done |
| Proven bounds recomputed; no fake clamp rounding | done |
| Schema safety distinguishes V3 vs V4 ModelSchemaVersion | done |
| Ultra Ball discard-cost + used-supporter Active counterexample | done |
| `PassiveMomentStateV4` (leaf-once residual across nests) | **not yet** |
| Representative physical-draw posterior / symbolic deck | **not yet** |
| ComboProbability representative invariance (full feature) | partial |
| End-to-end identity oracle | **not yet** |

## How to run (experimental only)

```bat
set PTCG_EXACT_EVALUATOR_VERSION=V3
set PTCG_EXACT_V4_PASSIVE_DRAW=1
```

Optional: `ExactEvaluatorV3File` + `ExactEvaluatorV4File`.

Bootstrap models are not adoption models.
