#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$root"

while IFS=$'\t' read -r checkout revision patch; do
    test "$(git -C "$checkout" rev-parse --show-toplevel)" = "$root/$checkout"
    if ! git -C "$checkout" cat-file -e "$revision^{commit}" 2>/dev/null; then
        git -C "$checkout" fetch origin "$revision"
    fi
    if [[ $(git -C "$checkout" rev-parse HEAD) != "$revision" ]]; then
        git -C "$checkout" checkout --detach "$revision"
    fi
done < config/revisions.tsv

while IFS=$'\t' read -r checkout revision patch; do
    if ! git -C "$checkout" apply --reverse --check "$root/patches/$patch" 2>/dev/null; then
        git -C "$checkout" apply --check "$root/patches/$patch"
    fi
done < config/revisions.tsv

while IFS=$'\t' read -r checkout revision patch; do
    if ! git -C "$checkout" apply --reverse --check "$root/patches/$patch" 2>/dev/null; then
        git -C "$checkout" apply "$root/patches/$patch"
    fi
done < config/revisions.tsv

cp -an overlay/. src/
sed "s@../overlay/deploy/berry-shell-bar/Noto-Sans-CJK-OFL.txt@berry-shell-bar/Noto-Sans-CJK-OFL.txt@" docs/BERRY_BROWSER_CJK.md > src/deploy/BERRY_BROWSER_CJK.md
install -D -m 0644 config/args.gn src/out/qnx-arm/args.gn
