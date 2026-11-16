#!/usr/bin/env bash
set -euo pipefail

shell=${1:-./bin/myshell}
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

output=$(printf 'echo PA03-smoke-ok\nMSG=hello\nset\necho $MSG\necho pipeline | tr a-z A-Z\necho redirected > %s/out\ncat < %s/out\necho first; echo second\nif true\nthen\necho IF-TRUE\nelse\necho IF-FALSE\nfi\nif false\nthen\necho BAD-TRUE\nelse\necho IF-FALSE-BRANCH\nfi\nexit\n' \
    "$tmpdir" "$tmpdir" | "$shell" 2>&1)

for expected in PA03-smoke-ok MSG=hello hello PIPELINE redirected first second IF-TRUE IF-FALSE-BRANCH; do
    if ! printf '%s\n' "$output" | grep -Fq "$expected"; then
        printf 'Smoke check failed: missing output: %s\n\n%s\n' "$expected" "$output" >&2
        exit 1
    fi
done

if printf '%s\n' "$output" | grep -Fq 'BAD-TRUE'; then
    printf 'Smoke check failed: false condition executed its then block\n' >&2
    exit 1
fi

echo 'PA-03 smoke checks passed.'
