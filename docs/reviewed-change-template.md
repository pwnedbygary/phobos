# Portable reviewed-change guide

Opt-in template for any repository. Replace the project-specific fields below
before adopting; it installs no hooks and has no effect on other repositories
or platform-generated checkpoints.

- Project instructions: `<instructions path>`
- Handoff and work plan: `<paths>`
- Build/test toolchains and production-path checks: `<commands and versions>`
- Invariants, sensitive artifacts, release constraints: `<project-specific list>`
- Publication target and authorization: `<remote/branch or managed merge>`

## Coordinator startup

Read the configured instructions, handoff and relevant work plan. Verify actual
branch, HEAD, dirty/staged/untracked files, remote, dependencies, available evidence
and execution capabilities. Preserve unrelated work. If an input is missing,
identify the precise blocker rather than inventing its contents.

Scope one coherent change with prerequisites, allowed files, invariants, acceptance
criteria and stop conditions. Give every worker those constraints and the relevant
evidence. Parallelize independent investigations, not conflicting edits.

## Mandatory loop for every authored commit

This includes code, tests, documentation, diagnostics, amendments and releases.

1. Implement the smallest justified change. Exercise the production path, not a
   copied algorithm; derive test expectations from independent specifications.
2. Run proportionate checks. Record commands, toolchain, actual successes/failures
   and environment limitations. Do not confuse host, target and production evidence.
3. Update the handoff before review: identity, observations, derivations, pinned
   comparisons, hypotheses, unknowns, files, checks, blockers and next decision.
4. Freeze base revision, explicit paths, full diff including modes/deletions and
   SHA-256 hashes for all intended files including untracked/new files.
   Store the manifest outside its own hashed set; hash the diff as well.
5. Obtain independent read-only review of that exact snapshot: separate agent,
   separate session or human, never the implementer. Supply requirements, complete
   patch/new files and surrounding production code. Require independent hash
   recomputation and rerun checks, not trust in the summary.
6. Reviewer returns snapshot, PASS/NEEDS CHANGES/BLOCKED, findings with severity
   and evidence, checks actually run, and nonblocking suggestions separately.
7. Resolve every finding explicitly. Verify edits landed. Any edit after review
   requires delta re-review. If a reviewer expires, restate prior findings to a
   new reviewer. Unavailable review or unresolved blockers prohibit commit.
8. After exact-snapshot PASS, verify hashes, stage only explicit reviewed paths,
   inspect staged diff, commit with review identity/verdict, snapshot hashes and
   check results (or a durable reference to that record).
9. Publish non-force when authorized to the agreed target or through the managed
   merge. If blocked, preserve the local work and report why. New remote changes
   require safe reconciliation, checks and review, never overwriting history.

Treat old documents as evidence, not authority over contradictory source.
Prefer the smallest discriminating check; absence of errors is not proof of
correctness. A review PASS accepts a scope, not every downstream outcome.