# Phobos development process

Applies to every agent-authored code, test, documentation, diagnostics, fix,
amendment and release commit in Phobos. Adapted from the supplied
DEVELOPMENT_PROCESS and COORDINATOR_PROMPT guides. This is a workflow agreement,
not an installed hook. Platform-generated checkpoints and unrelated repositories
are excluded; copying the portable template elsewhere requires explicit adoption.

## Per-change gate

1. State one coherent scope, files, prerequisites, invariants and stop conditions.
   Verify branch, HEAD, dirty/staged/untracked files, upstream and dependencies.
   Preserve unrelated work and the reconciled import; never reset or force-push.
2. Implement only the minimum evidence-justified change. Preserve saves, signing
   identity, timing defaults and verified multi-system behavior. For performance,
   classify observations, derived results, pinned comparisons, hypotheses and unknowns.
   Comments and historical handoffs are not fresh native evidence.
3. Run proportionate checks through the real production path. For behavior changes,
   prefer failing-before/passing-after fixtures with independent expected results.
   Compile affected translation units with the actual Android configuration.
   Record exact commands, toolchains, outcomes, failures and unavailable checks.
   Host tests/builds never substitute for device validation.
4. Update `docs/handoff.md` and `docs/implementation-plan.md` before review with
   scope, source/APK identity, observations, hypotheses, actual checks, limitations
   and next eligible experiment. Do not claim an edit landed without inspecting it.
5. Freeze the snapshot: base HEAD, explicit intended paths, full binary-capable
   tracked diff, deletions/modes, and SHA-256 of every intended existing/new file.
   Include untracked and ignored-new intended files explicitly: plain `git diff`
   omits them. Save the manifest and diff outside the snapshot to avoid recursive
   self-hashing. Record the diff's SHA-256 too.
6. Obtain independent read-only review from a separate agent/session or human.
   Supply exact base, manifest, diff, full new files, requirements, source context
   and check results. Reviewer must recompute hashes and rerun relevant checks,
   not accept the author's summary. No reviewer edits, commits, pushes or device actions.
7. Require PASS / NEEDS CHANGES / BLOCKED, severity and evidence for each finding,
   actual verification, and separate nonblocking suggestions. Resolve every finding
   by correction or evidence-backed disposition. If the reviewer expires, give a
   fresh reviewer the previous findings. Any subsequent edit requires delta review.
   Unavailable independent review means COMMIT BLOCKED; retain the patch.
8. Only PASS for the exact final snapshot permits commit. Recheck hashes and scope;
   stage explicit paths (never broad `git add -A` or `git add -u` sweeps).
   Check the staged diff matches the review. Include reviewer/verdict, manifest and
   diff hashes, and rerun results in the commit message or durable external review
   record referenced there. PASS is not proof of speedup or release readiness.
9. Publish non-force to the agreed branch after an approved commit when publication
   is authorized. If remote advanced, stop, safely reconcile, retest and re-review.
   If credentials/publication are unavailable, report local commit and PUSH BLOCKED.
   Managed task merge/publication may perform this step; do not independently push
   around that workflow.

## Phobos checks and invariants

- Documentation-only changes: whitespace/diff checks, link/path checks and
  independent citation/claim review; an APK rebuild provides no extra doc evidence.
- Android builds: `bash scripts/build-replit.sh assembleLegacyDebug` (or the exact
  requested flavor). The wrapper currently pins Gradle 9.6.0, AGP 9.3.2, JDK 17,
  CMake 3.22.1; SDK setup requests NDK 28.2.13676358. Verify actual versions each run.
  Do not copy the supplied Mupen guide's Gradle 8.4 / AGP 8.2.2 or DD test commands.
- Missing submodules/SDK/device are explicit blockers for their respective checks.
  Use repository tests applicable to touched paths; never invent suite results.
- Keep Vulkan SyncFull waiting and DP interrupt order. Before any async proposal,
  audit RDRAM/hidden-memory visibility, framebuffer reads, reset, unload and states.
  Never acquire `vulkan.mutex` in the abandon path.
- No blind timing, cache-coherency, renderer or RSP replacement. Profiling must be
  bounded, opt-in and overhead-tested; preserve normal release behavior.
- Never publish keys, private workspace metadata, copyrighted games/firmware,
  raw captures or diagnostic binaries without explicit appropriate authorization.
  Do not uninstall or clear app data to run a comparison.

See [coordinator prompt](coordinator-prompt.md),
[portable guide](reviewed-change-template.md) and
[performance baseline](performance-audit.md).