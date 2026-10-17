#!/usr/bin/env bash
set -ex

# Configure
cd "$APPVEYOR_BUILD_FOLDER/src_rebuild"
./premake5 gmake2
cd build

# Build
for config in debug_x64 release_x64 release_dev_x64
do
    make config=$config -j$(nproc)
done
