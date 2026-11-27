#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

[[ -e .git ]] || fail "Create the source archive from a Git checkout."

mkdir -p build
staging=$(mktemp -d build/.source-archive.XXXXXXXX)
trap 'rm -rf -- "$staging"' EXIT
source_dir=$staging/ctr-uefi-source
mkdir -p "$source_dir"

# The build uses working-tree files, including in a repository without HEAD.
git ls-files -z --cached --others --exclude-standard --deduplicate -- \
    .gitignore .gitmodules CtrPkg LICENSE Licenses Makefile NOTICE README.md Tests \
    build.sh loader patches tools > "$staging/project-paths"
while IFS= read -r -d '' path; do
    if [[ -f "$path" || -L "$path" ]]; then
        printf '%s\0' "$path"
    fi
done < "$staging/project-paths" |
    tar --create --no-recursion --null --files-from=- |
    tar --extract --directory="$source_dir"

archive_submodule() {
    local directory=$1 entry mode revision stage changes
    local extra_inputs=()
    shift
    entry=$(git ls-files --stage -- "$directory")
    read -r mode revision stage _ <<< "$entry"
    [[ "$mode" == 160000 && "$stage" == 0 && "$entry" != *$'\n'* ]] ||
        fail "The index must contain a submodule pin for $directory."
    [[ -e "$directory/.git" && $(git -C "$directory" rev-parse HEAD) == "$revision" ]] ||
        fail "Run git submodule update --init -- $directory to obtain commit $revision."
    if [[ "$directory" == third_party/edk2 ]]; then
        # BaseTools can use local overrides instead of the archived templates.
        extra_inputs=(Conf/{target.txt,tools_def.txt,build_rule.txt,FrameworkDatabase.db})
    fi
    changes=$(git -C "$directory" status --porcelain --untracked-files=all --ignored -- "$@" "${extra_inputs[@]}")
    [[ -z "$changes" ]] || fail "Source archive inputs in $directory must match the pinned source."
    mkdir -p "$source_dir/$directory"
    git -C "$directory" archive "$revision" -- "$@" |
        tar --extract --directory="$source_dir/$directory"
}

archive_submodule third_party/edk2 \
    edksetup.sh Conf/ReadMe.txt ArmPkg EmbeddedPkg MdeModulePkg MdePkg \
    IntelFrameworkPkg/{Include,IntelFrameworkPkg.dec,License.txt} \
    IntelFrameworkModulePkg/{Include,IntelFrameworkModulePkg.dec,License.txt} \
    BaseTools/BuildEnv BaseTools/BinWrappers/PosixLike BaseTools/Conf BaseTools/License.txt \
    BaseTools/Source/C/{GNUmakefile,Common,GenCrc32,GenFfs,GenFv,GenFw,GenSec,Include,LzmaCompress,Makefiles,TianoCompress,VolInfo} \
    BaseTools/Source/Python ':!BaseTools/Source/Python/UPT' ':!*.pyd' ':!*.lib'
archive_submodule third_party/edk2-fat FatPkg

tar --create --gzip --file="$staging/ctr-uefi-source.tar.gz" --directory="$staging" ctr-uefi-source
mv -- "$staging/ctr-uefi-source.tar.gz" build/ctr-uefi-source.tar.gz
printf 'Created %s\n' build/ctr-uefi-source.tar.gz
