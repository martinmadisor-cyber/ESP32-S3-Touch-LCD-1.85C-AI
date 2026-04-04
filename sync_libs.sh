#!/bin/zsh

# sync_libs.sh
# Purpose: Sync Arduino libraries from local documents to workspace.

SOURCE_DIR="$HOME/Documents/Arduino/libraries"
DEST_DIR="./Arduino"

# Verify source directory exists
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "❌ Error: Source directory not found at $SOURCE_DIR"
    exit 1
fi

# Clean destination
echo "🧹 Cleaning $DEST_DIR..."
rm -rf "$DEST_DIR"
mkdir -p "$DEST_DIR"

# Copy libraries
echo "📂 Copying libraries from $SOURCE_DIR to $DEST_DIR..."
# Using trailing dot to copy contents of source to destination
cp -R "$SOURCE_DIR/." "$DEST_DIR/"

if [[ $? -eq 0 ]]; then
    echo "✅ Success! Libraries synced to $DEST_DIR."
else
    echo "❌ Error: Failed to copy libraries. Check permissions."
    exit 1
fi
