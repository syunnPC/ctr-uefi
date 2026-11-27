#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
root=$PWD
deps=$root/.deps
out=$root/build
edk=$deps/edk2-work
edk_revision=3ccd6d8b75b7fab66a35da9e77312eaca167c0f0
fat_revision=ee3e5604cf2e3b79075cda62986b1fa627f21e09
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
cross_compile=${CROSS_COMPILE:-arm-none-eabi-}

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

resolve_command() {
    local executable
    executable=$(command -v "$1") || return
    printf '%s/%s\n' "$(cd -- "$(dirname -- "$executable")" && pwd)" "$(basename -- "$executable")"
}

if [[ -d "$deps/toolchain/usr/bin" ]]; then
    export PATH="$deps/toolchain/usr/bin:$PATH"
fi

cross_compile=$(resolve_command "${cross_compile}gcc") ||
    fail "Install the arm-none-eabi toolchain or set CROSS_COMPILE."
cross_compile=${cross_compile%gcc}

prepare_source() {
    local source=$1 revision=$2 required_file=$3
    if [[ -e "$root/.git" ]]; then
        if [[ ! -e "$root/$source/.git" ]]; then
            git submodule update --init -- "$source"
        fi
        [[ $(git -C "$root/$source" rev-parse HEAD) == "$revision" ]] ||
            fail "The submodule at $source must be at $revision."
    fi
    [[ -f "$root/$source/$required_file" ]] ||
        fail "Missing $source/$required_file. Use a checkout with submodules or a complete source archive."
}

archive_source() {
    local source=$1 entry=$2
    tar -C "$root/$source" --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
        --exclude='./.git' -cf - "$entry"
}

populate_workspace() (
    local source_key=$1 staging edk_patch
    staging=$(mktemp -d "$deps/edk2-work.XXXXXXXX")
    trap 'rm -rf -- "$staging"' EXIT

    archive_source third_party/edk2 . | tar -xmf - -C "$staging"
    archive_source third_party/edk2-fat FatPkg | tar -xmf - -C "$staging"
    ln -s -- ../../CtrPkg "$staging/CtrPkg"

    # Apply patches outside the parent repository and leave the submodules pristine.
    for edk_patch in "${edk_patches[@]}"; do
        GIT_CEILING_DIRECTORIES="$deps" git -C "$staging" apply "$edk_patch"
    done

    printf '%s\n' "$source_key" > "$staging/.ctr-source-key"
    if [[ -d "$edk" ]]; then
        rm -rf -- "$edk"
    fi
    mv -- "$staging" "$edk"
)

mkdir -p "$out" "$deps"
prepare_source third_party/edk2 "$edk_revision" edksetup.sh
prepare_source third_party/edk2-fat "$fat_revision" FatPkg/FatPkg.dec

edk_patches=(
    "$root/patches/armv6k.patch"
    "$root/patches/armv6k-exceptions.patch"
    "$root/patches/dxe-progress.patch"
    "$root/patches/exception-progress.patch"
    "$root/patches/genfw-relative-codeview.patch"
)
if [[ -L "$edk" || ( -e "$edk" && ! -f "$edk/.ctr-source-key" ) ]]; then
    fail "Move $edk aside to let the build create its workspace."
fi
source_key=$(
    set -e
    printf '%s\n' "$edk_revision" "$fat_revision"
    sha256sum "${edk_patches[@]}"
    archive_source third_party/edk2 . | sha256sum
    archive_source third_party/edk2-fat FatPkg | sha256sum
)
if [[ ! -f "$edk/.ctr-source-key" || $(<"$edk/.ctr-source-key") != "$source_key" ]]; then
    populate_workspace "$source_key"
fi

check_python2() {
    "$1" -c 'import sqlite3, sys, zlib; assert sys.version_info[:2] == (2, 7)'
}

python2=${PYTHON2:-}
if [[ -n "$python2" ]]; then
    python2=$(resolve_command "$python2") || fail "PYTHON2 must point to a usable Python 2.7 executable."
    check_python2 "$python2" || fail "PYTHON2 requires Python 2.7 with sqlite3 and zlib."
elif [[ -x "$deps/python2/bin/python" ]] && check_python2 "$deps/python2/bin/python"; then
    python2=$deps/python2/bin/python
elif command -v python2 >/dev/null && check_python2 python2; then
    python2=$(resolve_command python2)
else
    version=2.7.18
    archive=$deps/Python-$version.tgz
    checksum=da3080e3b488f648a3d7a4560ddee895284c3380b11d6de75edb986526b9a814
    if [[ ! -f "$archive" ]]; then
        curl -fL "https://www.python.org/ftp/python/$version/Python-$version.tgz" -o "$archive.tmp"
        mv -- "$archive.tmp" "$archive"
    fi
    if ! printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --status; then
        fail "Python source checksum failed for $archive."
    fi
    tar -xzf "$archive" -C "$deps"
    (
        cd "$deps/Python-$version"
        CC='gcc -std=gnu11' ./configure --prefix="$deps/python2"
        make -j "$jobs"
        make install
    )
    python2=$deps/python2/bin/python
    check_python2 "$python2" ||
        fail "Python 2.7 requires SQLite and zlib development headers. Install them or set PYTHON2."
fi
mkdir -p "$deps/edk-python"
ln -sfn -- "$python2" "$deps/edk-python/python"

basetools=$edk/BaseTools/Source/C
mkdir -p "$basetools/bin" "$basetools/libs"
host_cflags='-O2 -std=gnu11 -fcommon -fshort-wchar -fno-strict-aliasing -fno-merge-constants -c'
make -C "$basetools" -j "$jobs" Common CFLAGS="$host_cflags"
make -C "$basetools" -j "$jobs" GenFfs GenFv GenFw GenSec GenCrc32 LzmaCompress TianoCompress VolInfo CFLAGS="$host_cflags"
(
    cd "$edk"
    unset WORKSPACE EDK_TOOLS_PATH
    export PATH="$deps/edk-python:$PATH"
    export GCC48_ARM_PREFIX="$cross_compile"
    set +u
    source ./edksetup.sh BaseTools
    set -u
    build -p CtrPkg/Ctr.dsc -a ARM -t GCC48 -b RELEASE -n "$jobs"
)

make -C loader CROSS_COMPILE="$cross_compile" BUILD_DIR="$out/loader"
cp -- "$edk/Build/Ctr/RELEASE_GCC48/FV/CTR.fd" "$out/ctr-uefi.fd"
python3 Tests/ctr_fv_test.py "$out/ctr-uefi.fd"
cpu_dxe=$edk/Build/Ctr/RELEASE_GCC48/ARM/ArmPkg/Drivers/CpuDxe/CpuDxe
python3 Tests/ctr_exception_test.py "${cross_compile}objdump" "$cpu_dxe/OUTPUT/ArmV6/ExceptionSupport.obj"
python3 tools/mkfirm.py --arm9 "$out/loader/arm9.bin" --arm11 "$out/loader/arm11.bin" \
    --fd "$out/ctr-uefi.fd" --output "$out/ctr-uefi.firm"
printf 'Built %s\n' "$out/ctr-uefi.firm"
