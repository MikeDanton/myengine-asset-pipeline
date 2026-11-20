# Branch lifecycle policy

Status: active, 2026-11-20. Follow MyEngine2's cross-repository branch lifecycle and PR ownership policy, including exact-head CI, existing reviews, and required human/asset-rights gates.

- Prefer finishing and merging an existing PR before starting another branch. Soft limit: three concurrent implementation branches; exceptions require an issue, owner, blocker and retirement condition.
- Use issues and roadmaps rather than branches for planning or TODOs.
- Merge only when exact-head checks, reviews and approval gates pass. Do not bypass provenance, asset rights, or native visual review.
- Enable automatic head-branch deletion after merge where permitted.
- Audit stale branches against merged PRs, current integration base and unique commits. Delete only verified redundant heads; preserve open-PR, protected, unique-work and unclear branches until reviewed.
- Record cleanup decisions and branch deletion outcomes in an issue. Do not force-push or discard other contributors' work.
