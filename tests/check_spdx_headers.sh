#!/bin/sh
# Verify every .hpp/.cpp under include/, src/, platform/, tests/, examples/
# carries the two-line SPDX header as its first two lines:
#
#     // Copyright YYYY The CortexFlow Authors
#     // SPDX-License-Identifier: Apache-2.0
#
# Fails with a list of offending files on stderr. Cheap by design — no
# dependencies beyond POSIX find/grep/sed.
set -u

root="${1:-.}"
fail=0

for dir in include src platform tests examples; do
    [ -d "$root/$dir" ] || continue
    for f in $(find "$root/$dir" -type f \( -name '*.hpp' -o -name '*.cpp' \)); do
        line1=$(sed -n '1p' "$f")
        line2=$(sed -n '2p' "$f")
        ok=1
        case "$line1" in
            "// Copyright "*" The CortexFlow Authors") ;;
            *) ok=0 ;;
        esac
        [ "$line2" = "// SPDX-License-Identifier: Apache-2.0" ] || ok=0
        if [ "$ok" -eq 0 ]; then
            if [ "$fail" -eq 0 ]; then
                echo "FAIL: missing or malformed two-line SPDX header in:" >&2
            fi
            echo "  $f" >&2
            fail=1
        fi
    done
done

exit $fail
