#!/usr/bin/env bash
# Compile and run the host tests for logic that can be tested without the ESP32.
# No ESP-IDF needed - the few IDF headers used are stubbed in ./stubs.
set -euo pipefail

cd "$(dirname "$0")"
outputDir="$(mktemp -d)"
trap 'rm -rf "$outputDir"' EXIT

g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -g -fsanitize=address,undefined \
    -I stubs \
    -I ../components/gpio \
    test_evaluateSwitch.cpp ../components/gpio/gpio_evaluateSwitch.cpp \
    -o "$outputDir/test_evaluateSwitch"

"$outputDir/test_evaluateSwitch"
