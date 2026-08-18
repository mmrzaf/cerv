# Source Review Standard

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Normative review requirements for parser, filesystem, event-loop, configuration, and optimization changes.  

## Any parser change

- [ ] Primary RFC section checked.
- [ ] Accepted grammar did not broaden accidentally.
- [ ] Rejected grammar is standards-permitted to reject.
- [ ] All offsets/lengths remain bounded.
- [ ] Boundary tests added.
- [ ] Fuzz target reaches branch.
- [ ] No locale-sensitive parsing.
- [ ] No attacker input enters assertion path.

## Any filesystem change

- [ ] Root-relative FD model preserved.
- [ ] `openat2()` resolve flags preserved/strengthened.
- [ ] Descriptor metadata used after open.
- [ ] Symlink/special-file tests updated.
- [ ] Error classification reviewed.
- [ ] FD ownership/close paths reviewed.

## Any event-loop change

- [ ] Nonblocking semantics preserved.
- [ ] Partial syscall success handled.
- [ ] EAGAIN/EINTR handled correctly for this syscall.
- [ ] Work quantum remains bounded.
- [ ] Stale event generation checked.
- [ ] Deadline cannot be starved.
- [ ] Slot cleanup idempotent.

## Any new configuration

- [ ] It directly supports project mission.
- [ ] It has a finite input grammar.
- [ ] Invalid value fails before serve loop.
- [ ] It does not silently multiply another resource bound.
- [ ] Interaction matrix with existing options is tested.
- [ ] Documentation states observable effect exactly.

## Any optimization

- [ ] Before/after benchmark exists.
- [ ] p99 and saturation compared, not only peak RPS.
- [ ] Memory/FD/syscall bounds unchanged or spec updated.
- [ ] Sanitizer/fuzz/proof gates still pass.
- [ ] Complexity increase is justified by material result.
