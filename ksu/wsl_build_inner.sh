#!/bin/bash
# Adapted build_inner.sh for WSL2 Ubuntu (x86_64, no docker, no sudo).
# This is the exact recipe that produced ksu/tools/kernelsu.ko.
# See BUILD_NOTES.md for the full rationale and verification results.
set -eux

# ---- toolchain (debs extracted via apt-get download + dpkg-deb -x ~/toolchain) ----
TC="$HOME/toolchain"
export PATH="$TC/bin:$TC/usr/bin:$PATH"
export BISON_PKGDATADIR="$TC/usr/share/bison"
HOSTCFLAGS_TC="-I$TC/usr/include -I$TC/usr/include/x86_64-linux-gnu"
HOSTLDFLAGS_TC="-L$TC/usr/lib/x86_64-linux-gnu"

# All three paths are env-overridable; defaults assume a scratch dir under $HOME.
KERNEL="${KERNEL:-$HOME/rea-nx9/kernel-src}"  # AOSP kernel/common @ android12-5.10-2025-05_r7 (worktree)
KSU="${KSU:-$HOME/rea-nx9/ksu}"               # KernelSU v3.3.0 checkout WITH .git (+ 3 repo patches)
BUILD="${BUILD:-$HOME/rea-nx9/stage}"         # staging: empty_versions.py, patches, kallsyms_names

TOOLS="CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump \
STRIP=llvm-strip HOSTCC=clang HOSTCXX=clang++ ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
LLVM=1 LLVM_IAS=1 HOSTCFLAGS=$HOSTCFLAGS_TC HOSTLDFLAGS=$HOSTLDFLAGS_TC"

cd "$KERNEL"
# .config = zcat cfg.bin with CONFIG_UNUSED_KSYMS_WHITELIST repointed to a writable stub:
#   sed -i "s|/mnt/disks/...abi_symbollist.raw|$HOME/rea-nx9/ci-stub/abi_symbollist.raw|"
make $TOOLS olddefconfig
: > Module.symvers                  # EMPTY symvers on purpose (modpost -> no crc entries)
make $TOOLS modules_prepare -j"$(nproc)"      # must reach "LDS scripts/module.lds"
make $TOOLS security/selinux/ -j"$(nproc)" || true   # flask.h / av_permissions.h
make -C "$KERNEL" M="$KSU/kernel" $TOOLS CONFIG_KSU=m KCFLAGS=-Wno-error modules -j"$(nproc)"

python3 "$BUILD/empty_versions.py" "$KSU/kernel/kernelsu.ko" "$BUILD/kernelsu.ko"
llvm-nm -u "$BUILD/kernelsu.ko" | sed 's/^ *U *//' | sort -u > "$BUILD/ko_undefsyms.txt"
echo "=== BUILD DONE: $BUILD/kernelsu.ko ==="
