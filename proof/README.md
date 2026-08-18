# Bounded checks

`model_checks.c` is a native exhaustive reduced-domain checker. It runs on every `make proof` and covers checked arithmetic, decimal accumulation, qvalue classification against an independent model, range normalization, fixed-buffer behavior, hostile-path decoding, timer-heap operations, repeated connection-slot generation/reuse, connection state/deadline classification, and configuration/resource formulas. These checks are exhaustive only over their explicitly reduced domains.

`cbmc/` contains CBMC harnesses for bounded helpers and state structures. When `cbmc` is installed, `make proof` invokes them with pointer, bounds, signed/unsigned-overflow checks, and **unwinding assertions enabled**. `make proof-required` makes CBMC availability mandatory. Each harness documents its assumptions and unwind bounds; a passing run means the asserted properties hold only under those stated conditions.

The path harness stubs authority parsing because its property is decoded-path behavior. Runtime model checks isolate pure arena/timer/state logic from real epoll/socket scheduling; real Linux behavior is covered by integration and fuzz suites. Proof models do not introduce request-path allocation.
