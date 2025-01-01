#!/bin/bash

# Usage instruction
echo "================================================="
echo " PCB2GCODE SCRIPT "
echo "================================================="
echo "Uscase:"
echo "Automates G-code generation for PCB milling from KiCad files by running pcb2gcode 3 times: first for standard files (front, back, outline, PTH drills) second for NPTH drills, third for engraving text on custom layer (user layer 9)"
echo "It handles file paths dynamically based on the project name, ensures required files exist, renames debug outputs and deletes unnecessary files"
echo ""
echo "Usage:"
echo "  ./run.sh [PROJECT_NAME]"
echo "If no PROJECT_NAME is provided, it will be derived from the parent folder name."
echo ""
echo "Requirements:"
echo "  - The script must be run from a folder within the KiCad project."
echo "  - A millproject config file must exist in the current folder."
echo "  - KiCad files must be exported to ../export relative to the script's location."
echo ""
echo "Example:"
echo "  Directory structure:"
echo "    /home/user/git/project-name/pcb2gcode/"
echo "      - run.sh (this script)"
echo "      - millproject (config file)"
echo "    /home/user/git/project-name/export/"
echo "      - project-name-F_Cu.gbr"
echo "      - project-name-B_Cu.gbr"
echo "      - project-name-Edge_Cuts.gbr"
echo "      - project-name-PTH.drl"
echo "      - project-name-NPTH.drl"
echo ""
echo "  To run the script:"
echo "    cd /home/user/git/project-name/pcb2gcode"
echo "    ./run.sh project-name"
echo "================================================="
echo ""
echo ""



# Define project name
# Extract project name from argument or parent folder
if [[ -n "$1" ]]; then
    PROJECT_NAME="$1"
else
    PROJECT_NAME=$(basename "$(dirname "$(pwd)")")
    echo "No project name provided. Using folder name as project name: $PROJECT_NAME"
fi


# Define folders
INPUT_DIR="../export"
INPUT_DIR_MILL_ISOLATION="../export/gndPlanesOnly"
OUTPUT_DIR="./out"
OUTPUT_DIR_TEMP="/tmp/pcb2gcode_out"

# Define file paths
BACK_FILE="$INPUT_DIR/${PROJECT_NAME}-B_Cu.gbr"
FRONT_FILE="$INPUT_DIR/${PROJECT_NAME}-F_Cu.gbr"
OUTLINE_FILE="$INPUT_DIR/${PROJECT_NAME}-Edge_Cuts.gbr"
PTH_DRILL_FILE="$INPUT_DIR/${PROJECT_NAME}-PTH.drl"

# files for pass for milling away copper areas
MILL_AWAY_COPPER__BACK_FILE="$INPUT_DIR/gndPlanesOnly/${PROJECT_NAME}-B_Cu.gbr"
MILL_AWAY_COPPER__FRONT_FILE="$INPUT_DIR/gndPlanesOnly/${PROJECT_NAME}-F_Cu.gbr"

NPTH_DRILL_FILE="$INPUT_DIR/${PROJECT_NAME}-NPTH.drl"
TEXT_FRONT_FILE="$INPUT_DIR/${PROJECT_NAME}-User_9.gbr"

NPTH_DRILL_OUT_FILE="npth.ngc"
TEXT_FRONT_OUT_FILE="text-front.ngc"

CONFIG_FILE="./millproject"
CONFIG_FILE_TEXT="./millproject_text"
CONFIG_FILE_MILL_AWAY_COPPER="./millproject_millAwayCopper"
TEMP_CONFIG=$(mktemp)

# Additional options for pcb2gcode runs
PCB2GCODE_OPTIONS="2>&1 | grep -v 'Unsupported'" #-> do not output erros like:
#** (process:507955): CRITICAL **: 10:23:56.224: Unsupported G00 (rout mode) code at line 197 in file "../export/power-supply-board_v1.0-PTH.drl"



# Create output directory if it doesn't exist
echo "clearing output dir..."
rm -rfv $OUTPUT_DIR/* # clear previous output
mkdir -p "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR_TEMP"


# Function to append file paths to config file
append_file_paths() {
    echo "==> run.sh: Appending file paths to config..."
    echo "back=$BACK_FILE" >> "$1"
    echo "front=$FRONT_FILE" >> "$1"
    echo "outline=$OUTLINE_FILE" >> "$1"
    echo "drill=$PTH_DRILL_FILE" >> "$1"
    echo "output-dir=$OUTPUT_DIR" >> "$1"
}

# Function to check file existence
check_file() {
    if [[ ! -f "$1" ]]; then
        echo "==> run.sh: Warning: File not found: $1"
        return 1
    fi
    return 0
}



# Run first pass: Generate front, back, outline, and PTH drilling
    echo -e "\n\n\n"
    echo "================================================="
    echo "=== initiating first run (front, back, holes) ==="
    echo "================================================="
if check_file "$BACK_FILE" && check_file "$FRONT_FILE" && check_file "$OUTLINE_FILE" && check_file "$PTH_DRILL_FILE"; then
    cp "$CONFIG_FILE" "$TEMP_CONFIG"
    append_file_paths "$TEMP_CONFIG"
    pcb2gcode --config "$TEMP_CONFIG"
    echo "==> run.sh: First pass complete."
else
    echo "==> run.sh: Skipping first pass due to missing files."
fi



# Run second pass: Generate NPTH drilling only
    echo -e "\n\n\n"
    echo "==========================================="
    echo "=== initiating second run for NPTH only ==="
    echo "==========================================="
if check_file "$NPTH_DRILL_FILE"; then
    cp "$CONFIG_FILE" "$TEMP_CONFIG"
    echo "==> run.sh: Appending NPTH drill file to config..."
    # rename original drill debug output image
    echo "drill=$NPTH_DRILL_FILE" >> "$TEMP_CONFIG"
    echo "outline=$OUTLINE_FILE" >> "$TEMP_CONFIG" #note: outline needed for matching gcode origin
    echo "drill-output=$NPTH_DRILL_OUT_FILE" >> "$TEMP_CONFIG"
    echo "output-dir=$OUTPUT_DIR" >> "$TEMP_CONFIG"
    echo "==> run.sh: Renaming drill debug image from previous run..."
    mv "$OUTPUT_DIR"/original_drill.svg "$OUTPUT_DIR"/original_drill_normal.svg
    pcb2gcode --config "$TEMP_CONFIG"
    mv "$OUTPUT_DIR"/original_drill.svg "$OUTPUT_DIR"/original_drill_NPTH.svg
    echo "==> run.sh: Second pass complete."
else
    echo "==> run.sh: Skipping second pass due to missing NPTH file: $NPTH_DRILL_FILE"
fi




# Run third pass: engrave text (separate layer) only
    echo -e "\n\n\n"
    echo "==========================================="
    echo "=== initiating third run (engrave text) ==="
    echo "==========================================="
if check_file "$TEXT_FRONT_FILE"; then
    rm -rf "$OUTPUT_DIR_TEMP"
mkdir -p "$OUTPUT_DIR_TEMP"
    cp "$CONFIG_FILE_TEXT" "$TEMP_CONFIG"
    echo "==> run.sh: Appending text gerber file to config..."
    # rename original drill debug output image
    echo "front=$TEXT_FRONT_FILE" >> "$TEMP_CONFIG"
    echo "outline=$OUTLINE_FILE" >> "$TEMP_CONFIG" #note: outline needed for matching gcode origin
    echo "front-output=$TEXT_FRONT_OUT_FILE" >> "$TEMP_CONFIG"
    echo "output-dir=$OUTPUT_DIR_TEMP" >> "$TEMP_CONFIG"
    pcb2gcode --config "$TEMP_CONFIG"
    # copy only wanted files to actual output folder
    echo "==> run.sh: Copy wanted files from temp folder"
    cp "$OUTPUT_DIR_TEMP"/processed_front_final.svg "$OUTPUT_DIR"/text-front_processed-final.svg
    cp "$OUTPUT_DIR_TEMP"/text-front.ngc "$OUTPUT_DIR"/text-front.ngc
    echo "==> run.sh: third pass complete."
else
    echo "==> run.sh: Skipping third pass due to missing text-layer file: $TEXT_FRONT_FILE"
fi





# Run 4th pass: engrave text (separate layer) only
    echo -e "\n\n\n"
    echo "====================================================="
    echo "=== initiating third run (mill away copper areas) ==="
    echo "====================================================="
if check_file "$MILL_AWAY_COPPER__BACK_FILE"; then
    rm -rf "$OUTPUT_DIR_TEMP"
mkdir -p "$OUTPUT_DIR_TEMP"
    cp "$CONFIG_FILE_MILL_AWAY_COPPER" "$TEMP_CONFIG"
    echo "back=$MILL_AWAY_COPPER__BACK_FILE" >> "$TEMP_CONFIG"
    echo "front=$MILL_AWAY_COPPER__FRONT_FILE" >> "$TEMP_CONFIG"
    echo "outline=$OUTLINE_FILE" >> "$TEMP_CONFIG"
    echo "output-dir=$OUTPUT_DIR_TEMP" >> "$TEMP_CONFIG"

    #cat "$TEMP_CONFIG"
    pcb2gcode --config "$TEMP_CONFIG"
    echo "==> run.sh: Copy wanted files from temp folder"

    cp "$OUTPUT_DIR_TEMP"/front.ngc "$OUTPUT_DIR"/MILL-AWAY-COPPER_front.ngc
    cp "$OUTPUT_DIR_TEMP"/back.ngc "$OUTPUT_DIR"/MILL-AWAY-COPPER_back.ngc

    for i in $(seq 0 8); do
        # Processed front files
        if [ -f "$OUTPUT_DIR_TEMP/processed_front_final_$i.svg" ]; then
            cp "$OUTPUT_DIR_TEMP/processed_front_final_$i.svg" "$OUTPUT_DIR/MILL-AWAY-COPPER_front_processed-final_$i.svg"
        fi

        # Processed back files
        if [ -f "$OUTPUT_DIR_TEMP/processed_back_final_$i.svg" ]; then
            cp "$OUTPUT_DIR_TEMP/processed_back_final_$i.svg" "$OUTPUT_DIR/MILL-AWAY-COPPER_back_processed-final_$i.svg"
        fi
    done 

    echo "==> run.sh:  pass for milling away copper areas finished"
else
    echo "==> run.sh: Skipping third pass due to missing text-layer file: $MILL_AWAY_COPPER_BACK_FILE"
fi




# Clean up temporary config file
rm -f "$TEMP_CONFIG"
#rm -rf "$OUTPUT_DIR_TEMP"
# Delete unwanted files
echo "==> run.sh: Deleting unwanted files..."
rm -f "$OUTPUT_DIR"/*contentions*.svg \
      "$OUTPUT_DIR"/*original_outline.svg \
      "$OUTPUT_DIR"/*original_front.svg \
      "$OUTPUT_DIR"/*original_back.svg \
      "$OUTPUT_DIR"/*processed_front.svg \
      "$OUTPUT_DIR"/*processed_back.svg \
      "$OUTPUT_DIR"/*processed_outline.svg \
      "$OUTPUT_DIR"/*traced*.svg \
      "$OUTPUT_DIR"/*masked*.svg \
      "$OUTPUT_DIR"/*contentions*.svg


echo "==> run.sh: G-code generation complete. Output saved in $OUTPUT_DIR."
