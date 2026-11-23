# CI & Release Checklist

Last edited: 2026-11-23. Last reviewed: 2026-11-23 (source CMake guard, Git tag/release metadata and hosted CI workflow scope; no local native build).

This is a **recommended/manual release verification checklist**, not a
statement that every check below is implemented as an automatic PR gate.
Current source admission is **Clang/AppleClang only** (`CMakeLists.txt` rejects
GCC and MSVC). The path-scoped GitHub Actions workflows check asset-source,
atlas, candidate and image-provider contracts. Relevant C++/CMake changes also
trigger Clang pak/runtime tests with Zstd disabled and enabled (10 and 11 CTests),
plus a Zstd-enabled Clang **ASan + UBSan** lane (11 CTests). These checks are
not a substitute for the complete release, installed-consumer, downstream
integration and sanitizer-configuration validation matrix. A `v1.1.0` tag exists, but no GitHub Release entry was
published at the 2026-11-22 audit. This document does **not** publish one.

## Recommended CI jobs

1. Build (Release) with a supported Clang toolchain
   - `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++`
   - `cmake --build build -j`

2. Unit tests
   - `ctest --test-dir build --verbose`

3. Static analysis (clang-tidy)
   - Configure `-DENABLE_CLANG_TIDY=ON` and run a `build-tidy` build to collect warnings.
   - Supply project-specific filters/checks in `.clang-tidy` or the invocation when needed.

4. AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan)
   - For C++/CMake changes, `.github/workflows/pak-sanitizers.yml` now runs a
     Zstd-enabled Clang Debug build with ASan, UBSan, leak detection and the
     11 pak/runtime/packer CTests on GitHub-hosted Ubuntu.
   - For releases, explicitly validate any additional required compiler,
     configuration, dependency and installed-consumer sanitizer combinations.
     The path-scoped CI result is not a complete sanitizer certification.
   - Build and run tests under ASan to detect runtime memory issues.

5. Optional: Memory sanitizer, UBSan, or MSAN jobs for more coverage.

6. Packaging job
   - Build and `cmake --install` to a temporary prefix and verify `find_package` works from that prefix.

## Release checklist (manual steps)

1. Update `CMakeLists.txt` project `VERSION` and tag accordingly (semantic versioning recommended).
2. If you make wire-format changes, increment the `format_version` constant in `include/pak_format.h` and document the change in `docs/PAK_FORMAT.md` and `CHANGELOG.md`.
3. Execute the complete relevant **release validation matrix** (Clang build,
   tests, clang-tidy, ASan, installed consumer and integration as applicable),
   running the non-automated checks explicitly and recording their outcomes.
   A green path-scoped GitHub Actions result alone does not certify this matrix.
4. After the required owner publication approval, create the intended tag
   and a **separate GitHub Release** with release notes and CHANGELOG entry;
   do not describe a tag alone as a published GitHub Release.
5. Optionally publish release artifacts (archives) with prebuilt binaries for common platforms.
6. Update documentation and examples in `README.md` if any API or usage has changed.

## Publishing

- After tagging and building, downstream users can `find_package` the installed artifacts (see README). When publishing, provide a short example in the release notes showing how to `find_package` the installed package.

## Notes

- Prefer reproducible builds: use tagged dependencies in `FetchContent` declarations.
- Keep wire-format changes backward-compatible where possible (append-only fields and `toc_entry_size` support).

## Review validation — 2026-11-14

- Standalone Clang Debug/Werror: 11 CTest suites pass, including six Python CLI scenarios.
- AddressSanitizer + UndefinedBehaviorSanitizer + leak detection: all 11 suites pass.
- Without zstd: all 10 applicable suites pass, including failed-compression output preservation.
- The six new CLI scenarios each fail against the preceding implementation.
- Installed-package consumer and engine integration are checked before publication.

The source uses little-endian wire structs. Windows replacement uses the native
replace-existing operation; Windows compilation/runtime were not exercised here.
No version bump or release tag is implied by committing and pushing these changes.
