# Visual review of generated candidates

Last edited: 2026-11-19. This is a **review aid**, not automatic art approval.

The approved pipeline now has four authoring steps:

```text
assetgenerate --(unreviewed PNGs)--> candidate_review --(contact sheet)--> human inspection
                                                                              |
                                          copy selected PNG and record rights-review metadata
                                                                              |
                                              assetprep -> atlasprep -> assetc -> game
```

The review step never writes a file to an approved-art directory, never edits
`assets.json`, and does not make a judgment about commercial-use rights.

## Generate a contact sheet

`candidate_review.py` requires Python 3.9+ and Pillow 12.0.0 (or a compatible
Pillow version). Use the same pinned version for byte-identical PNG output.

```sh
python3 -m pip install Pillow==12.0.0
python3 tools/assetgenerate.py game-art/requests/torch.json \
  build/unreviewed/torch
python3 tools/candidate_review.py build/unreviewed/torch \
  build/review/torch --columns 3 --tile-size 192
# Explicit rebuild:
python3 tools/candidate_review.py build/unreviewed/torch \
  build/review/torch --replace
```

Outputs inside `build/review/torch`:

- `contact-sheet.png`: a checkerboard-backed, side-by-side comparison of the
  candidate PNGs. Every tile is labeled with its candidate filename and short
  SHA-256 fingerprint. Resampling changes **only the preview**, not the sources.
- `.asset-pipeline-quarantine.json`: a deterministic sidecar with image
  fingerprints, dimensions, source receipt hash and
  `state: unreviewed / approved: false / eligible_for_packaging: false`.

A candidate is rejected when the quarantine marker is absent/invalid, source
PNG hashes or dimensions differ from the receipt, PNG decoding fails, or
unlisted PNG files are found. The review output itself carries the quarantine
marker and is likewise rejected by `assetc` if accidentally used as pack input.

## Human approval step

Look at `contact-sheet.png`, inspect the full-resolution source PNGs for defects,
and independently review generation provider terms and source rights.

**Only after approval**, manually copy the selected image into your game's
`source/` artwork directory (outside quarantine) and add the corresponding
`ai-import` entry with `approved: true`, provider/model/prompt SHA-256 and a
named rights reviewer in `assets.json`. Then run `assetprep` and optional
`atlasprep` before packaging. The generator and reviewer never promote art.

The mock provider currently ships only synthetic checkerboards to test this
pipeline, not AI images or releasable art. Real hosted/local model adapters are
separate future work under issue #4. Native platformer pack-and-render validation
remains issue #3.

**Privacy:** output receipts and contact-sheet labels do not include raw prompts.
Generation request files may contain prompts: do not commit confidential prompts
to public repositories.

**Limits:** 1–16 PNGs per batch; up to 8192 pixels in either source dimension,
64 MiB per PNG, and optional preview tile size 96–384 px with 1–4 columns.
No subjective review, image enhancement or prompt inference occurs.
