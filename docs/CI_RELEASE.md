# CI & Release Checklist

Last edited: 2026-11-14. Last reviewed: 2026-11-14 (Linux validation below).

This document describes recommended CI jobs and a release checklist for the Asset Pack Pipeline project.

## Recommended CI jobs

1. Build (Release) for supported compilers
   - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
   - `cmake --build build -j`

2. Unit tests
   - `ctest --test-dir build --verbose`

3. Static analysis (clang-tidy)
   - Configure `-DENABLE_CLANG_TIDY=ON` and run a `build-tidy` build to collect warnings.
   - Supply project-specific filters/checks in `.clang-tidy` or the invocation when needed.

4. AddressSanitizer (ASan) run
   - Configure an ASan build (e.g. `-DCMAKE_CXX_FLAGS=-fsanitize=address -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address`) or use a dedicated `build-asan` profile.
   - Build and run tests under ASan to detect runtime memory issues.

5. Optional: Memory sanitizer, UBSan, or MSAN jobs for more coverage.

6. Packaging job
   - Build and `cmake --install` to a temporary prefix and verify `find_package` works from that prefix.

## Release checklist (manual steps)

1. Update `CMakeLists.txt` project `VERSION` and tag accordingly (semantic versioning recommended).
2. If you make wire-format changes, increment the `format_version` constant in `include/pak_format.h` and document the change in `docs/PAK_FORMAT.md` and `CHANGELOG.md`.
3. Run full CI (build, tests, clang-tidy, ASan) and fix any regressions.
4. Create a signed tag and GitHub release with release notes and CHANGELOG entry.
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
