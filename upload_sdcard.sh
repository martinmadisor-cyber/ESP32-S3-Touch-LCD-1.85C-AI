#!/bin/bash

# upload_sdcard.sh
# Usage: ./upload_sdcard.sh <ESP32_IP>

if [ -z "$1" ]; then
  echo "Error: No IP address provided."
  echo "Usage: $0 <ESP32_IP>"
  echo "Example: $0 192.168.0.32"
  exit 1
fi

# Ensure the IP has the http:// prefix
if [[ "$1" != http* ]]; then
  ESP32_IP="http://$1"
else
  ESP32_IP="$1"
fi

echo "Uploading files to ESP32 at $ESP32_IP..."
echo "----------------------------------------"

# Function to upload a file
upload_file() {
    local file=$1
    local dest_path=$2

    if [ -f "$file" ]; then
        echo "Uploading $file to $dest_path ..."
        curl -X POST "$ESP32_IP/upload?path=$dest_path" -F "file=@$file"
        echo -e "\n" # Newline for readability
    else
        echo "Warning: File $file not found, skipping."
    fi
}

# Upload all files from ./sdcard/
for file in ./sdcard/*; do
    if [ -f "$file" ]; then
        filename=$(basename "$file")
        extension="${filename##*.}"
        
        # HTML and CSS files go to /html/, others go to /
        if [ "$extension" = "html" ] || [ "$extension" = "css" ]; then
            dest_path="/html/"
        else
            dest_path="/"
        fi
        
        upload_file "$file" "$dest_path"
    fi
done

# Upload srmodels.bin from ./data/
upload_file "./data/srmodels.bin" "/"

echo "----------------------------------------"
echo "Upload process completed!"
