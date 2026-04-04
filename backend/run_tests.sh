#!/bin/bash

# run_tests.sh - Script to run backend tests for ESP32 AI Chatbot project

# Ensure we are in the backend directory
cd "$(dirname "$0")"

# Activate virtual environment if it exists
if [ -d ".venv" ]; then
    source .venv/bin/activate
fi

# Run pytest with PYTHONPATH set to current directory so modules (api, services, etc.) are found
echo "Running backend tests..."
PYTHONPATH=. pytest tests/ "$@"
