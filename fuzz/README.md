# Coverage-guided fuzzing

Cerv keeps independent libFuzzer entry points for byte parsers and bounded stateful primitives. `make fuzz` compiles with Clang + ASan + UBSan, copies checked-in seeds into `build/fuzz-corpus/`, and mutates only that build copy so local campaigns never rewrite the persistent regression corpus.

There are 17 targets: complete request, request line, target, authority, Content-Length, Accept-Encoding, entity tags/If-None-Match, If-Range, HTTP date, Range, qvalue, bounded buffer, response planning, timer heap, connection/runtime state, configuration/resource formulas, and arena ownership/generation state. Parser targets use semantic/idempotence or independent-model oracles where practical. Stateful targets validate invariants after arbitrary bounded operation sequences.

Persistent corpus content includes ordinary valid forms, RFC edge cases, request-smuggling differentials, path ambiguity cases, HTTP/representation regressions, timer operation seeds, runtime seeds, configuration seeds, and arena lifecycle seeds. Any minimized reproducer belongs under the matching `fuzz/corpus/<target>/` directory and in a deterministic regression test when feasible.

Use `FUZZ_RUNS=<n> make fuzz` for a bounded local campaign. Longer campaigns can execute binaries under `build/fuzz/` directly with larger run/time budgets. Each target remains independent so one parser or state machine cannot mask another target's coverage or crash.
