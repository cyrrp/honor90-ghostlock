# kernelsu.ko — Honor 90 (REA-NX9) build notes

## Deliverable
`ksu/tools/kernelsu.ko` — 6,470,888 bytes, sha256 `bf4841e17483c5e8c59f4725b231e41c38944b7ef3757de8da690759993f1947`
(the committed binary had its embedded build-machine paths normalized — 72 same-length
byte substitutions; load-time behavior is identical; original sha256 was
`17c678e4abd670bbb71f541162505ff34a3a563e05c9bff98119526d9cad6e1a`)
ELF64 aarch64 relocatable, `vermagic: 5.10.236 SMP preempt mod_unload modversions aarch64`,
`__versions` section present with `sh_size=0` (empty-section trick verified against
`kernel/module.c:1305-1398` of this exact tree: `num_versions=0` → `pr_warn_once` + PASS per
symbol, and `same_magic()` skips the release string because the section exists → the
device's `-android12-9-...-ab14427738` release suffix is never compared).

## Sources
- KERNEL_SRC: **AOSP kernel/common @ tag `android12-5.10-2025-05_r7`** (`71f3aedc8b92`),
  checked out in a Linux filesystem (WSL2 ext4) from a bare clone of the tag — a Windows
  worktree is impossible (`drivers/gpu/drm/nouveau/…/aux.c` is a reserved DOS name). No
  Honor REA-NX9 kernel source is publicly downloadable (opensource.hihonor.com unreachable /
  no package), **and it isn't needed**: the device `cfg.bin` (gzipped `/proc/config.gz`
  equivalent, pulled from the device) carries `CONFIG_UNUSED_KSYMS_WHITELIST=
  "/mnt/disks/build-disk/src/android/common-android12-5.10-2025-05/…/abi_symbollist.raw"` —
  the kernel was built on AOSP's build infrastructure from the plain
  `common-android12-5.10-2025-05` branch.
- Device config: `cfg.bin` (`zcat` → `.config`), used verbatim **except**
  `CONFIG_UNUSED_KSYMS_WHITELIST` repointed to a writable stub file
  (e.g. `~/rea-nx9/ci-stub/abi_symbollist.raw`, empty). `/mnt/disks` is not creatable
  without root in WSL; the whitelist is only read at vmlinux link time and never affects
  an external-module build.
- KernelSU **v3.3.0** (`932014ab`, full history → `rev-list --count`=2601 →
  **KSU_VERSION 32601**) + the three repo patches applied in order
  (init-bootid, sepolicy-dyn-len, selinux-hide-backup). NOTE: the repo `.patch` files are
  CRLF-terminated — convert to LF (`sed -i 's/\r$//'`) or `git apply` fails spuriously.

## Toolchain (WSL2 Ubuntu 22.04, no sudo, no docker)
`apt-get download` + `dpkg-deb -x ~/toolchain` for: clang-14, lld-14, libclang-common-14-dev,
libclang1-14, libobjc-11-dev, bc, bison, flex, m4, libelf-dev, libssl-dev, zlib1g-dev,
libzstd-dev. System packages already present: gcc-11, aarch64-linux-gnu-* (2.38), llvm-14
binutils, make, pahole, git, python3, rsync, kmod.
- `~/toolchain/bin/` symlinks provide `clang`, `clang++`, `ld.lld`, `llvm-*` names.
- bison needs `BISON_PKGDATADIR=~/toolchain/usr/share/bison`.
- `scripts/extract-cert` needs openssl headers: `HOSTCFLAGS="-I$TC/usr/include
  -I$TC/usr/include/x86_64-linux-gnu"` + `HOSTLDFLAGS=-L$TC/usr/lib/x86_64-linux-gnu`
  (`opensslconf.h` lives in the arch include dir). The extracted `libssl.so`/`libcrypto.so`
 /`libelf.so` symlinks were repointed to the system `.so.3`/`.so.1` files.
- clang-14 (not the device build's r416183b/clang-12.0.5): matches the original
  debian:bookworm recipe; struct layouts are identical regardless (verified below).

## Build steps (mirror of `ksu/wsl_build_inner.sh`)
```
export PATH=$HOME/toolchain/bin:$HOME/toolchain/usr/bin:$PATH
export BISON_PKGDATADIR=$HOME/toolchain/usr/share/bison
TOOLS="CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump \
STRIP=llvm-strip HOSTCC=clang HOSTCXX=clang++ ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
LLVM=1 LLVM_IAS=1 HOSTCFLAGS=-I…(above) HOSTLDFLAGS=-L…(above)"
cd $KERNEL_SRC
make $TOOLS olddefconfig
: > Module.symvers
make $TOOLS modules_prepare -j8            # generates scripts/module.lds (must succeed!)
make $TOOLS security/selinux/ -j8          # generates security/selinux/{flask,av_permissions}.h
make -C "$KERNEL_SRC" M=$KSU_SRC/kernel $TOOLS \
     CONFIG_KSU=m KCFLAGS=-Wno-error modules -j8
python3 empty_versions.py kernelsu.ko out.ko   # __versions -> sh_size 0
```
Gotcha: `modules_prepare` must actually reach `LDS scripts/module.lds`. The first run
dies silently inside `prepare` at `scripts/extract-cert` (openssl headers) and never
generates it; the module link then fails with "No rule to make target
'scripts/module.lds'". Fix the HOSTCFLAGS first.

## Verification results
- **Struct-offset drift: NONE.** pahole DWARF comparison of the built .ko vs the device
  vmlinux (extracted from boot.img), all (member, offset, size) triples identical for: task_struct (202
  members; cred@1920/0x780, real_cred@1912, mm@1304, active_mm@1312, comm@1936, fs@1976,
  files@1984, nsproxy@2000, seccomp@2120), cred, selinux_state (initialized@2),
  selinux_policy (sidtab@0, policydb@8 sz 600), policydb (symtab@16), sidtab
  (convert@40, frozen@48), avtab, ctl_table (data@8), inode, super_block, mm_struct,
  file, dentry, vfsmount, fsnotify_mark_connector, fsnotify_group. Only cosmetic
  `unsigned long` vs `long unsigned int` spelling differs (clang-14 vs clang-12 DWARF
  naming). The H80GT's 0x780→0x7b8 drift does NOT apply here — REA-NX9 runs pure GKI.
- **Undefined symbols:** 208; ALL resolve against this device's System.map + vmlinux
  symtab. (`docs/ko_missing_syms.txt` flags `__cfi_slowpath`, `commit_creds`,
  `kasan_flag_enabled` only because `kallsyms_names` is the *H80GT* symbol list — all
  three exist on REA-NX9. Per the tools README the loader's fake-kallsyms intentionally
  omits commit_creds/prepare_kernel_cred anyway. NOTE: `load_ko.c` resolves
  `kasan_flag_enabled` dynamically from kallsyms — do NOT hardcode an alias; on this
  kernel the old static alias pointed into `__jump_table` and wrote to ZERO_PAGE.)
- Module params present: `bootid_ctl`, `bootid_buf`, `pristine_policy`, `norc`,
  `allow_shell` → all three patches compiled in. `scmversion: g932014ab5b2c-dirty`
  (-dirty = patched tree).

## Deviations from build_inner.sh
1. Ran directly in WSL2 x86_64 (no container): toolchain via dpkg-deb extraction into
   `~/toolchain` instead of `apt-get install`.
2. KERNEL_SRC = AOSP tag worktree (not Honor opensource — none available for REA-NX9,
   and config proves pure-GKI provenance); verified zero offset drift.
3. `.config` whitelist path repointed (can't mkdir `/mnt/disks`); stub file created.
4. Added `HOSTCFLAGS`/`HOSTLDFLAGS` for extracted openssl headers (extract-cert).
5. Patches needed `sed 's/\r$//'` (CRLF in repo files) before `git apply`.
6. clang-14 vs device clang-12.0.5 — same as the original docker recipe (bookworm).
