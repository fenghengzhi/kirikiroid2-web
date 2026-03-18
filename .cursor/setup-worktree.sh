#!/bin/bash
set -e

export PATH="/opt/homebrew/opt/bison/bin:$PATH"
source "$HOME/emsdk/emsdk_env.sh"
export VCPKG_ROOT="$HOME/vcpkg"

if [ -n "$ROOT_WORKTREE_PATH" ]; then
    cp "$ROOT_WORKTREE_PATH/server.crt" server.crt 2>/dev/null || true
    cp "$ROOT_WORKTREE_PATH/server.key" server.key 2>/dev/null || true
fi

cmake --preset "Web Release Config"
cmake --build out/web/release -j"$(sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

echo "Worktree setup complete!"
