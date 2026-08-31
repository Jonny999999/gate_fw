#!/usr/bin/env bash
# Compile and run the host tests for logic that can be tested without the ESP32.
# No ESP-IDF needed - button.cpp and blink.cpp deliberately have no hardware or clock
# dependency, which is exactly what makes them testable here.
set -euo pipefail

cd "$(dirname "$0")"
outputDir="$(mktemp -d)"
trap 'rm -rf "$outputDir"' EXIT

failed=0
for testName in button blink; do
    g++ -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
        -I ../main \
        "test_${testName}.cpp" "../main/${testName}.cpp" \
        -o "$outputDir/test_${testName}"
    "$outputDir/test_${testName}" || failed=1
    echo
done

exit $failed
