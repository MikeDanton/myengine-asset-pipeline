# Repository agent instructions

Read [README.md](README.md), [branch lifecycle](docs/guides/branch_lifecycle.md) and [CI and PR ownership](docs/CONTINUOUS_INTEGRATION_PR_OWNERSHIP.md) before work. Preserve all existing validation, rights, provenance, native and release gates.

## Progress reports and task handoff (required)

End every substantive update with owning issue/PR and exact tested head, exact validation result, direct current CI run/job links when applicable (otherwise explicitly say why none exists), next step/owner, and **Human action/decision required: None / Action / Decision** with specifics as needed. For private platformer, report local test commands and exact-head results instead of CI; do not run CI for reporting purposes. Never substitute historical checks or a green badge for current validation.

**PR status (required):** In every substantive report footer, include a separate `PR status:` line for each relevant PR, with its link and a colored emoji plus explicit text. Verify GitHub's current state; do not infer merge from green CI. Use:

- 🟢 **MERGED** — verified `merged=true`; name target branch and merge SHA, link integration evidence if available.
- 🔵 **READY** — non-draft, required validation/review complete and eligible for integration; not merged yet.
- 🟡 **AWAITING CI** — exact-head required checks queued/running; name the pending check.
- 🔴 **BLOCKED** — failed required checks, conflicts, missing required evidence or technical blocker; state why.
- 🟣 **HUMAN REVIEW** — explicit outstanding human approval/action/decision; state what is needed.
- ⚪ **DRAFT** / **OPEN — IN PROGRESS** — work incomplete; distinguish actual GitHub draft status.
- ⚫ **CLOSED — UNMERGED** — closed without merge; state reason or successor PR if known.
- ⚪ **UNKNOWN** / **NO PR** — state unavailable or no PR exists; distinguish them and do not guess.

For open PRs, report the actual draft flag and all material blockers separately when relevant; prefer the actionable disposition (human gate, technical blocker, pending validation, ready, then in progress). A draft is never READY. Keep CI/local validation status and exact tested head separate from PR disposition, including after merge. Never claim issue completion or release acceptance from a merge alone. Use the text labels even in clients that cannot display emoji colors. Preserve the exact CI run/job links, next owner/action and final human-action/decision line. This is reporting only, not new merge authorization.
