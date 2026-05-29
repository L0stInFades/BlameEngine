#!/usr/bin/env bash
# Incremental clang-format gate: verify only the lines a change TOUCHES conform to
# .clang-format. Legacy code is grandfathered; touching a line means formatting it.
#
#   scripts/clang_format_diff.sh [base-ref]   # default base: origin/master, else HEAD~1
#
# Exit non-zero (and print the offending diff) if any changed line is mis-formatted.
set -uo pipefail
cd "$(dirname "$0")/.."

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
if ! command -v "$CLANG_FORMAT" >/dev/null 2>&1; then
    echo "error: $CLANG_FORMAT not found on PATH" >&2
    exit 127
fi

base="${1:-}"
if [ -z "$base" ]; then
    if git rev-parse --verify --quiet origin/master >/dev/null; then
        base="origin/master"
    else
        base="$(git rev-parse --verify --quiet HEAD~1 || git rev-parse HEAD)"
    fi
fi
echo "Incremental clang-format check against: $base"

files="$(git diff --name-only --diff-filter=ACMR "$base" -- \
    '*.h' '*.hpp' '*.cpp' '*.cc' '*.mm' | grep -vE '^third_party/' || true)"
if [ -z "$files" ]; then
    echo "No C/C++ files changed."
    exit 0
fi

status=0
for f in $files; do
    [ -f "$f" ] || continue
    # Build --lines=START:END arguments from the added/changed hunks of this file.
    ranges="$(git diff -U0 "$base" -- "$f" | awk '
        /^@@/ {
            # @@ -old +newStart[,newCount] @@
            if (match($0, /\+[0-9]+(,[0-9]+)?/)) {
                spec = substr($0, RSTART + 1, RLENGTH - 1)
                c = index(spec, ",")
                if (c > 0) { start = substr(spec, 1, c - 1) + 0; cnt = substr(spec, c + 1) + 0 }
                else       { start = spec + 0; cnt = 1 }
                if (cnt > 0) printf " --lines=%d:%d", start, start + cnt - 1
            }
        }')"
    [ -z "$ranges" ] && continue
    # clang-format reformats only those lines and emits the whole file; diff against original.
    if ! diff -u "$f" <("$CLANG_FORMAT" $ranges "$f") >/tmp/_cf_diff 2>/dev/null; then
        echo "::group::Formatting issues in changed lines of $f"
        cat /tmp/_cf_diff
        echo "::endgroup::"
        status=1
    fi
done

if [ "$status" -ne 0 ]; then
    echo "clang-format check failed. Run: scripts/clang_format.sh (or clang-format -i <file>)" >&2
fi
exit "$status"
