#!/usr/bin/env bash
# setup-cross-build-debian.sh
# Sets up a Linux (Debian/Ubuntu) host to cross-compile BasicRenderer
# as a Windows x64 executable using clang-cl + lld-link.
#
# Run with: sudo ./scripts/setup-cross-build-debian.sh
# After setup, configure with: cmake --preset linux-cross-debug

set -euo pipefail

XWIN_VERSION="0.6.5"
XWIN_DIR="${XWIN_DIR:-$HOME/.xwin}"

echo "=== BasicRenderer cross-compilation setup (Debian/Ubuntu) ==="

# System packages
echo ""
echo "[1/5] Installing system packages..."
apt-get update
apt-get install -y \
    clang \
    lld \
    llvm \
    cmake \
    ninja-build \
    git \
    curl \
    unzip \
    tar \
    pkg-config \
    wine64 \
    python3 \
    ca-certificates

# Verify tools exist
echo "  clang-cl: $(which clang-cl 2>/dev/null || echo 'NOT FOUND — will check alternatives')"
echo "  lld-link: $(which lld-link 2>/dev/null || echo 'NOT FOUND — will check alternatives')"
echo "  llvm-lib: $(which llvm-lib 2>/dev/null || echo 'NOT FOUND — will check alternatives')"
echo "  llvm-rc:  $(which llvm-rc  2>/dev/null || echo 'NOT FOUND — will check alternatives')"

# On Debian, LLVM tools may be suffixed with the version number (e.g. clang-cl-18).
# Create unversioned symlinks if the bare names don't exist.
for tool in clang-cl lld-link llvm-lib llvm-rc llvm-mt; do
    if ! command -v "$tool" &>/dev/null; then
        # Find the highest-versioned variant
        versioned=$(compgen -c "${tool}-" 2>/dev/null | sort -V | tail -n1 || true)
        if [ -n "$versioned" ] && command -v "$versioned" &>/dev/null; then
            echo "  Creating symlink: $tool -> $versioned"
            ln -sf "$(which "$versioned")" "/usr/local/bin/$tool"
        else
            echo "  WARNING: $tool not found. You may need to install a newer LLVM package."
        fi
    fi
done

# vcpkg
echo ""
echo "[2/5] Setting up vcpkg..."
if [ -z "${VCPKG_ROOT:-}" ]; then
    VCPKG_ROOT="$HOME/vcpkg"
fi

if [ ! -d "$VCPKG_ROOT" ]; then
    echo "  Cloning vcpkg to $VCPKG_ROOT..."
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
    "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
else
    echo "  vcpkg already exists at $VCPKG_ROOT"
    # Ensure it's up to date
    git -C "$VCPKG_ROOT" pull --ff-only 2>/dev/null || true
    "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics 2>/dev/null || true
fi

# xwin (Windows SDK + MSVC CRT)
echo ""
echo "[3/5] Installing xwin and downloading Windows SDK..."
XWIN_BIN="$HOME/.local/bin/xwin"
if [ ! -f "$XWIN_BIN" ]; then
    mkdir -p "$HOME/.local/bin"
    ARCH=$(uname -m)
    if [ "$ARCH" = "x86_64" ]; then
        XWIN_ARCH="x86_64"
    elif [ "$ARCH" = "aarch64" ]; then
        XWIN_ARCH="aarch64"
    else
        echo "  ERROR: Unsupported architecture $ARCH for xwin"
        exit 1
    fi
    XWIN_URL="https://github.com/Jake-Shadle/xwin/releases/download/${XWIN_VERSION}/xwin-${XWIN_VERSION}-${XWIN_ARCH}-unknown-linux-musl.tar.gz"
    echo "  Downloading xwin ${XWIN_VERSION}..."
    curl -fsSL "$XWIN_URL" | tar xz -C /tmp
    mv "/tmp/xwin-${XWIN_VERSION}-${XWIN_ARCH}-unknown-linux-musl/xwin" "$XWIN_BIN"
    chmod +x "$XWIN_BIN"
    rm -rf "/tmp/xwin-${XWIN_VERSION}-${XWIN_ARCH}-unknown-linux-musl"
else
    echo "  xwin already installed at $XWIN_BIN"
fi

if [ ! -d "$XWIN_DIR/crt" ]; then
    echo "  Downloading and splatting Windows SDK + MSVC CRT to $XWIN_DIR..."
    "$XWIN_BIN" --accept-license splat --output "$XWIN_DIR"
else
    echo "  Windows SDK already present at $XWIN_DIR"
fi

# Wine prefix initialization
echo ""
echo "[4/5] Initializing Wine prefix (for CMAKE_CROSSCOMPILING_EMULATOR)..."
# Create a minimal Wine prefix so first run of resource_codegen doesn't stall
if [ ! -d "${WINEPREFIX:-$HOME/.wine}" ]; then
    WINEDEBUG=-all wineboot --init 2>/dev/null || true
    echo "  Wine prefix initialized."
else
    echo "  Wine prefix already exists."
fi

# Environment summary
echo ""
echo "[5/5] Setup complete!"
echo ""
echo "Add these to your shell profile (~/.bashrc or ~/.zshrc):"
echo ""
echo "  export VCPKG_ROOT=\"$VCPKG_ROOT\""
echo "  export PATH=\"\$VCPKG_ROOT:\$HOME/.local/bin:\$PATH\""
echo "  export XWIN_DIR=\"$XWIN_DIR\""
echo ""
echo "Then configure and build with:"
echo ""
echo "  cmake --preset linux-cross-debug"
echo "  cmake --build out/build/linux-cross-debug"
echo ""
echo "Run the result with:"
echo ""
echo "  wine out/build/linux-cross-debug/BasicRenderer/BasicRenderer.exe"
echo ""
