#!/usr/bin/env bash
# Format (or check) all tracked C/C++ sources with clang-format using the repo .clang-format.
#
#   scripts/clang_format.sh            # format in place
#   scripts/clang_format.sh --check    # dry-run; non-zero exit if anything is unformatted
#
# Note: CI gates PRs diff-based (only changed lines); this script operates on the whole
# tree and is intended for a deliberate "format everything" pass or a full-tree check.
set -euo pipefail
cd "$(dirname "$0")/.."

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
if ! command -v "$CLANG_FORMAT" >/dev/null 2>&1; then
    echo "error: $CLANG_FORMAT not found on PATH" >&2
    exit 127
fi

mode="format"
if [[ "${1:-}" == "--check" || "${1:-}" == "check" ]]; then
    mode="check"
fi

# All tracked C/C++ sources, excluding vendored third_party.
files="$(git ls-files '*.h' '*.hpp' '*.cpp' '*.cc' '*.mm' | grep -vE '^third_party/' || true)"
if [[ -z "$files" ]]; then
    echo "no source files found"
    exit 0
fi

if [[ "$mode" == "check" ]]; then
    echo "$files" | xargs "$CLANG_FORMAT" --dry-run --Werror
    echo "clang-format check passed"
else
    echo "$files" | xargs "$CLANG_FORMAT" -i
    echo "formatted $(echo "$files" | wc -l | tr -d ' ') files"
fi
