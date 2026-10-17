#!/usr/bin/env bash

# Install
export PREMAKE_URL="https://github.com/premake/premake-core/releases/download/v5.0.0-beta1/premake-5.0.0-beta1-linux.tar.gz"

curl "$PREMAKE_URL" -Lo premake5.tar.gz
tar xvf premake5.tar.gz
rm -f premake5.tar.gz

PREMAKE=$(pwd)/premake5

# Configure
cd "src_rebuild"
$PREMAKE gmake2
$PREMAKE vscode
cd project_gmake2_linux


