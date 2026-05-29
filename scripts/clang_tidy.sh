#!/usr/bin/env bash
# Run clang-tidy over engine sources using a compile database and the repo .clang-tidy.
#
#   scripts/clang_tidy.sh                 # tidy all engine/tools/game .cpp (slow; legacy noisy)
#   scripts/clang_tidy.sh path/to/a.cpp   # tidy specific files (recommended for iterating)
#
# A compile database is generated under out/build/tidy if one is not already present.
set -euo pipefail
cd "$(dirname "$0")/.."

CLANG_TIDY="${CLANG_TIDY:-clang-tidy}"
if ! command -v "$CLANG_TIDY" >/dev/null 2>&1; then
    echo "error: $CLANG_TIDY not found (try: brew install llvm, or use \$CLANG_TIDY)" >&2
    exit 127
fi

BUILD_DIR="${TIDY_BUILD_DIR:-out/build/tidy}"
if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo "generating compile database in $BUILD_DIR ..."
    cmake -S . -B "$BUILD_DIR" -G "Unix Makefiles" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DBUILD_TESTS=OFF -DBUILD_TOOLS=ON -DBUILD_TERMINAL=OFF >/dev/null
fi

if [[ "$#" -gt 0 ]]; then
    files="$*"
else
    files="$(git ls-files 'engine/*.cpp' 'tools/*.cpp' 'game/*.cpp' | grep -vE '^third_party/' || true)"
fi
[[ -z "$files" ]] && { echo "no source files"; exit 0; }

# On macOS, AppleClang's libc++/SDK headers aren't on LLVM clang-tidy's default search
# path; without the SDK sysroot clang-tidy mis-parses ("cstdint not found") and emits
# false positives. Pass the active SDK sysroot so the parse is complete and accurate.
EXTRA_ARGS=()
if [[ "$(uname)" == "Darwin" ]]; then
    SDK_PATH="$(xcrun --show-sdk-path 2>/dev/null || true)"
    [[ -n "$SDK_PATH" ]] && EXTRA_ARGS+=("--extra-arg=-isysroot" "--extra-arg=$SDK_PATH")
fi

for f in $files; do
    "$CLANG_TIDY" -p "$BUILD_DIR" "${EXTRA_ARGS[@]}" --quiet "$f"
done
echo "clang-tidy finished"
