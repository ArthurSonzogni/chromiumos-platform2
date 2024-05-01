#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# ./build.sh [build-config-dir]
#
# Example:
# ./build.sh hatch-bloonchipper-test
set -e

# Do all Prerequisites first from
# https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#Prerequisites

# (outside)

# Apply argument.
CONFIG="${1:-brya-latest-test}"

echo "# Building ${CONFIG} ####################################################"

out="$(realpath "${CONFIG}")"

source "${out}/build_config.sh"

ROOT=~/chromiumos-fpstudy-${BOARD}-${BRANCH}
WORKON_PKGS=( )

# Reference Chromiumos Checkout (speeds up the checkout process)
REF=~/chromiumos
# If you have an extra SSD that you would like to use for the chroot
CHROOT_DST="/var/extra"
CHROOT_DST_DIR="${CHROOT_DST}/$(basename "${ROOT}")-chroot"

mkdir -p "${ROOT}"
cd "${ROOT}"

REPO_OPTS=( )
REPO_OPTS+=( -u https://chrome-internal.googlesource.com/chromeos/manifest-internal.git )
#REPO_OPTS+=( --repo-url https://chromium.googlesource.com/external/repo.git )
if [[ -n "${REF}" && -d "${REF}" ]]; then
	REPO_OPTS+=( --reference "${REF}" )
fi
REPO_OPTS+=( -b "${BRANCH}" )
repo init "${REPO_OPTS[@]}"

# Repo sync -d failed to move checkout from a release branch to main, so we will
# simply run repo sync without -d, first.
repo sync -j$(( 2 * $(nproc) ))
repo sync -j$(( 2 * $(nproc) )) -d
repo abandon fpstudy || echo "Failing is okay, it's just a fact of life..."
# So that we can run "repo sync" to update the release branch
repo start fpstudy --all

# Finds and apply all changes that only apply to the list of files.
#
# Usage: cherry_pick_updates <dir> <remote> <latest-refspec> [file1 [files...]]
#
# The file paths are relative to |dir|.
cherry_pick_updates() {
	local dir="$1"
	local remote="$2"
	local latest="$3" # "cros/main" to force latest"
	shift 3
	local files=( "$@" )

	echo "# Dir ${dir}"
	pushd "${dir}"
	if ! git fetch $remote $latest; then
		echo "Error fetching"
		popd
		return 1
	fi
	local hashes=( )
	if ! hashes+=( $(git rev-list --reverse HEAD..${latest} -- "${files[@]}") ); then
		echo "Error getting rev-list"
		popd
		return 1
	fi
	echo "# hashes = ${hashes[*]}"
	if [ ${#hashes[@]} -gt 0 ]; then
		if ! git cherry-pick "${hashes[@]}"; then
			echo "Error cherry-picking"
			popd
			return 1
		fi
	fi
	popd
}

# Cherry-pick the list of commits/refspecs from the remote.
#
# Usage: cherry_pick <dir> <remote> [refspec [refspecs...]]
cherry_pick() {
	local dir="$1"
	local remote="$2"
	shift 2

	echo "# Dir ${dir}"
	pushd "${dir}"
	for commit; do
		if ! git fetch "${remote}" "${commit}"; then
			echo "Error fetching"
			popd
			return 1
		fi
		if ! git cherry-pick FETCH_HEAD; then
			echo "Error cherry-picking"
			popd
			return 1
		fi
	done
	popd
}

# Chromite "cros" command changes for build-packages and build-image
# haven't been properly backported to release branches. So, fast forward
# the chromite repo.
# echo "# Force updating chromite for cros command functionality."
# git -C chromite fetch
# git -C chromite reset --hard cros/main

WORKON_PKGS+=( fingerprint_study )

# Checkout platform2/biod/study
dir=src/platform2/biod/study
# FINGERPRINT_STUDY_CHANGES=cros/main
# FINGERPRINT_STUDY_CHANGES=b1ee162959349067af5d52dcbcab607706bf77ea
if [[ -n "${FINGERPRINT_STUDY_CHANGES}" ]]; then
	cherry_pick_updates "${dir}" cros "${FINGERPRINT_STUDY_CHANGES}" .
fi

# Force encryption enable
if "${ENABLE_ENCRYPTION}"; then
	# Modify study server upstart config to enable encryption.
	cherry_pick "${dir}" cros refs/changes/05/2651005/4
fi

# Checkout chromiumos-overlay/.../fingerprint_study
dir=src/third_party/chromiumos-overlay/chromeos-base/fingerprint_study
#latest=cros/main
#latest=e65ab2a38df32d75f7238039bd81be42b3d225dd
if "${ENABLE_ENCRYPTION}"; then
	# Change ebuild to install encryption key / recipients list files.
	#cherry_pick_updates $dir cros $latest ./fingerprint_study-9999.ebuild
	cherry_pick "${dir}" cros refs/changes/82/2650782/2
fi

# Apply build_config.sh specific source changes.
config_apply_src_changes

if "${ENABLE_ENCRYPTION}"; then
	cp "${KEYS[@]}" "${ROOT}/src/platform2/biod/study"
fi

# Checklist:
# Need updates to remove python2 from packages.use profile thing.
# Need to ensure that virtual/target... has fpstudy USE flag switch
# Update study params src/platform2/biod/study/init/fingerprint_study.conf
# Add public key to src/platform2/biod/study/chromeos-fpstudy-public-device.gpg
# Add recipients to src/platform2/biod/study/chromeos-fpstudy-recipients.txt

# Enabled different chroot directory linking
if [[ -d "${CHROOT_DST}" ]]; then
	sudo rm -rf "${CHROOT_DST_DIR}"
	mkdir "${CHROOT_DST_DIR}"
	ln -s "${CHROOT_DST_DIR}" "${ROOT}/chroot"
fi

# Remove /google/* directories from PATH
# to avoid http://b/233944700#comment52 .
# PATH="$(echo $PATH | sed -e 's\:/google[^:]*\\')"
PATH="$(echo $PATH | sed -E 's%(^\/google[^:]*:)|(:\/google[^:]*)%%g')"

# Clear the previous SDK, since case we downgraded the release version.
command cros_sdk --delete
# --nouse-image is the default now
command cros_sdk -- setup_board --board="${BOARD}"
echo "# cros workon ###########################################################"
if [[ ${#WORKON_PKGS[@]} -gt 0 ]]; then
	# command cros_sdk -- cros-workon-$BOARD start ${WORKON_PKGS[*]}
	cros workon --board="${BOARD}" start "${WORKON_PKGS[@]}"
fi

echo "# cros build-packages ###################################################"

USE_FLAGS=( fpstudy )
if [[ "${IMAGE_TYPE}" == "test" ]]; then
	USE_FLAGS+=( login_enable_crosh_sudo )
fi

BUILD_FLAGS=( )
# We could add the --chrome, but that might make build take longer.
# The 115 and 116 release branches wouldn't build without --chrome flag.
# Note: cros "build" was the only version of build-packages.
# BUILD_FLAGS+=( --internal )
# BUILD_FLAGS+=( --chrome )
# USE_FLAGS+=( chrome_internal )

echo USE="${USE_FLAGS[*]}" cros build-packages --board="${BOARD}" "${BUILD_FLAGS[@]}"
USE="${USE_FLAGS[*]}" cros build-packages --board="${BOARD}" "${BUILD_FLAGS[@]}"

echo "# cros build-image ######################################################"
echo USE="${USE_FLAGS[*]}" cros build-image --board="${BOARD}" "${BUILD_FLAGS[@]}" "${IMAGE_OPTS[@]}" "${IMAGE_TYPE}"
USE="${USE_FLAGS[*]}" cros build-image --board="${BOARD}" "${BUILD_FLAGS[@]}" "${IMAGE_OPTS[@]}" "${IMAGE_TYPE}"

echo "# cros flash ############################################################"
cros flash "file://${out}/fpstudy-image-${BOARD}-${BRANCH}.bin" "${BOARD}/latest"

# Follow the Installing Chromium OS on your Device section from
# https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#installing-chromium-os-on-your-device
