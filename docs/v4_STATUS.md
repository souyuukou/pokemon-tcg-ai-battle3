# V4 Evaluator — Status

## Gate verdict

**Experimental path only.** Not Exact-certified for general nested search.

## P0c (this push)

| Item | Status |
|------|--------|
| Passive uses `transitionContinuationKey` (no V3 signature) before partition | done |
| Active keeps full `continuationIdentityKey` (V3 signature) | done |
| Classify → then partition (not signature-first singleton) | done |
| Representative Semantic invariance guard | done |
| OperatorClosure coverage flags + `complete()` | done |
| Terminal chance seals coverage when no further draw/zone | provisional |
| Clamp: `evaluateV4ExactUnclamped` for strip path; bounds use 60 copies | done |
| `PassiveMomentStateV4` | not yet |
| Full V4 Semantic retrain without Passive hidden IDs | not yet |
| End-to-end identity oracle | not yet |

Default: V3. `PTCG_EXACT_V4_PASSIVE_DRAW=1` stays uncertified.
