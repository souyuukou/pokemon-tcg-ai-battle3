# pokemon-tcg-ai-battle3

Exact, information-safe turn search for the Pokémon TCG competition engine.

The implementation lives in `sample_submission/sample_submission/exact_solver`.
Deck-specific policy and evaluator settings live under `decks/` and are selected
with the `PTCG_DECK_PROFILE` environment variable.  Large replay corpora in
`data/` are intentionally not versioned.

Run the tests with:

```powershell
python -m pytest tests
```

