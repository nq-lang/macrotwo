#!/usr/bin/env bash
# .devcontainer/setup.sh
# Runs once on container creation (onCreateCommand).
set -euo pipefail

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║  MACRO INTELLIGENCE TERMINAL — Codespace Setup       ║"
echo "╚══════════════════════════════════════════════════════╝"
echo ""

# ── System packages ───────────────────────────────────────────────────────────
apt-get update -qq
apt-get install -y --no-install-recommends \
  build-essential ninja-build cmake pkg-config git curl zip unzip tar wget \
  libgl1-mesa-dev libglu1-mesa-dev mesa-utils \
  libx11-dev libxrandr-dev libxi-dev libxxf86vm-dev \
  libxinerama-dev libxcursor-dev libxext-dev libxfixes-dev \
  libssl-dev libcurl4-openssl-dev zlib1g-dev \
  libfontconfig1-dev libfreetype-dev \
  xvfb x11-utils \
  python3 python3-pip \
  clangd-17 clang-tidy-17 \
  gcc-13 g++-13

# ── Set GCC-13 as default ─────────────────────────────────────────────────────
update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
                    --slave   /usr/bin/g++ g++ /usr/bin/g++-13
update-alternatives --set gcc /usr/bin/gcc-13

echo "[setup] GCC: $(gcc --version | head -1)"
echo "[setup] G++: $(g++ --version | head -1)"

# ── vcpkg ─────────────────────────────────────────────────────────────────────
VCPKG_DIR=/usr/local/vcpkg
if [ ! -f "$VCPKG_DIR/vcpkg" ]; then
  echo "[setup] Bootstrapping vcpkg..."
  git clone --depth 1 https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
  "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
  chmod -R 777 "$VCPKG_DIR"
else
  echo "[setup] vcpkg already present at $VCPKG_DIR"
fi

export VCPKG_ROOT="$VCPKG_DIR"

# Add vcpkg to PATH for all future sessions
cat >> /home/vscode/.zshrc << 'ZSHEOF'

# Macro Terminal
export VCPKG_ROOT=/usr/local/vcpkg
export PATH="$VCPKG_ROOT:$PATH"
export DISPLAY=:99
ZSHEOF

cat >> /home/vscode/.bashrc << 'BASHEOF'
export VCPKG_ROOT=/usr/local/vcpkg
export PATH="$VCPKG_ROOT:$PATH"
export DISPLAY=:99
BASHEOF

# ── Configure project ─────────────────────────────────────────────────────────
cd /workspaces/macro-terminal || exit 0

echo "[setup] Configuring (Debug)..."
cmake --preset debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" \
  2>&1 | tail -20

# ── Build ─────────────────────────────────────────────────────────────────────
echo "[setup] Building..."
cmake --build build/debug --parallel "$(nproc)" 2>&1 | tail -30

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║  BUILD COMPLETE                                       ║"
echo "║                                                       ║"
echo "║  To run:                                              ║"
echo "║    cd build/debug                                     ║"
echo "║    cp ../../.env.example .env  (fill in API keys)    ║"
echo "║    xvfb-run -a ./bin/macro-terminal                   ║"
echo "║                                                       ║"
echo "║  To run tests:                                        ║"
echo "║    ctest --preset default                             ║"
echo "╚══════════════════════════════════════════════════════╝"
