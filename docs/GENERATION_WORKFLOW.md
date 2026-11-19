# Generation candidates and quarantine (v1)

Last edited: 2026-11-19. This is an **authoring** interface, not runtime inference.

The existing tools have three distinct duties:

```text
generation specification
        |
assetgenerate.py -> unreviewed candidate directory + quarantine marker
        |                    (NEVER feed directly to assetc)
        v
human selection and distribution-rights review (manual, outside CLI)
        |
copy selected PNG to game-art/source/ and declare approved source in assets.json
        |
assetprep.py -> approved/ + external provenance
        |
atlasprep.py (optional) -> pack-ready/ -> assetc -> game.pak
```

**The deterministic offline mock is the default. An optional, explicitly paid OpenAI Images provider is also available.** It produces obviously synthetic checkerboard PNGs to exercise the provider contract and costs $0. The mock never makes network calls. The paid provider requires an explicit CLI flag and OPENAI_API_KEY, and makes only one request without retries. See [real assets](CREATING_REAL_ASSETS.md). Real provider adapters can implement `GenerationProvider` but should not be registered until separately reviewed, budgeted, tested, and configured.

## Generate a test candidate batch

Save `game-art/requests/torch.json`:

```json
{
  "version": 1,
  "job_id": "platformer-torch",
  "provider": "mock",
  "model": "checker-v1",
  "prompt": "An original wall torch for a ruined temple platformer",
  "width": 64,
  "height": 64,
  "candidate_count": 3,
  "seed": 42,
  "budget": {"max_candidates": 3, "max_cost_usd": 0}
}
```

Run from the asset-pipeline repository (adapt paths to sibling checkouts):

```sh
python3 tools/assetgenerate.py game-art/requests/torch.json \
  build/unreviewed/platformer-torch
# Explicitly rebuild the same destination:
python3 tools/assetgenerate.py game-art/requests/torch.json \
  build/unreviewed/platformer-torch --replace
```

The destination contains `candidate-001.png`, etc., and the marker
`.asset-pipeline-quarantine.json`. The deterministic receipt records job ID, provider, model, SHA-256 prompt/spec fingerprints, each image hash/size/dimensions/seed/cost, and **unreviewed, approved=false, eligible_for_packaging=false**. It does not contain the prompt text.

**Do not commit private prompts into a public repository.** Keep request specs outside version control if they contain confidential material. The prompt digest provides provenance linkage without distributing the prompt.

## Approval boundary

Candidates are never automatically promoted. Inspect candidate images, reject bad results, and independently verify the image-generation provider's commercial-use terms and source-image rights. To approve one, *manually copy* the chosen PNG from the candidate directory to a distinct, reviewed `source/` directory, and add an entry to the existing assetprep manifest:

```json
{
  "key": "sprites/torch.png",
  "kind": "png",
  "approved": true,
  "source": {
    "adapter": "ai-import",
    "file": "source/reviewed-torch.png",
    "provider": "actual generation provider, not the mock fixture",
    "model": "exact model/version",
    "prompt_sha256": "actual 64-character lowercase SHA-256",
    "rights_note": "Reviewed distribution terms and restrictions",
    "rights_reviewed": true,
    "reviewed_by": "responsible maintainer"
  }
}
```

The placeholder must be replaced with real values; the mock provider's checkerboards are not intended as production art. The manual step is deliberate: it ensures generation cannot silently approve its own outputs. No generator writes to `approved/`, an assetprep manifest, or the renderer.

`assetc` now **refuses to pack any input directory containing `.asset-pipeline-quarantine.json`, including in nested folders**. The guard runs before the incremental pack-cache shortcut and preserves an existing output pack. It is a build-time safety check, not a cryptographic security guarantee; copying or removing the marker defeats the guard, which is why approval and separation of directories still matter.

## Provider port and budgets

`tools/assetgenerate.py` defines `GenerationRequest`, `GeneratedImage`, and the `GenerationProvider` protocol. A registered provider must report `estimate_cost_usd` and `generate_candidates`. The CLI validates the request, count/size/seed/cost budget, provider/model identity, generated PNG structure/dimensions and resulting candidate count **before publishing** the batch. Unexpected or over-budget outputs fail without replacing a previously completed batch.

The preflight estimate prevents a known over-budget call; postflight validation detects a provider violating its advertised estimate but cannot undo any costs already incurred. Future paid adapters need their own hard request budget, rate limits, retry policy and cancellation behavior.

The mock uses a deterministic seed and prompt-derived checker pattern, so repeated runs are byte-identical. Real hosted models may not reproduce byte-identical outputs; preserve the **approved resulting bytes** and provenance rather than promising reproducible model generation.

See [candidate visual review](CANDIDATE_REVIEW.md) for the optional contact-sheet tool, which validates source hashes and generates an unapproved comparison image.

## Scope and next work

- Add a real provider adapter only when an explicit backend and budget are selected; never dynamically import an arbitrary module named in an untrusted JSON spec.
- Add candidate contact-sheet previews and selection UX while keeping manual approval and rights review.
- The renderer and shipping games stay independent of provider SDKs, model credentials, Python and network inference.
- Platformer end-to-end pack-and-render acceptance remains tracked in issue #3; SimGame culture/biome visual catalogs remain tracked in issue #4.
