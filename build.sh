#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
#
# Builds everything for the EDK II payload on the F101 EVB and packs it:
#
#   ./build.sh [all|tools|opensbi|dtb|syterkit|edk2|package|clean]
#
# Output: out/f101-edk2/  (see its README.txt and run.sh)
#
# Toolchains (paths can be overridden with the environment):
#   LINUX_GCC   Xuantie linux glibc toolchain prefix, builds OpenSBI (rv64, needs a linker with
#               PIE support) and EDK II (GCC_RISCV64_PREFIX)
#   ELF_GCC     Xuantie newlib toolchain prefix, builds SyterKit (rv32 loader)
set -euo pipefail

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
WS=$(cd -- "$HERE/.." && pwd)
LINUX_GCC=${LINUX_GCC:-$WS/toolchains/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V3.2.0/bin/riscv64-unknown-linux-gnu-}
ELF_GCC=${ELF_GCC:-$WS/toolchains/Xuantie-900-gcc-elf-newlib-x86_64-V3.2.0/bin/riscv64-unknown-elf-}
DTC=${DTC:-$(command -v dtc || echo "$WS/tools-local/usr/bin/dtc")}
JOBS=${JOBS:-$(nproc)}
OUT=$HERE/out/f101-edk2
PLATFORM=F101
BUILD_TARGET=${BUILD_TARGET:-RELEASE}

# memory map of the 16 MiB PSRAM (the loader, OpenSBI and F101Pkg/F101.fdf agree on it)
EDK2_ADDR=0x40000000
DTB_ADDR=0x40f40000
SBI_ADDR=0x40f80000
LOADER_ADDR=0x20000

say() { printf '\n== %s\n' "$*"; }

submodules() {
	say "submodules"
	git -C "$HERE" submodule update --init opensbi SyterKit edk2
	# the few submodules of EDK II that the platform needs
	git -C "$HERE/edk2" submodule update --init --depth 1 \
		MdePkg/Library/BaseFdtLib/libfdt \
		MdePkg/Library/MipiSysTLib/mipisyst \
		MdeModulePkg/Library/BrotliCustomDecompressLib/brotli \
		MdeModulePkg/Universal/RegularExpressionDxe/oniguruma \
		BaseTools/Source/C/BrotliCompress/brotli
}

edk2_env() {
	export WORKSPACE=$HERE
	export PACKAGES_PATH=$HERE:$HERE/edk2
	export EDK_TOOLS_PATH=$HERE/edk2/BaseTools
	export GCC_RISCV64_PREFIX=$LINUX_GCC
	# libuuid and iasl for the host tools: no development packages needed when they are
	# in tools-local of the workspace
	export C_INCLUDE_PATH=$WS/tools-local/usr/include${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}
	export LIBRARY_PATH=$WS/tools-local/usr/lib${LIBRARY_PATH:+:$LIBRARY_PATH}
	export PATH=$WS/tools-local/usr/bin:$PATH
	unset CROSS_COMPILE
}

do_tools() {
	say "EDK II BaseTools"
	edk2_env
	make -C "$HERE/edk2/BaseTools" -j"$JOBS"
}

do_opensbi() {
	say "OpenSBI (rv64)"
	[ -x "${LINUX_GCC}gcc" ] || { echo "no compiler: ${LINUX_GCC}gcc" >&2; exit 1; }
	# the Makefile resolves O= with readlink -f, which needs the parent directory to exist
	mkdir -p "$HERE/opensbi/build"
	unset CROSS_COMPILE O
	( cd "$HERE/opensbi" && ./build.sh --arch rv64 "$LINUX_GCC" )
}

do_dtb() {
	say "device tree"
	"$DTC" -I dts -O dtb -o "$HERE/dts/f101-evb.dtb" "$HERE/dts/f101-evb.dts"
}

do_syterkit() {
	say "SyterKit loader (rv32, switches the core to rv64 and starts OpenSBI)"
	local sk=$HERE/SyterKit
	if git -C "$sk" apply --check "$HERE/patches/syterkit-f101-evb.patch" 2>/dev/null; then
		git -C "$sk" apply "$HERE/patches/syterkit-f101-evb.patch"
	fi
	unset CROSS_COMPILE
	mkdir -p "$HERE/out-syter"
	( cd "$sk"
	  make O=../out-syter yuzukineko_rv32_sram_defconfig CROSS_COMPILE="$ELF_GCC" >/dev/null
	  # no Rust toolchain needed
	  sed -i 's/^CONFIG_RUST_FFI=y/# CONFIG_RUST_FFI is not set/' ../out-syter/.config
	  make O=../out-syter olddefconfig CROSS_COMPILE="$ELF_GCC" >/dev/null
	  make O=../out-syter -j"$JOBS" CROSS_COMPILE="$ELF_GCC" >/dev/null )
}

do_edk2() {
	say "EDK II $PLATFORM ($BUILD_TARGET)"
	edk2_env
	[ -x "$HERE/edk2/BaseTools/Source/C/bin/GenFv" ] || do_tools
	# shellcheck disable=SC1091
	# edksetup.sh is not written for set -u / set -e
	( set +eu; cd "$HERE/edk2" && . ./edksetup.sh >/dev/null 2>&1; cd "$HERE" &&
	  build -a RISCV64 -b "$BUILD_TARGET" -t GCC -p F101Pkg/F101.dsc -n "$JOBS" )
}

do_package() {
	say "package -> $OUT"
	local fd=$HERE/Build/$PLATFORM/${BUILD_TARGET}_GCC/FV/F101_CODE.fd
	local sbi=$HERE/opensbi/build/sun252i-f101-rv64/platform/generic/firmware/fw_jump.bin
	local loader=$HERE/out-syter/build/yuzukineko/app_sram/usb-boot-rv64i/usb-boot-rv64i_fel.bin
	local f
	for f in "$fd" "$sbi" "$loader" "$HERE/dts/f101-evb.dtb"; do
		[ -f "$f" ] || { echo "missing: $f (run ./build.sh all)" >&2; exit 1; }
	done
	rm -rf "$OUT"; mkdir -p "$OUT"
	python3 - "$fd" "$HERE/dts/f101-evb.dtb" "$sbi" "$OUT" <<'PY'
import sys
fd, dtb, sbi, out = sys.argv[1:5]
img = open(fd, 'rb').read()
used = len(img.rstrip(b'\xff'))
used = (used + 0xfff) & ~0xfff          # the rest of the flash image is erased state
open(out + '/edk2.fd', 'wb').write(img[:used])
d = open(dtb, 'rb').read()
s = open(sbi, 'rb').read()
assert len(d) <= 0x40000
# device tree at 0x40f40000, OpenSBI 0x40000 behind it (0x40f80000)
open(out + '/dtb_sbi.bin', 'wb').write(d + b'\0' * (0x40000 - len(d)) + s)
print('edk2.fd %d bytes, dtb_sbi.bin %d bytes' % (used, 0x40000 + len(s)))
PY
	cp "$loader" "$OUT/loader.bin"
	cp "$HERE/dts/f101-evb.dtb" "$sbi" "$OUT/"
	cat > "$OUT/run.sh" <<EOS
#!/bin/sh
# Boots the EDK II image from the FEL mode with xfel (a fresh power cycle first).
# xfel needs 'ddr f101-s3' to bring the PSRAM up; the console is UART3 (115200).
set -e
D=\$(cd "\$(dirname "\$0")" && pwd)
XFEL=\${XFEL:-xfel}
\$XFEL version
\$XFEL ddr f101-s3
\$XFEL write $EDK2_ADDR "\$D/edk2.fd"
\$XFEL write $DTB_ADDR "\$D/dtb_sbi.bin"
\$XFEL write $LOADER_ADDR "\$D/loader.bin"
\$XFEL exec $LOADER_ADDR
EOS
	chmod +x "$OUT/run.sh"
	cat > "$OUT/README.txt" <<EOS
EDK II for the Allwinner F101 EVB (XuanTie C907 in RV64 mode)

  edk2.fd       the firmware image                          -> $EDK2_ADDR
  dtb_sbi.bin   device tree (256 KiB slot) + OpenSBI        -> $DTB_ADDR ($SBI_ADDR is OpenSBI)
  loader.bin    SyterKit (rv32): switches the core to RV64  -> $LOADER_ADDR (SRAM), executed
  f101-evb.dtb, fw_jump.bin   the two parts of dtb_sbi.bin

Boot: power cycle into FEL, then ./run.sh (needs xfel). Console: UART3 PE08/PE09, 115200 8N1.
Flow: FEL -> loader.bin (rv32) -> watchdog reset into RV64 -> OpenSBI (S-mode next) -> edk2.fd
EOS
	ls -l "$OUT"
}

do_clean() {
	rm -rf "$HERE/Build" "$HERE/out" "$HERE/out-syter" "$HERE/opensbi/build"
}

case "${1:-all}" in
	all) submodules; do_tools; do_opensbi; do_dtb; do_syterkit; do_edk2; do_package ;;
	tools) edk2_env; do_tools ;;
	opensbi) do_opensbi ;;
	dtb) do_dtb ;;
	syterkit) do_syterkit ;;
	edk2) do_edk2 ;;
	package) do_package ;;
	submodules) submodules ;;
	clean) do_clean ;;
	*) echo "usage: $0 [all|submodules|tools|opensbi|dtb|syterkit|edk2|package|clean]" >&2; exit 1 ;;
esac
