# V4 Evaluator — Status

## Gate verdict

Experimental only. `v4PassiveDrawCertified=false` until full identity oracles pass.

## P0g (this push)

| Item | Status |
|------|--------|
| Exclude only current Draw Effect via `OperatorSourceKey` (not whole cardId) | done |
| `Draw()` records `pendingSkillId` / `pendingEffectIndex` | done |
| `functionStack`: Opaque ⇒ unknown+furtherChance; EffectControl whitelist (`AfterEffect` / `ActivateSkillEffect` / …) allowed | done (TypedDeferredFunction skeleton) |
| `ExactCardLivenessV4SchemaVersion()` C API + tests | done |
| Candidate-aware global/selection scanners | done |
| Explicit `selectionSafe` / `conditionSafe` metrics | done |
| Full e2e identity oracle | **not yet** |
| Full `TypedDeferredFunction` coverage on every callback | **not yet** |
| `PassiveMomentStateV4` | **not yet** |
| seed4 90s certified | still Phase-6 |
