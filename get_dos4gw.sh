#!/bin/bash
# Script to locate and copy DOS4GW.EXE for running CitySim

echo "Searching for DOS4GW.EXE..."

# Try common Watcom locations
SEARCH_PATHS=(
    "$WATCOM/binw/dos4gw.exe"
    "$WATCOM/binnt/dos4gw.exe"
    "/opt/watcom/binw/dos4gw.exe"
    "/usr/bin/watcom/binw/dos4gw.exe"
    "/usr/local/watcom/binw/dos4gw.exe"
)

DOS4GW_FOUND=""

for path in "${SEARCH_PATHS[@]}"; do
    if [ -f "$path" ]; then
        DOS4GW_FOUND="$path"
        echo "Found DOS4GW.EXE at: $path"
        break
    fi
done

if [ -z "$DOS4GW_FOUND" ]; then
    echo "ERROR: DOS4GW.EXE not found!"
    echo ""
    echo "DOS4GW.EXE should be in your Watcom installation."
    echo "Please locate it manually and copy it to the current directory."
    echo ""
    echo "Common locations:"
    echo "  \$WATCOM/binw/dos4gw.exe"
    echo "  /opt/watcom/binw/dos4gw.exe"
    echo ""
    echo "Or download it from:"
    echo "  https://github.com/open-watcom/open-watcom-v2/releases"
    exit 1
fi

# Copy to current directory
cp "$DOS4GW_FOUND" ./dos4gw.exe

if [ $? -eq 0 ]; then
    echo "Successfully copied DOS4GW.EXE to current directory"
    echo ""
    echo "You can now run the game in DOSBox:"
    echo "  dosbox citysim.exe"
    echo ""
    echo "Or copy both citysim.exe and dos4gw.exe to your DOS machine"
else
    echo "ERROR: Failed to copy DOS4GW.EXE"
    exit 1
fi
