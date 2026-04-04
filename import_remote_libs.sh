#!/bin/zsh

# import_remote_libs.sh
# Purpose: Manually pull missing libraries from GitHub into the Arduino/ folder.

DEST_DIR="./Arduino"
mkdir -p "$DEST_DIR"

echo "🚀 Fetching libraries from GitHub..."

# List of libraries [RepoURL] [TargetFolderName]
libs=(
    "https://github.com/moononournation/Arduino_GFX.git" "GFX_Library_for_Arduino"
    "https://github.com/schreibfaul1/ESP32-audioI2S.git" "ESP32-audioI2S-master"
    "https://github.com/gilmaimon/ArduinoWebsockets.git" "ArduinoWebsockets"
)

# Iterate and clone/update
for ((i=1; i<=${#libs}; i+=2)); do
    url="${libs[$i]}"
    folder="${libs[$i+1]}"
    target="$DEST_DIR/$folder"
    
    if [[ -d "$target" ]]; then
        echo "🔄 Updating $folder..."
        cd "$target" && git pull && cd ../..
    else
        echo "📥 Cloning $folder..."
        git clone --depth 1 "$url" "$target"
    fi
done

echo "✅ All remote libraries imported to $DEST_DIR."
