# V4 Evaluator — Status

## Gate verdict

Experimental only. `certified=false` until full identity oracles pass on production decks.

## P0e (this push)

| Item | Status |
|------|--------|
| Per-card `CandidateCoverageProof` / `proveCandidate` | done |
| Scanners: pending / global / costs / selection / conditions | done |
| Condition effects included in operator footprints | done |
| Dropped `closure.complete()` as Passive integral gate | done |
| Nested chance = pending-skill remainder (not all Main Items) | done |
| Fallback metrics by coverage category | done |
| Damage-only operators do not block Passive energy | done (diagnostics) |
| Mini-deck Passive ON/OFF identity oracle | added (skips if setup fails) |
| seed4 90s certified | still Phase-6 |

`closure.complete()` remains a diagnostic aggregate. Passive classification uses `proveCandidate` + `PassiveProofV4`.
