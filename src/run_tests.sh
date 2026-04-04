#!/bin/bash

# run_tests.sh (Firmware) - Convenience script to run PlatformIO tests

# Switch to project root if running from src
if [[ $PWD == *"/src" ]]; then
  cd ..
fi

echo "--- Running Native (Desktop simulation) Tests ---"
pio test -e native

echo ""
echo "--- Running Compilation Verification (Master) ---"
pio run -e master
