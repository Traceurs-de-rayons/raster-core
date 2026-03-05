#!/bin/bash
# Met à jour le compile_commands.json de raster-core depuis le build engine
jq "[.[] | select(.file | contains(\"raster-core\"))]" ../engine/build/compile_commands.json > compile_commands.json
echo "✓ Updated raster-core/compile_commands.json ($(jq length compile_commands.json) entries)"
