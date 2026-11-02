#!/bin/sh
# Build and run the core tests (no JUCE needed), plain and with sanitizers.
set -e
cd "$(dirname "$0")/.."
g++ -std=c++17 -O2 -Wall -Wextra -o /tmp/mp_test tests/test_core.cpp
/tmp/mp_test
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -o /tmp/mp_test_san tests/test_core.cpp
/tmp/mp_test_san quick
