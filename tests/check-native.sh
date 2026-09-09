#!/bin/sh
# Builds every test harness with a native compiler and checks the failures against
# tests/unported.txt, then runs the harnesses that built.
#
# The point is that nothing is skipped. A harness that is expected to fail is attempted and
# its failure is accounted for; the check fails when the set of failures differs from the
# list, in either direction. A harness that starts building is progress and its line comes
# out of the list.
#
# Usage: tests/check-native.sh <build directory>

set -eu

build=${1:?usage: tests/check-native.sh <build directory>}
here=$(dirname "$0")
unported="${here}/unported.txt"

# Attempted, not skipped: -k 0 keeps going so one failure does not hide the rest. The build
# is expected to fail, so its status is read from what failed rather than from its exit code.
status=0
cmake --build "${build}" --target harnesses -- -k 0 > "${build}/harnesses.log" 2>&1 || status=$?

grep '^FAILED:' "${build}/harnesses.log" > "${build}/failed-steps.txt" || true

# Ninja names the failing target in the object path it was writing or the file it was
# linking, and prefixes the line with its exit code from 1.12 onwards.
sed -n 's|^FAILED:.*CMakeFiles/\([A-Za-z0-9_]*\)\.dir.*|\1|p;s|^FAILED:.*test-bin/\([A-Za-z0-9_]*\).*|\1|p' \
    "${build}/failed-steps.txt" | sort -u > "${build}/failed-targets.txt"

# Only a harness failure is this script's business. Anything else means the build did not
# get far enough for the comparison below to mean anything.
grep -ho 'opents_add_test([A-Za-z0-9_]*' "${here}"/*/CMakeLists.txt \
    | sed 's|opents_add_test(||' | sort -u > "${build}/harnesses.txt"

comm -12 "${build}/failed-targets.txt" "${build}/harnesses.txt" > "${build}/failed.txt"
comm -23 "${build}/failed-targets.txt" "${build}/harnesses.txt" > "${build}/failed-other.txt"

if [ -s "${build}/failed-other.txt" ]; then
    echo "The build failed outside the harnesses, so the list below proves nothing:"
    sed 's|^|    |' "${build}/failed-other.txt"
    exit 1
fi

if [ "${status}" -ne 0 ] && [ ! -s "${build}/failed-steps.txt" ]; then
    echo "The build did not complete and named no failing step; see ${build}/harnesses.log."
    exit 1
fi

grep -v '^#' "${unported}" | awk 'NF {print $1}' | sort -u > "${build}/expected.txt"

if ! diff -u "${build}/expected.txt" "${build}/failed.txt" > "${build}/unported.diff"; then
    echo "The harnesses that fail natively are not the ones tests/unported.txt lists."
    echo "A line to remove means a harness now builds; a line to add means one stopped."
    echo
    cat "${build}/unported.diff"
    exit 1
fi

echo "$(wc -l < "${build}/expected.txt" | tr -d ' ') harnesses unported, as listed."

# The listed harnesses have no executable to run, so CTest is asked for the rest by name.
skip=$(grep -v '^#' "${unported}" | awk 'NF {printf "%s%s", sep, $2; sep="|"}')
ctest --test-dir "${build}" --output-on-failure --exclude-regex "^(${skip})\$"
