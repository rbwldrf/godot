#!/bin/bash
# Build GameNetworkingSockets as a static library
# This follows Godot's approach of using pre-built thirdparty libraries

set -e

echo "Building GameNetworkingSockets static library..."

# Create build directory
mkdir -p build
cd build

# Generate protobuf headers if needed
PROTO_DIR="../src/common"
PROTO_FILES="steamnetworkingsockets_messages_certs.proto steamnetworkingsockets_messages.proto steamnetworkingsockets_messages_udp.proto"

echo "Generating protobuf headers..."
for proto in $PROTO_FILES; do
    if [ -f "$PROTO_DIR/$proto" ]; then
        protoc --cpp_out="$PROTO_DIR" -I="$PROTO_DIR" "$PROTO_DIR/$proto"
        echo "Generated headers for $proto"
    fi
done

# Compile all GameNetworkingSockets source files into object files
echo "Compiling source files..."

CXXFLAGS="-O2 -fPIC -DSTEAMNETWORKINGSOCKETS_FOREXPORT -DSTEAMNETWORKINGSOCKETS_STATIC_LINK -DSTEAMNETWORKINGSOCKETS_OPENSOURCE -DSTEAMNETWORKINGSOCKETS_DISABLE_ENCRYPTION -DPOSIX"
INCLUDES="-I../include -I../src -I../src/public -I../src/common -I../src/steamnetworkingsockets -I../src/steamnetworkingsockets/clientlib -I/usr/include"

# Minimal source files for basic UDP networking only (avoiding protobuf dependencies)
SOURCES="
../src/steamnetworkingsockets/clientlib/steamnetworkingsockets_flat.cpp
../src/steamnetworkingsockets/clientlib/steamnetworkingsockets_lowlevel.cpp
../src/steamnetworkingsockets/clientlib/steamnetworkingsockets_udp.cpp
../src/steamnetworkingsockets/steamnetworkingsockets_shared.cpp
../src/steamnetworkingsockets/steamnetworkingsockets_stats.cpp
../src/steamnetworkingsockets/steamnetworkingsockets_thinker.cpp
../src/common/steamid.cpp
../src/tier0/dbg.cpp
../src/tier0/platformtime.cpp
../src/tier1/ipv6text.c
../src/tier1/netadr.cpp
../src/tier1/utlbuffer.cpp
../src/tier1/utlmemory.cpp
../src/vstdlib/strtools.cpp
"

# Compile each source file
for src in $SOURCES; do
    if [ -f "$src" ]; then
        obj_name=$(basename "$src" | sed 's/\.[^.]*$/.o/')
        echo "Compiling $src -> $obj_name"
        g++ $CXXFLAGS $INCLUDES -c "$src" -o "$obj_name"
    else
        echo "Warning: Source file $src not found, skipping"
    fi
done

# Create static library
echo "Creating static library..."
ar rcs libGameNetworkingSockets.a *.o
ranlib libGameNetworkingSockets.a

echo "Static library built: $(pwd)/libGameNetworkingSockets.a"
ls -la libGameNetworkingSockets.a