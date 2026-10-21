#!/usr/bin/env bash
# Tests sur PC sans PlatformIO : gcc + Unity (cloné une fois dans .pio/unity).
# Usage : scripts/run_tests.sh
set -e
cd "$(dirname "$0")/.."
U=.pio/unity
[ -d "$U" ] || git clone -q --depth 1 https://github.com/ThrowTheSwitch/Unity "$U"
mkdir -p .pio/hosttest
FLAGS="-std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-sign-compare -DHOST_TEST
       -I test/stubs -I include -I lib/fatfs -I lib/bme68x -I lib/bme280 -I lib/cesi_grove/include -I $U/src"
status=0
for t in test/test_*/; do
  name=$(basename "$t")
  echo "=== $name"
  gcc $FLAGS "$t/test_main.c" "$U/src/unity.c" -o ".pio/hosttest/$name" -lm 2>&1 | grep -v "lib/fatfs" || true
  ".pio/hosttest/$name" | grep -v ":PASS" || status=1
done
exit $status
