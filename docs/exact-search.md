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

Two root workers own independent `Game` scratch state and partial cursors. They
share a 64-shard table of immutable completed entries; SipHash chooses a shard
and the complete canonical bytes are compared inside the digest bucket. Root
actions are probed fairly, then advanced in bounded node quanta. Actions with
the same canonical successor use one representative while all physical root
intervals remain in the API result. Unknown opponent identities are never
populated with guessed cards: an actual identity dependency produces the full
evaluator interval and `certified=false`.

`ExactCanonicalState` is used at every native node. It explicitly encodes State
scalars, Card state, references, attachments, ordered stacks, and exchangeable
zone multisets. It never hashes C++ padding, unused FixedList storage, physical
serials, or absolute move counters. This is required for resumability: the old
raw serializer produced different keys when the same root action was replayed.
Canonical bytes use lossless zero-run encoding before hashing and storage.

Interrupted Decision nodes retain exact action intervals and a round-robin
cursor. Chance nodes retain certified integer mass. Terminal turn leaves are
evaluated directly and are not inserted into the TT because they are cheap and
almost always unique. RSS is sampled in native enumeration loops; 2.7 GiB
stops further search safely and returns the current proven interval.

Windows x64 `cg.dll` and Linux x86-64 `libcg.so` include `ExactDecide`. The
Python wrapper feature-detects the symbol so the unchanged ARM64 library uses a
legal deterministic fallback.

`ExactDecideV2` additionally accepts an optional known opponent deck for
closed-world validation. Production calls omit it; an identity-dependent read
of an unknown opponent zone therefore still fails closed. Detailed metrics
separate unknown-opponent reads, unsupported concrete reads, interrupted
transitions, depth guards, raw choices, and quotient-merged choices.

`ExactTurnBegin` retains the selected root worker's transposition table and a
compact contingent policy for the rest of the turn.  Actor decision nodes are
indexed by a serial-independent semantic observation key; actions are stored as
semantic option descriptors and remapped to the current physical option array.
`ExactTurnAdvance` conditions on the next observation and returns a certified
policy hit without expanding nodes when the information state is unambiguous.
If multiple hidden beliefs produce different actions or values for the same
observable key, lookup fails closed and resumes exact search from the live
observation. `ExactTurnRelease` frees all native session memory at turn end.
`ExactTurnProgress` is read-only and reports the current root action, depth,
canonical/successor merges, resumed work, elapsed time, and memory.

The Python policy owns a `PolicyContext` per player.  Each context has its own
600-second chess clock, native session, and decision metrics; only time spent in
that player's action calls is charged.  This prevents self-play from sharing a
single budget or charging one player for the opponent's search.

Count-only and existence-only conditions on a hidden deck use the zone size and
do not request card identities. Concrete searches suspend the transition,
enumerate bounded card-count allocations with combination weights, materialize
the selected world, and replay from the pre-transition checkpoint. Identical
copies in an exchangeable searched deck share one semantic action.

`BattleStartSeeded` uses an explicitly specified Fisher-Yates shuffle so its
fixtures are identical under MSVC and libstdc++. With the Majkel1337 profile,
seed 6 reaches a first-turn Poké Pad position whose only root choices are Poké
Pad and End. Both Windows and Linux enumerate 1,264,533 evaluated leaves and
return the exact value 4100 with `certified=true`, `opaqueNodes=0`; observed
wall time is approximately 118 seconds on the two-core development target.
With retained policy generation enabled the same position remains the turn-one
regression oracle; its three subsequent Poke Pad choices are served by the
certified policy in under one millisecond each with no resumed search nodes.

The recorded seed-6 turn-one policy path is `[[0], [5], [0], [0]]`, which is
used to reach the second-turn Hilda/Telepath/Boss regression without rerunning
the turn-one proof. A 570-second Windows run after canonicalisation processed
7,984,635 nodes with 107,496 canonical TT merges, 279,015 successor merges,
8,612 resumed actions, 18,056 resumed chance mass, 3 ms deadline overrun, and
773 MB peak RSS. It remained correctly uncertified because the non-End root
intervals were still open; this is a measured outstanding acceptance failure,
not treated as a proof or a successful full-turn certification.

## Git deck workflow

`main` contains generic engine/search code. Deck/evaluator changes belong in
`deck/<slug>` branches and a profile under `decks/`. Select a profile with
`PTCG_DECK_PROFILE`; every profile carries a canonical SHA-256 checked at load.

