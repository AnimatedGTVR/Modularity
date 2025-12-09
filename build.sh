#!/bin/bash
set -euo pipefail

start_time=$(date +%s)

finish() {
    local exit_code=$?
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))

    if [ $exit_code -eq 0 ]; then
        echo -e "================================\n   Modularity - Native Linux Build Complete\n================================"
        echo -e "[Complete]: Your Modularity Build Completed in ${duration}s!\nThe build should be located under Modularity within another folder called 'Build'"
    else
        echo -e "================================\n   Modularity - Native Linux Build Failed\n================================"
        echo "[Failed]: Your Modularity Build Failed after ${duration}s (exit code ${exit_code})."
    fi

    exit $exit_code
}

trap finish EXIT

echo -e "================================\n   Modularity - Native Linux Builder\n================================"

git submodule update --init --recursive

if [ -d "build" ]; then
    echo -e "[i]: Oh! We found an existing build directory.\nRemoving existing folder..."
    rm -rf build/
    echo -e "[i]: Build Has been Removed\nContinuing build"
fi

mkdir -p build
cd build
cmake ..
cmake --build . -- -j"$(nproc)"
cp -r ../Resources .
cp Resources/imgui.ini .
ln -sf build/compile_commands.json compile_commands.json