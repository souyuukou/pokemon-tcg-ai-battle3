# Exact turn search

## Semantics

The root is an information state, not a guessed complete game state. A leaf is
the state after end-of-turn effects and Pokémon Checkup, immediately before the
opponent's `TurnStart` and draw. Policies are keyed by the acting player's
information state, so a decision cannot depend on a hidden card identity.

Decision alternatives may be merged only when they induce the same normalized
distribution over next information state, observation, and rule state. Decision
multiplicity is discarded; chance multiplicity is accumulated as an integer.
Draws use products of binomial coefficients and a total of `C(N,k)`. Probabilities
are `fractions.Fraction`; there is no sampling, floating point, or probability
cutoff.

## Exact reductions

- An unordered draw is a card-count vector, not a permutation.
- Identical hand copies and identical structured Bench Pokémon form orbits.
- Multi-card choices are count vectors when order has no rule meaning.
- Energy payments are grouped by the final multiset, except energy with distinct
  effects remains distinct.
- Damage-counter placements use integer partitions over Pokémon orbits.
- Coin sequences are grouped by sufficient statistics only when later rules use
  only those statistics.
- A shuffled deck is represented lazily by its remaining multiset until an
  order-sensitive effect observes it.
- Face-down, unobserved, exchangeable prize positions have one representative
  decision. The revealed identity remains an exact chance outcome.
- Trigger order, yes/no, and count choices merge only after successor equality,
  unless a commuting rule has been explicitly audited.

Unknown selection contexts fail closed: all legal actions are retained.

## State identity and hashing

Canonical keys include rule state, turn history and flags, the card/attachment
graph, pending effects and selections, and actor-relative knowledge/belief.
Physical serials are alpha-renamed. Logs, UI option order, and exact RNG state are
excluded. Belief weights are divided by their GCD.

Two fixed-key SipHash-2-4 results provide a stable 128-bit table index. The full
canonical bytes are interned and compared on every digest hit. Hashes are never
used as proof of equality. Namespace fields include engine/canonical-rule/deck/
evaluator versions and the leaf definition.

## Resource policy

Use at most two independent root workers. Each worker owns native states and its
chance provider; immutable card data may be shared. The intended RSS hard stop is
2.7 GB: approximately 1.3 GB keys/TT, 0.8 GB workers, 0.3 GB beliefs/policy/rational
values, and 0.3 GB headroom. Keep 30 seconds of the 600-second match bank.

An interrupted node is stored as an interval, never as an exact value. The
emergency action maximizes the proven lower bound and is marked `certified=false`.
The legacy `SearchBegin` API requires fabricated opponent identities. The agent
does not use that path for turn planning; `ExactDecide` below preserves unknown
zones and reports an interval when an identity is genuinely required.

## Native turn planner

`ExactDecide` is the information-safe replacement for `SearchBegin` in the
submission agent. It accepts the sanitized serialized observation, the fixed
deck, aligned hand-card values, and one wall-clock budget. It returns the chosen
physical option indexes, exact rational lower/upper bounds, certification, and
search metrics.

The planner stores the actor's unknown deck and prizes as one card-count pool.
Draws and prize takes branch by card type with remaining-copy integer weights.
When an effect legally reveals the deck, prize multisets are enumerated lazily
with hypergeometric weights and the triggering action is replayed in each
resulting information state. A shuffled known deck is a multiset, not a sampled
permutation.

Two root workers own independent `Game` scratch state, TT, key memory, and a
shared per-worker deadline. Each TT is limited by both entry count and 550 MiB
of stored key/value bytes. Unknown opponent identities are never populated with
guessed cards: an actual identity dependency produces the full evaluator
interval and `certified=false`.

Windows x64 `cg.dll` and Linux x86-64 `libcg.so` include `ExactDecide`. The
Python wrapper feature-detects the symbol so the unchanged ARM64 library uses a
legal deterministic fallback.

## Git deck workflow

`main` contains generic engine/search code. Deck/evaluator changes belong in
`deck/<slug>` branches and a profile under `decks/`. Select a profile with
`PTCG_DECK_PROFILE`; every profile carries a canonical SHA-256 checked at load.

