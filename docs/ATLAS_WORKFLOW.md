# Deterministic sprite atlas stage (v1)

Last edited: 2026-11-19. Status: offline authoring; renderer integration not yet shipped.

Run this step after the reviewed source adapters in [SOURCE_WORKFLOW.md](SOURCE_WORKFLOW.md).
Human and AI artwork both become ordinary approved PNG files. This step never
calls an AI service, uses provider credentials, or changes the .pak wire format.

## Stages

```text
human / AI artwork -> assetprep -> build/approved -> atlasprep -> build/pack-ready -> assetc -> game.pak
```

Approved assets are copied unchanged into pack-ready; new atlas PNG and JSON
metadata are added. The existing C++ assetc packs everything in pack-ready.

## Build-time setup

Python 3.9+ and Pillow are required only to run atlasprep, not to build or
play the game. CI pins Pillow 12.0.0 for repeatable PNG encoder behavior.

```sh
python3 -m pip install Pillow==12.0.0
python3 tools/assetprep.py game-art/assets.json build/approved \
  --provenance build/source-provenance.json --replace
python3 tools/atlasprep.py build/approved game-art/environment-atlas.json \
  build/pack-ready --replace
build/tools/assetc/assetc build/pack-ready build/platformer.pak
```

### Sample layout (game-art/environment-atlas.json)

```json
{
  "version": 1,
  "atlas_key": "atlases/environment.png",
  "metadata_key": "atlases/environment.json",
  "atlas_width": 256,
  "max_height": 2048,
  "padding": 2,
  "sprites": [
    {"id": "torch_idle", "source_key": "sprites/torch-strip.png",
     "frame_width": 32, "frame_height": 32, "frame_count": 4,
     "direction": "horizontal", "pivot": [16, 30]},
    {"id": "rune", "source_key": "sprites/rune.png",
     "frame_width": 32, "frame_height": 32, "pivot": [16, 16]}
  ]
}
```

Source PNGs must already be in build/approved. Strip dimensions must exactly
match the declared frame count; single frames are the default. Pivot is an
integer pixel coordinate relative to the original frame, top-left origin.
No trimming, rescaling, rotation or visual evaluation is performed.

## Outputs and contracts

- atlases/environment.png: RGBA atlas, sorted sprite IDs, ordered frame indices,
  shelf/row placement, optional edge extrusion to prevent texture bleeding.
- atlases/environment.json: versioned runtime metadata, with id, frame, x, y,
  width, height, pivot, source_key and source_sha256. Positions cover the
  unpadded interior of each frame. The origin is top-left.
- The approved source files are copied byte-for-byte, allowing inspection or
  future fallback behavior. No AI prompts, personal attribution or licensing
  notes are inserted into the atlas metadata.

The requested atlas_width is exact; height is compact to the used rows.
For the same source bytes, layout and Pillow version, atlas output is
deterministic. This is a stable simple shelf layout, not an optimal bin packer.

Safety: source and output paths must be separate, source symlinks are rejected,
keys cannot escape via parent traversal, PNGs must decode, and generated keys
must not collide with approved source keys. Output replacement needs --replace.

## Follow-ups

Native platformer integration (#3) will exercise the metadata coordinates,
pivots and alpha in the renderer. Larger optimizations, previews and automatic
generation adapters are tracked in #4. No inference should occur at runtime.
