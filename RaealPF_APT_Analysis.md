# APT (Geometry Dash 2.2081) — technical analysis for RaealPF

**Source snapshot analysed:** `aliakigreque/APT`, commit `2286ad3fc2b85ada7f33ebff885a10d4810fb3e3` (the repository's latest commit on 22 Nov 2026; analysis made 24 Nov 2026). This is a source-oriented explanation, not a claim that every level or trigger is perfectly simulated.

## Executive summary

APT is not a macro recorder that replays somebody else's inputs, and it is not just a geometric route planner. It builds a local model of the loaded level, advances a custom Geometry Dash simulator one tick at a time, and searches for a sequence of button states that reaches the level goal. Its core is a **heuristic, layered beam search** with state deduplication, portal-aware priorities, geometry-based pruning, multi-threaded expansion, and several forms of learned backtracking.

The strongest idea to take away for RaealPF is the *whole feedback loop*: a game-specific simulator makes candidate inputs meaningful; a search explores that simulator; dead ends trigger targeted retries rather than a blind restart; and live execution is checked against the real game. Simply increasing beam width would not reproduce most of APT's effectiveness.

APT's own README also places restrictions on the mod, its code, and its described way of working. See [Licensing and use](#licensing-and-use) before treating this as a blueprint to reimplement or distribute.

## 1. What APT does

At a high level, APT:

1. Reads the currently loaded level and relevant game settings into a `World` representation.
2. Creates a simulated `Game` state at the start of an attempt.
3. Searches possible per-tick inputs while simulating movement, collisions, triggers, portals, and object state.
4. Turns the chosen input sequence into a macro and either plays it after an offline solve or feeds it into the real game while live search continues.
5. Lets the player stop and edit a path tick-by-tick, then continues searching from the edited state.

Its stated target is Geometry Dash **2.2081 on Windows**, with **Geode 5.11.2**. The mod metadata pins those versions. The project describes its simulator as exact, and the build disables fast-math/FMA-style contraction to reduce floating-point differences; nevertheless, the source explicitly tracks unsupported triggers and approximations, so “exact” should be read as the goal, not a guarantee for every level. ([README](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e3/README.md), [mod metadata](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e3/mod.json), [build configuration](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e3/CMakeLists.txt))

## 2. End-to-end architecture

```text
Loaded GD level
     │
     ├─ src/world_export.cpp: copy objects, hitboxes, settings, trigger data
     ▼
 sim::World ── World::build(): indexes, groups, moving objects, portal clusters
     │
     ├─ Game::reset(): initialise a simulated attempt and its state
     ▼
 sim::Game ── stepTick(state, input): simulate one tick
     │
     ├─ searchPath(): expand, score, merge, prune, and backtrack
     ▼
 per-tick input path ──► macro / offline playback
                    └──► live plan publication ──► real game buttons
                                                 └─► mirror check / resync
```

### Level ingestion

`src/world_export.cpp::buildWorld` copies the live game's level objects and properties into the simulator's `World`: positions, collision rectangles, oriented boxes, groups, trigger parameters, level settings, start conditions, and other state needed by simulation. Some 2.2 trigger properties are recovered from the raw level string because they are not all available from the in-memory object fields. `sim/World::build` then prepares data structures such as spatial/collision indexes, group lookups, moving-object information, and portal clusters. ([world export](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/src/world_export.cpp), [world preprocessing](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/world.cpp), [World definition](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/world.hpp))

The simulator has dedicated code for player physics, collision handling, slopes/rotation, portals, orbs/pads, dual mode, triggers, time/camera effects, and object movement. A `Game` state contains much more than the player's x/y: velocities, mode and gravity flags, buttons, dynamic object state, trigger state, timing, RNG, and (when relevant) the second player. `stepTick` advances that state through the simulation pipeline. At load time APT logs trigger/camera features it cannot simulate or only approximates, which is useful diagnostic information when a level behaves unexpectedly. ([step pipeline](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/step.cpp), [physics](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/physics.cpp), [trigger implementation](https://github.com/aliakigreque/APT/tree/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim))

### Search entry point

`searchPath` builds level cues and item goals, initializes a search run, and repeats a sequence of tick stages until it completes, reaches a configured horizon, is cancelled, exhausts its budget, or cannot find another viable state. The search is therefore built around a simulator transition function, not around screenshots or hand-authored collision geometry alone. ([search options and result](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search.hpp), [search loop](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search.cpp))

## 3. How the path search works

### 3.1 Action space: button states, one tick at a time

The search's action is a small bitmask describing which jump buttons are held on the next tick: bit 0 for player 1, bit 1 for player 2. In a normal single-player state there are two candidates (hold/release); when the level is in two-player dual mode there can be four combinations. The simulator sees the held state and processes press/release transitions just as the game does.

APT does not always allow a new press on every tick. `minPressGap` limits how soon another press can occur; the default UI setting is 12 clicks/second, converted from 240 simulation ticks/second to a 20-tick gap. Repeated failures can make the retry ladder reduce this gap, because some passages require faster inputs. Holding an existing press is not the same thing as issuing a new press. The search also contains a special minimum-segment rule for dart/wave input patterns. ([input branching and stepping](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_expand.cpp), [CPS setting](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/src/settings.hpp))

### 3.2 Layered beam search

A search layer represents one tick. For each surviving state in the current layer, APT:

1. Generates allowed button-state actions.
2. Copies or reuses a game state and simulates one tick for each action.
3. Drops actual deaths and additional states rejected by its learned/geometry rules.
4. Adds costs and bookkeeping such as press timing, portal debt, and fragility.
5. Merges equivalent-looking children, sorts the candidates, then keeps at most the current beam width.

The default beam width is **150 states per tick** (the UI allows 100–2400). This is not a promise to examine every possible input history: it is a bounded search, deliberately selecting a subset of states at every layer. ([default options](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search.hpp), [expand](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_expand.cpp), [merge/sort/prune](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_layer.cpp))

### 3.3 State keys and duplicate merging

To avoid keeping many nearly identical histories, APT makes a compact search key. It includes rounded x, quantized y and vertical velocity, mode/ground/jump-related flags, and extra data derived from object/trigger state and dual-player state. The base quantization is 2 pixels for y and 0.5 for vertical velocity; the search can use finer values in difficult regions or on wider retries. When keys match, it also checks trigger-state equality before merging and keeps the better-ranked node.

This is a speed/coverage trade-off: the key is not a byte-for-byte representation of every float and every part of `Game`. In particular, quantization and compact hashed “extra” state mean two states can be treated as equivalent even if they are not perfectly identical. That is one reason a beam search can prune a path that would have worked. For RaealPF, make the key reflect *all future-relevant state*; if a new feature changes future transitions, it must participate in equivalence/dominance decisions as well as in simulation. ([key construction](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_maps.cpp), [trigger-aware merge and ordering](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_internal.hpp), [layer merge/prune](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_layer.cpp))

### 3.4 Ranking: not just “furthest x wins”

APT's search ranking is deliberately multi-part. Broadly, it prefers lower **portal debt**, then lower accumulated **cost**, then lower **fragility**, with additional tie-breaking. The cost model encourages fewer inputs but can reward presses that advance relevant item/counter goals. It can also penalize near-collision timing, missed orbs/portals, late speed changes, persistent-item changes, leaving the useful level region, and other risky or unpromising states. Several of those are heuristic nudges rather than exact game rules.

Portal handling is a major differentiator. APT groups nearby portals, estimates which ones matter or are reachable, tracks missed speed/mode exits, and detects when the best debt level rises. It can then restart from before that portal and try again with a wider search or a forced/must-take exit. “Debt” is a search priority and retry signal—not a claim that every decorative or irrelevant portal must be taken. ([costs and portal handling](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_costs.cpp), [debt/retry ladder](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_debt.cpp), [input and fragility costs](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_expand.cpp))

The beam also tries to stay diverse. It groups states into coarse classes (including mode, position/velocity regions, and related flags), applies class/lineage quotas, and reserves some room for states that differ spatially or dynamically. That helps prevent a cheap but narrow cluster of near-identical states from filling the entire beam. ([state classes](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_internal.hpp), [beam diversity and pruning](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_layer.cpp))

### 3.5 Geometry and “obvious dead end” pruning

Before or during search APT builds maps/cues from the level: portal clusters, vertical bands, empty regions, level extent, structural/free-space information, and a void hull. These enable costs or early rejection for cases such as a player staying far outside meaningful level content, an obviously doomed fall, a long stuck/looping route, a trap corridor, or a flying mode passing an exit without taking it.

This is a valuable optimisation, but it is also a place where a novel level mechanic can fool the planner. APT often treats these as *learned or relaxable rules*: after all states die, it may allow a previously rejected zone, widen a corridor, or search from an earlier point. For RaealPF, distinguish true impossibility checks (e.g. the simulator says the player died) from “probably bad” heuristics; prefer a penalty before a hard kill unless the rule is proven sound. ([map construction](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_maps.cpp), [cue construction](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search.cpp), [state checks](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_expand.cpp))

### 3.6 Backtracking and learned retries

When a whole layer dies, APT does more than stop. Its backoff pipeline can:

- Record places where returns, flying exits, structure escapes, or off-level routes were rejected, then relax selected zones on the next try.
- Detect loops and stuck/trap corridors, and restart before the part that caused them.
- Infer that a counter/item value, toggle orb/block, or touch trigger needs to be reached before passing a point.
- Learn that a mode portal led to a dead end and avoid it on a later lineage.
- Increase the beam, refine y/velocity quantization, reduce the click gap, and move the restart point further back.

The search keeps **hard** restart points for committed/replayable prefixes and **soft** restart points containing several alternative states (up to 96 in the current source). The widening ladder tries different combinations of restart distance, beam multiplier, precision, and press gap; it does not always repeat the exact same search from tick zero. This learned, targeted recovery is probably one of the main reasons APT does better than a bare beam search. ([backoff pipeline](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_backoff.cpp), [restart/soft-state machinery](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_restart.cpp), [widening rungs](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_internal.hpp))

### 3.7 Multi-threading and performance

Parent states are expanded in parallel with worker-local buffers/state arenas and later merged. The live mode chooses its worker count to leave CPU capacity for the game; “Fastest search” changes that trade-off. The game can pause if the search has not produced enough plan, and the live search reduces its beam when appropriate to avoid running too far behind. This is practical systems engineering around the algorithm, not a property of beam search itself. ([worker pool and expansion](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_internal.hpp), [live thread selection and planner](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/src/simplanner.cpp))

## 4. Offline, live, editor, and safety behavior

| Mode | What happens |
|---|---|
| **F4 — offline search** | The game waits while the simulator searches. On an ordinary level success APT converts the path to a macro, saves a GDR macro, and starts playback. A special settings-menu plan is handled separately. On failure it retains the best partial path and exports diagnostic level data. A time limit is optional; 0 means unlimited. |
| **F2 — live search** | APT restarts the attempt and searches in a background thread while feeding the real game a published plan. It initially seeks about 2,400 ticks (10 seconds) of lead. Offline-style commits wait for path convergence; live publishing also commits a selected prefix to maintain lead and removes incompatible search branches. If a later publication changes already-played inputs, APT marks that attempt as lost rather than rewriting history. Failed searches can retry on later attempts with a wider beam. |
| **F3 — path editor** | Pause at a tick, step backward/forward, change the held jump state, and resume the search from the edited route. This makes the tool useful as a playtesting/assistive planner, not only as a fully automatic solver. |
| **F5 / F6 / F7** | Play the current macro, save a GDR macro, or record detailed game/player state and level data for debugging. |

During live mode APT replays the real inputs in a simulator “mirror”, compares simulated and real player samples, and logs RNG/position divergence. It can resync the simulated state from a small observed discrepancy and continue searching. This is important: a simulated solution is not enough if the game has drifted from the model. ([user-facing controls](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/about.md#L16-L62), [offline/live planner](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/src/simplanner.cpp), [tick editor](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/src/editor.cpp), [real-game engine](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/src/engine.cpp))

**Safe mode** is on by default. The hooks prevent bot-controlled attempts/completions/jump counts from being credited normally. This is a stats safeguard, not permission to submit bot play as human play: APT's own docs warn that bot completions can violate leaderboard/community rules and lead to removed scores or bans. ([Safe mode hooks](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/src/main.cpp), [APT progress/leaderboard warning](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/about.md#L44-L54))

## 5. What APT does especially well—and what it does not promise

### Strong points

- **Game-specific simulation:** branches are evaluated against a detailed state transition model rather than a generic “jump over obstacles” abstraction.
- **More than brute force:** portal debt, touch/counter learning, structural maps, state diversity, soft restarts, and precision widening target common failure modes.
- **Live feedback:** prefix publication, real-input mirroring, RNG checks, and resync make live execution more robust than blindly firing a precomputed macro.
- **Debuggability:** recordings, exported level JSONL, logs, and the `apsim` command-line harness support replay/verification and self-tests. ([APSIM tools](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/tools/apsim.cpp))

### Limits

- **Not complete or optimal:** a finite beam, quantized keys, heuristics, and restart limits mean failure does not prove no path exists. APT prefers low-cost/few-input candidates, but it does not guarantee a globally minimum-click or maximum-robustness solution.
- **Simulator coverage matters:** unsupported trigger/camera cases and approximations can make a simulated path diverge from the real level. The live mirror is a diagnostic/correction loop, not a proof of correctness.
- **Version-specific integration:** memory offsets, Geode bindings, timing, RNG, and physics can change between GD versions. The current metadata/build target is pinned to 2.2081/Geode 5.11.2.
- **Resource cost:** wide beams and complex trigger states can be expensive; live mode has to trade search throughput against frame rate and plan lead.

One adjacent detail: the repository has a separate `sim/robust.cpp` and an `apsim robust` utility for inspecting/centering click timing windows. In this source snapshot that code is not included in the Geode mod's CMake target, and `robustnessLookahead` is declared in `SearchOptions` but is not used elsewhere. I would treat it as a separate/experimental offline tool, not as a default guarantee of the in-game search. ([robustness helper](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/robust.cpp), [CLI command](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/tools/apsim.cpp), [mod build target](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/CMakeLists.txt))

## 6. RaealPF design lessons

You have not listed RaealPF's new features yet, so these are general extension points rather than assumptions about your roadmap.

### Keep the same *separation of responsibilities*, but make it more modular

A clean RaealPF layout could separate:

1. **Game adapter / world importer** — GD-version-specific object and setting extraction.
2. **Deterministic simulator** — `step(state, action)`, collision/trigger mechanics, RNG and serialization.
3. **Search core** — action generation, state key/equality, beam policy, goals, costs, restart strategy.
4. **Feature modules** — optional objectives/constraints/route policies that plug into the search without being mixed into every expansion branch.
5. **Execution/editor layer** — Geode hooks, live input injection, resync, macro IO, UI.
6. **Headless verification tools** — replay real recordings, compare per-tick state, run regression tests and benchmarks.

APT's `SearchRun` is capable but tightly coupled: a lot of feature-specific checks, costs, and backoff rules live in the expansion/search files. For a fork-independent RaealPF, explicit interfaces such as **goal predicates**, **hard constraints**, **soft cost terms**, and **restart policies** would make your new features easier to add and ablate.

### Be strict about state identity

When you add a feature, ask: **Could this variable change any future transition or goal result?** If yes, it belongs in the simulator state and usually in the search key/equality check, checkpoint serialization, replay records, and live mirror validation. Examples include new persistent counters, RNG streams, timed effects, target selections, or a new player control mode. Missing one can make the search merge two states that look alike now but have different futures.

APT's compact/quantized key is a useful performance example, but not a safe universal template. RaealPF could retain coarse spatial buckets while comparing a more complete canonical state before merging, or keep a small Pareto set per bucket (e.g. cost, click count, robustness, and remaining resources).

### Separate “impossible” from “unattractive”

- **Hard-kill only** when the simulator proves death or a feature's constraint truly cannot be met.
- **Penalize** uncertain detours, low-quality routes, risky timings, or off-structure movement.
- **Learn/retry** when the beam empties: retain the cause, restart before it, adjust precision/width/action limits, and relax only the heuristic that caused the failure.

This avoids turning a promising RaealPF feature into an unsound pruning rule. Keep cost dimensions interpretable in logs; consider ranking a vector (e.g. completion feasibility, objective satisfaction, robustness, clicks, time) instead of hiding all priorities in one opaque scalar.

### Testing pattern worth adopting

APT's useful pattern is to save enough data to reproduce a mismatch outside the game. For RaealPF, make a headless test path early:

- Capture a level plus exact player/input/RNG state.
- Replay one tick at a time and compare simulator output to recorded game output.
- Keep regression cases for each mode, portal, trigger family, collision edge, dual-player case, and each RaealPF-only feature.
- Run the search deterministically with fixed seeds; check that it produces a valid replay, not merely a plausible path overlay.
- Benchmark per-tick expansion, memory/state counts, and time-to-first-plan; test both worst-case levels and ordinary ones.
- Version your recording and macro formats so future simulator changes remain diagnosable.

The source tree's `tools/apsim` (`verify`, `solve`, `replay`, `test`, `bench`, `pathmap`, `snap`, and `robust`) is a good *testing-tool shape* to study without taking its implementation. ([APSIM CLI](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/tools/apsim.cpp))

## 7. Licensing and use

APT's [README terms](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/README.md#L43-L63) say that the mod is free for personal uses such as showcases, playtesting, and creating/editing macros; they also say **commercial use of the mod, its code, and “the way it works” (including a copy or reimplementation) requires written permission**, and that modified/repackaged versions must not be published under the APT name or as your own work. I did not find a separate root `LICENSE` file in this snapshot.

Because RaealPF is intended to be a similar pathfinder, do not assume that adding features automatically resolves those terms. If you intend to sell, monetize, bundle, redistribute, or closely adapt/reimplement APT's approach, get written clarification/permission from the author first. This is a summary of the repository's stated terms, not legal advice.

## Source map

All links below are pinned to the analysed commit so they remain tied to this review:

- [README / terms](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/README.md)
- [User guide and controls](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/about.md)
- [Search API/options](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search.hpp)
- [Search orchestration](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search.cpp)
- [Expansion and per-tick checks](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_expand.cpp)
- [State key and level maps](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_maps.cpp)
- [Beam merge/pruning](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_layer.cpp)
- [Portal-debt retries](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_debt.cpp)
- [Dead-end learning/backoff](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_backoff.cpp)
- [Prefix publishing/restarts](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/search_restart.cpp)
- [Simulator state and tick pipeline](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/sim/gd.hpp)
- [Geode world importer](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/src/world_export.cpp)
- [Live/offline planner](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/src/simplanner.cpp)
- [Game-step engine and editor](https://github.com/aliakigreque/APT/tree/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/src)
- [Headless simulator tools](https://github.com/aliakigreque/APT/blob/2286ad3fc2b85ada7f33ebff885a10d4810fb3e/tools/apsim.cpp)
