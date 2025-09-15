#!/bin/bash

clear

FILES=(
docs/project_summary.md
static/midi_out.html
static/midi_in.html
)

for ((i=0; i<${#FILES[@]}; i++))
do
    echo "## ${FILES[$i]}"
    echo ""
    echo '```'
    cat ${FILES[$i]}
    echo '```'
    echo ""
done
