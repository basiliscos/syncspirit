#!/bin/bash

HERE="$(dirname "$(readlink -f "$0")")"

# Set library path to include bundled libraries
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"

# Execute the application
exec "$HERE/usr/bin/syncspirit-daemon" "$@"
