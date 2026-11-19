# Reviewed asset source workflow (v1)

**Status:** source preparation only, not an image-generation service. Last edited: 2026-11-19.

This is an upstream stage for the existing C++ `assetc` packer. It is intentionally independent of MyEngine2, the platformer, and SimGame; none of those projects should need to know which artist or model supplied an image.

    human artist's PNG  ──> human-file ─┐
                                      ├─> review + verify PNG ─> staged keys ─> assetc ─> .pak
    generated PNG       ──> ai-import ─┘                 │
                                                        └─> provenance.json (OUTSIDE pack)

**Scope:** PNG imports with identical bytes, deterministic staged paths and provenance, and a manual approval gate. v1 does **not** generate artwork, decode pixels, make spritesheets/atlases, ensure visual consistency, or perform a legal rights determination. Provider API calls and model secrets are not in this tool. The source adapters are extension points for a future generation orchestrator.

## Run

Requires Python 3.9+ (uses the standard library only). Use the root of a consumer asset workspace to hold a versioned manifest and its source PNGs:

    game-art/
      assets.json
      source/
        torch-human.png
        torch-ai.png

Example `assets.json` (the source PNG files must already exist):

```json
{
  "version": 1,
  "assets": [
    {
      "key": "sprites/torch.png",
      "kind": "png",
      "approved": true,
      "image": {"width": 64, "height": 64, "alpha": true},
      "source": {
        "adapter": "human-file",
        "file": "source/torch-human.png",
        "creator": "Artist name",
        "license": "CC0-1.0"
      }
    },
    {
      "key": "sprites/torch-variant.png",
      "kind": "png",
      "approved": true,
      "image": {"width": 64, "height": 64, "alpha": true},
      "source": {
        "adapter": "ai-import",
        "file": "source/torch-ai.png",
        "provider": "selected provider",
        "model": "record exact model/version",
        "prompt_sha256": "9b57195e089942d034368b7d8b59cad641110c4a0b3dd1eb91d5d780f43a426c",
        "rights_note": "Document which terms permit distribution and any restrictions",
        "rights_reviewed": true,
        "reviewed_by": "maintainer"
      }
    }
  ]
}
```

The example prompt hash is the SHA-256 of the text `torch prompt`, not a real production prompt.

```sh
python3 tools/assetprep.py game-art/assets.json build/approved \
  --provenance build/asset-provenance.json
# Repeatable rebuild, replacing only the generated staging directory:
python3 tools/assetprep.py game-art/assets.json build/approved \
  --provenance build/asset-provenance.json --replace
build/tools/assetc/assetc build/approved build/game-assets.pak
```

Run these commands from the asset-pipeline repository or adapt the paths to the checkout. `assetc` is built separately with CMake. In a game repository, call the source tool from its sibling checkout; never copy the tool into the engine.

**Asset identity:** the manifest's `key` is the staged relative file path. `assetc` computes the runtime asset ID using XXH64 of that exact case-preserved key. Changing the key changes the asset ID. Do not put image source metadata or generation prompts in the pack.

## Validation and provenance

- Both adapters require per-asset `approved: true`. A human-file asset records `creator` and `license`; an AI-import asset records `provider`, `model`, SHA-256 of its prompt, `rights_note`, a named reviewer, and `rights_reviewed: true`. These fields document a review; their presence is **not** proof of legal rights.
- Source files must be relative to the manifest directory, cannot escape via `..` or symlink targets, and cannot come from the staging tree. Keys are normalized POSIX paths; duplicates and case-folding collisions are rejected.
- PNG signature, IHDR dimensions/color type, chunk bounds/CRCs and IDAT/IEND presence are checked. `image.alpha` checks alpha **capability** (not actual transparent pixels). Full image decoding and visual approval remain separate tasks.
- Staging is built in a temporary directory, and an invalid manifest leaves an existing staged tree unchanged. Replacing an existing stage requires `--replace`. Keep staging isolated from original source trees.
- The deterministic sidecar contains asset key, SHA-256 of source bytes, size, dimensions and provenance. It must stay **outside** the `assetc` input directory. Audit it and store it with build evidence as needed; it may contain creator/provider details.
- Do not put API tokens, private prompts, or personally sensitive material into a manifest or provenance note. AI model/licensing terms and approvals must be checked before commercial distribution.

## Ownership boundary

| Layer | Owner |
|---|---|
| Source acquisition, image generation, human editing | Artist or future provider adapters / orchestrator |
| Reviewed PNG imports and provenance | `myengine-asset-pipeline/tools/assetprep.py` |
| Compression, hashes, pack wire format | Existing `assetc` + `asset_runtime` |
| Runtime rendering and loading | MyEngine2 |
| Art direction, catalog of keys, approvals | Platformer / SimGame repositories |

Neither `asset_runtime` nor `assetc` needs to depend on Python, an AI model or an image API.

## Roadmap, not shipped in v1

1. **Consumer proof:** stage and pack one approved platformer torch/rune/door asset set, load it by stable IDs and verify on screen.
2. **Image processing:** optional pixel decoding, dimension conversion, transparent padding, sheet slicing, atlas and sprite metadata, deterministic transformations and visual previews.
3. **Generation adapters:** a configurable provider interface (local and hosted models) that outputs *candidates* into a quarantined directory, with retry/rate/cost budgets and no automatic approval.
4. **SimGame:** procedural material/theme families and asset variants selected from simulation data, without runtime inference.

Future generation must not bypass review/rights gates. Reproducible releases package approved outputs, not provider network calls.
