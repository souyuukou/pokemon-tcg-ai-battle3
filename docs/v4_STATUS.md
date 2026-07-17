# V4 Evaluator — Status

## Gate verdict

Experimental only. `certified=false` until coverage scanners + oracles pass.

## P0d (this push)

| Item | Status |
|------|--------|
| `passiveSemanticInvariant`: explicit base+drawn Passive, no empty-closure reclassify | done |
| Check take = 1..min(drawCount, class.count); heavy class ⇒ Active fallback | done |
| Full FeatureRecord serialization (`SerializeSemanticFeatures`) | done |
| Removed fake `SealCoverageForTerminalChance` true-stamping | done |
| V4 search leaf path uses unclamped eval uniformly | done |
| `prePartitionProofHash` from dependency/reachable/model (not 0) | done |
| Real coverage scanners (pending/global/costs/selection/conditions) | **not yet** |
| Full identity e2e oracle | **not yet** |

Without coverage scanners, `OperatorClosure::complete()` is rarely true when reachable operators exist, so Passive integral stays fail-closed — intentional.
