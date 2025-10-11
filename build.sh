#!/bin/bash

# Default values
CMAKE_ARGS=""

# Find a valid macOS SDK and set it for CMake to fix a potential mismatch
if [[ "$(uname)" == "Darwin" ]]; then
    SDK_PATH=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null)
    if [ -n "$SDK_PATH" ]; then
        CMAKE_ARGS="-DCMAKE_OSX_SYSROOT=$SDK_PATH"
    fi
fi

# Parse command-line arguments
for arg in "$@"
do
    case $arg in
        --tidy)
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CXX_CLANG_TIDY=clang-tidy"
        shift # Remove --tidy from processing
        ;;
        --tidy-fix)
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CXX_CLANG_TIDY='clang-tidy;-fix-errors'"
        shift # Remove --tidy-fix from processing
        ;;
        *)
        # Other arguments can be passed to cmake
        ;;
    esac
done

# Clean build directory for a fresh configuration
if [ -d "build" ]; then
    echo "Removing existing build directory."
    rm -rf build
fi

echo "Configuring with: cmake -B build $CMAKE_ARGS"

# Run CMake configuration. This will generate compile_commands.json
cmake -B build $CMAKE_ARGS

# Run the build
echo "Building project..."
cmake --build build --parallel
