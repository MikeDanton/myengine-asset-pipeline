# Continuous integration and GPT PR ownership policy

Status: proposed for integration, 2026-11-19. Applies to human-delegated GPT/Codex contributors to `myengine-asset-pipeline`, and to agent-authored pull requests. This is standing maintainer authorization for **eligible, green, reviewed PRs**, not permission to bypass repository rules.

## Default: own the PR through integration

A contributor owns its change until it is **merged and verified**, or has an explicitly recorded blocker and next owner. Publishing a branch, opening a PR, pushing a fix or observing a green badge alone is not task completion.

Prefer small, short-lived PRs from current `main`. Before starting a dependent PR, inspect existing PRs and try to finish/merge its prerequisite. A stack is justified only when a dependent change cannot wait or the prerequisite has a genuine blocker. Record the dependency, track each head independently, and rebase/reconcile the child after its parent merges. Never force-push or overwrite another contributor's work without an explicit handoff.

## Merge-on-green authorization and gates

An agent **may squash-merge without requesting fresh human permission each time** only when ALL conditions hold:

1. The task/PR is within the maintainer-approved scope, targets the repository's intended integration branch (`main`), is **not draft**, and no maintainer instruction says hold. Do not self-remove a deliberate draft/hold without first satisfying and documenting its exit criteria.
2. The PR is focused, reviewed for intent, has no unexpected files, no unresolved substantive review finding or active `changes requested`, and GitHub reports it mergeable against the current base. AI review feedback is advisory unless it identifies a real defect; inspect and triage it, do not blindly treat a bot comment as approval or rejection.
3. **Every required gate is satisfied at the exact current PR head SHA.** Confirm check run *conclusions*, not just queued/running status or a combined green indicator. Verify the affected test scope, any change-specific contract/integration checks and, where allowed by existing policy, a reviewer-approved SHA-bound local validation substitution. Skipped/not-triggered checks are not evidence of execution. CI/workflow/build infrastructure changes require independent CI, not local waiver.
4. The change requires **no unfulfilled human, native, visual, security, public-release or external dependency gate**. In particular, green headless CI does not approve Vulkan/UI/art appearance, gameplay feel, hardware behavior, benchmark claims, or image rights.
5. Re-fetch the PR head, base, checks and review state **immediately before merge**. Merge with an expected-head SHA guard; if either head/base or required evidence changes, stop, reconcile, rerun and recheck rather than merge stale results.
6. After merge, verify GitHub reports `merged=true`, record the merge commit and intended target, check the expected files on the target, and close/retire the work branch when safe. Never claim local or native tests that were not run.

**Not automatically eligible:** draft/hold PRs; feature changes requiring unresolved native/visual review; security and trust-boundary changes; authentication/credentials or permission changes; destructive data changes; distribution/publication, licensing, paid service or asset-rights decisions; release/tag/publication promotions; and changes whose test coverage or compatibility cannot be established. These require the relevant owner gate or explicit maintainer approval. Nonvisual simulation/engine logic is not automatically high risk: it may merge on green with appropriate deterministic tests, state/continuation invariants and relevant sibling integration.

Do not weaken branch protection, edit required-check settings, relabel a PR to suppress tests, or mark CI as passing merely to obtain a merge. Repository protection and existing validation commands remain authoritative.

## Operational workflow

1. **Before work:** inspect current `main`, related issues/PRs, concurrent branches and required test/native gates; declare ownership and scope. If a prerequisite PR is already eligible, integrate it rather than open another stacked PR.
2. **On push:** inspect the *exact* head commit, changed files, branch divergence, CI and review feedback. Fix failures on the owning branch, rerun relevant checks and monitor until terminal success or a real blocker. Never auto-merge while jobs are pending.
3. **On success:** classify the PR as eligible or blocked using the gates above. For eligible PRs, set ready-for-review only when exit criteria are met; squash-merge with the exact-head guard, verify integration, and update task/PR notes.
4. **On blocker:** leave draft/hold, state exactly what failed or who must act, name the required evidence, and avoid creating gratuitous successor stacks. For native-only gates, prepare reproducible commands/evidence manifests and hand off to the maintainer; do not impersonate human visual approval.
5. **On conflict:** the PR author is responsible for bounded reconciliation against current `main`; compare actual incremental changes, preserve newer unrelated work, and rerun exact-head CI. Do not force-push other agents' branches or replace a newer workflow with an older one.
6. **On handoff:** report PR link and SHA, precise pass/fail checks, validation environment, merge SHA or blocker, deferred native/release gates, and next owner. A stopped agent cannot silently monitor CI later: another agent/session or explicitly configured GitHub automation must resume the gate check.

## Classification examples

| Change | Merge on green? | Additional requirement |
| --- | --- | --- |
| Docs, test fixtures, non-runtime Python tools | **Yes**, if focused and reviewed | Link/source review; applicable tooling and CI checks |
| Nonvisual C++ logic and architecture contracts | **Yes**, if fully verified | Affected tests, determinism/continuation or compatibility gates as relevant; architecture review where required |
| CI, CMake, runner and packaging scripts | **Conditional** | Independent exact-head CI and event/security/permissions review |
| GPU/Vulkan/UI rendering, level appearance, visible gameplay | **Hold** if visual effect unapproved | Exact-head native capture and explicit human review |
| Production asset/art incorporation | **Hold** pending rights/provenance and appearance approval | Human approval and relevant pack/integration tests |
| Security, credentials, destructive migrations, releases/publication | **Hold** | Explicit owner authorization and specified gates |
| Promotion to release/default branch | **Hold** | Separate release/integration policy and explicit maintainer approval |

## Scope and enforcement

This file defines contributor behavior, **not a background agent or GitHub auto-merge bot**. A GPT can merge only while it is actively operating with permitted GitHub access and the gates are verifiable. Automatic unattended merging requires separate, explicitly configured GitHub controls and review. If a repository's existing rules are stricter, **the stricter requirement wins**. A green PR should not languish solely because its author stopped after pushing: the next contributing agent should first check whether it can complete that merge.
