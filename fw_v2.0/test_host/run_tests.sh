#!/usr/bin/env bash
# Compile and run the host tests for logic that can be tested without the ESP32.
# No ESP-IDF needed - button.cpp deliberately has no hardware or clock dependency.
set -euo pipefail

cd "$(dirname "$0")"
outputDir="$(mktemp -d)"
trap 'rm -rf "$outputDir"' EXIT

g++ -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I ../main \
    test_button.cpp ../main/button.cpp \
    -o "$outputDir/test_button"

"$outputDir/test_button"
