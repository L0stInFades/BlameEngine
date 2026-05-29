# Code Quality Tooling

Industrial-grade C++ quality gates for Blame Engine: formatting, static analysis,
sanitizers, strict warnings, and CI enforcement. See also `CODE_STYLE.md` for conventions.

## TL;DR

```bash
scripts/clang_format.sh            # auto-format all tracked sources
scripts/clang_format.sh --check    # verify formatting (whole tree)
scripts/clang_tidy.sh path/to.cpp  # lint specific files

cmake --preset asan                # configure ASan + UBSan test build
cmake --build out/build/asan --target test_runtime
ctest --test-dir out/build/asan -R RuntimeTest --output-on-failure
```

## Formatting — clang-format

`.clang-format` is tuned to the existing style (4-space indent, 120-column limit,
attached/K&R braces, pointer-left `T* p`, `template<...>` with no space, no include
reordering). It is therefore close to a no-op on current code and defines the canonical
style going forward.

- Format your changes: `scripts/clang_format.sh` (or `clang-format -i <file>`).
- **CI policy (incremental / line-based):** the `format` job runs
  `scripts/clang_format_diff.sh`, which checks only the *lines a change touches* (via
  `clang-format --lines`). Legacy lines are grandfathered; lines you add or edit must be
  formatted. This is the standard incremental-adoption approach and avoids a giant one-time
  reformat. Run it locally with `scripts/clang_format_diff.sh [base-ref]`.
- To adopt repo-wide in one deliberate pass: run `scripts/clang_format.sh` and commit the
  result on its own (no logic changes mixed in).

## Static analysis — clang-tidy

`.clang-tidy` enables `bugprone-*`, `performance-*`, `modernize-*`, `readability-*`,
`cppcoreguidelines-*`, `clang-analyzer-*`, and `misc-*`, with the noisy/over-strict checks
disabled, plus naming rules from `CODE_STYLE.md` (PascalCase types/methods, camelBack
locals, `member_` trailing underscore).

- Run locally: `scripts/clang_tidy.sh [files...]` (generates a compile database under
  `out/build/tidy`). Needs `clang-tidy` (`brew install llvm`).
- Enable during a normal build: `cmake -DNEXT_ENABLE_CLANG_TIDY=ON ...`.

## Sanitizers — ASan + UBSan

The `asan` preset builds the tests with AddressSanitizer + UndefinedBehaviorSanitizer and
`-fno-sanitize-recover=all` (any UB aborts). Use it to catch memory errors and UB — in
particular it guards the archetype ECS, whose component migration is pointer-invalidation
prone.

```bash
cmake --preset asan
cmake --build out/build/asan --target test_runtime -j
ctest --test-dir out/build/asan -R RuntimeTest --output-on-failure
```

Enable on any build with `-DNEXT_SANITIZE=address,undefined` (or `thread`, etc.). Sanitizer
flags are applied globally so the whole program is instrumented.

## Strict warnings

`next_apply_warnings(<target>)` (from `cmake/NextQuality.cmake`) applies `-Wall -Wextra`
(`/W4 /permissive-` on MSVC). It is rolled out target-by-target (currently `next_runtime`).
Flip `-DNEXT_WARNINGS_AS_ERRORS=ON` to fail the build on any warning once a target is clean.

## Other static analysis — cppcheck

CI runs `cppcheck` (warning/performance/portability) as an advisory job. Run locally:

```bash
cppcheck --enable=warning,performance,portability --std=c++17 -i third_party engine tools game
```

## CI (`.github/workflows/code-quality.yml`)

| Job | Runner | Gate |
|-----|--------|------|
| `format` | ubuntu | clang-format on changed files (blocking) |
| `sanitizers` | macOS | build + run unit tests under ASan/UBSan (blocking) |
| `cppcheck` | ubuntu | static analysis (advisory) |

The `sanitizers` job also closes a prior gap: CI previously built the engine but never ran
the unit-test suite. It now runs the core suites on every push/PR under sanitizers.
