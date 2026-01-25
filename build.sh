#!/bin/bash
set -euo pipefail

start_time=$(date +%s)
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

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

clean_build=0
build_type="Release"
for arg in "$@"; do
    if [ "$arg" = "--clean" ]; then
        clean_build=1
    elif [[ "$arg" == --build-type=* ]]; then
        build_type="${arg#*=}"
    fi
done

git submodule update --init --recursive

if command -v ccache >/dev/null 2>&1; then
    export CCACHE_BASEDIR="$script_dir"
    export CCACHE_NOHASHDIR=1
    echo -e "[i]: ccache detected. Normalizing paths for cross-build cache hits."
fi

if [ -d "build" ] && [ $clean_build -eq 1 ]; then
    echo -e "[i]: Cleaning existing build directory..."
    rm -rf build/
    echo -e "[i]: Build Has been Removed\nContinuing build"
fi

mkdir -p build
cd build
cmake .. -DMONO_ROOT=/usr -DCMAKE_BUILD_TYPE="$build_type"
cmake --build . -- -j"$(nproc)"

mkdir -p Packages/ThirdParty
find . -type f \( -name "*.a" -o -name "*.so" -o -name "*.dylib" -o -name "*.lib" \) \
    -not -path "./Packages/*" -exec cp -f {} Packages/ThirdParty/ \;

mkdir -p Packages/Engine
find . -type f \( -name "libcore*" -o -name "core*.lib" -o -name "core*.dll" \) \
    -not -path "./Packages/*" -exec cp -f {} Packages/Engine/ \;

cd ..

player_cache_dir="build/player-cache"
if [ $clean_build -eq 1 ] && [ -d "$player_cache_dir" ]; then
    echo -e "[i]: Cleaning player cache build directory..."
    rm -rf "$player_cache_dir"
fi

mkdir -p "$player_cache_dir"
cmake -S . -B "$player_cache_dir" -DMONO_ROOT=/usr -DCMAKE_BUILD_TYPE="$build_type" -DMODULARITY_BUILD_EDITOR=OFF
cmake --build "$player_cache_dir" --target ModularityPlayer -- -j"$(nproc)"

mkdir -p "$player_cache_dir/Packages/ThirdParty"
find "$player_cache_dir" -type f \( -name "*.a" -o -name "*.so" -o -name "*.dylib" -o -name "*.lib" \) \
    -not -path "$player_cache_dir/Packages/*" -exec cp -f {} "$player_cache_dir/Packages/ThirdParty/" \;

mkdir -p "$player_cache_dir/Packages/Engine"
find "$player_cache_dir" -type f \( -name "libcore*" -o -name "core*.lib" -o -name "core*.dll" \) \
    -not -path "$player_cache_dir/Packages/*" -exec cp -f {} "$player_cache_dir/Packages/Engine/" \;

cd build

cp -r ../Resources .
cp Resources/imgui.ini .
ln -sf build/compile_commands.json compile_commands.json
