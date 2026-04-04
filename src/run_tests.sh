#!/bin/bash

# Switch to project root if running from src
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR/.."

echo "--- Running Embedded Tests on Hardware ---"
pio test -e master

echo "--- Running Compilation Verification ---"
pio run -e master
