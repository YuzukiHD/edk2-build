# Environment of the EDK II build: . ./env.sh
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS="$(cd "$HERE/.." && pwd)"
export WORKSPACE="$HERE"
export PACKAGES_PATH="$HERE:$HERE/edk2"
export EDK_TOOLS_PATH="$HERE/edk2/BaseTools"
export GCC_RISCV64_PREFIX="${GCC_RISCV64_PREFIX:-$WS/toolchains/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V3.2.0/bin/riscv64-unknown-linux-gnu-}"
# libuuid for the host tools, no development package is installed
export C_INCLUDE_PATH="$WS/tools-local/usr/include${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
export LIBRARY_PATH="$WS/tools-local/usr/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
export PATH="$WS/tools-local/usr/bin:$PATH"
unset CROSS_COMPILE
