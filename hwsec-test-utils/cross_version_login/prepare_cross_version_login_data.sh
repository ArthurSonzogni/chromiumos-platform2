#!/bin/bash

# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This bash scripts is used for creating the cross-version login testing data.
# Given the target CrOS version, the script will download the image from
# google storage and create user account in the image. Then, copy and upload
# the data to google storage so that we could use it in cross-version login
# testing.

error() {
  echo "[Error] $(basename "$0"): $*" >&2
}

die() {
  error "$*"
  exit 1
}

usage() {
  die "Usage: $(basename "$0") <board> <version> <output_dir>\
[<ssh_identity_file>]"
}

# Get a temporary copy of keyfile and set proper permission to it, so that we
# could use it to ssh DUT.
# Arguments: path of keyfile source, path of keyfile copy.
setup_keyfile() {
  local keyfile_src="$1"
  local keyfile="$2"
  if ! cp "${keyfile_src}" "${keyfile}" ; then
    error "failed to cp '${keyfile_src}' to '${keyfile}'"
    return 1
  fi
  if ! chmod 400 "${keyfile}" ; then
    error "failed to chmod keyfile '${keyfile}'"
    return 1
  fi
  return 0
}

# Generates the content of the external data file for tast-tests.
# Arguments: gs url of external data, file path of local data.
generate_external_data_file() {
  local gs_url="$1"
  local filepath="$2"
  # Generate the external data file.
  cat <<EOF
{
  "url": "${gs_url}",
  "size": $(stat -c %s "${filepath}"),
  "sha256sum": "$(sha256sum "${filepath}" | cut -d ' ' -f 1)"
}
EOF
}

# Gets the image of target version from Google Cloud Storage.
# Arguments: Google Storage URL of the image.
fetch_image() {
  local gs_url="$1"

  local compressed_image="${TMP_DIR}/image.tar.xz"
  if ! gsutil cp "${gs_url}" "${compressed_image}"; then
    error "gsutil failed to cp '${gs_url}' '${compressed_image}'"
    return 1
  fi
  # The filename of image is "chromiumos_test_image.bin".
  if ! tar Jxf "${compressed_image}" -C "${TMP_DIR}" ; then
    error "failed to decompress the '${compressed_image}'"
    return 1
  fi
  return 0
}

# Copy the data from the DUT to the local path.
# Arguments: data path on the DUT, local data path.
copy_from_dut() {
  local remote_path=$1
  local local_path=$2
  scp -o StrictHostKeyChecking=no -o GlobalKnownHostsFile=/dev/null \
      -o UserKnownHostsFile=/dev/null -o LogLevel=quiet -i "${KEYFILE}" \
      -P "${PORT}" "root@${HOST}:${remote_path}" "${local_path}" 1>/dev/null
}

# Gets data from the DUT and uploads the data to the Google Cloud Storage
# Arguments: prefix for naming the data,
#            output directory for config and external data file.
upload_data() {
  local prefix="$1"
  local output_dir="$2"
  local remote_data_dir="/tmp/cross_version_login"

  local data_name="${prefix}_data.tar.gz"
  local data_path="${TMP_DIR}/${data_name}"
  local remote_data_path="${remote_data_dir}/data.tar.gz"

  local config_name="${prefix}_config.json"
  local config_path="${output_dir}/${config_name}"
  local remote_config_path="${remote_data_dir}/config.json"

  local external_file="${output_dir}/${data_name}.external"
  local gs_url="gs://chromiumos-test-assets-public/tast/cros/hwsec/\
cross_version_login/${data_name}"

  if ! copy_from_dut "${remote_data_path}" "${data_path}"
  then
    error "failed to scp the file '${data_path}'"
    return 1
  fi

  if ! copy_from_dut "${remote_config_path}" "${config_path}"
  then
    error "failed to scp the file '${config_path}'"
    return 1
  fi
  echo "Config file is created at '${config_path}'"
  if ! generate_external_data_file "${gs_url}" "${data_path}" \
      > "${external_file}"
  then
    error "failed to write the file '${external_file}'"
    return 1
  fi
  echo "External data file is created at '${external_file}'"
  if ! gsutil cp "${data_path}" "${gs_url}" ; then
    error "gsutil failed to cp '${data_path}' '${gs_url}'"
    return 1
  fi
  echo "Testing data is uploaded to '${gs_url}'"
  return 0
}

# Generates the data file on DUT for cross-version login testing.
generate_data() (
  local test_name="hwsec.PrepareCrossVersionLoginData"
  # "tpm2_simulator" is added by crrev.com/c/3312977, so this test cannot run
  # on older version. Therefore, adds -extrauseflags "tpm2_simulator" here.
  if ! tast run -failfortests -extrauseflags "tpm2_simulator" \
      "${HOST}:${PORT}" "${test_name}" 1>"/dev/null"; then
    error "tast failed to run ${test_name}"
    return 1
  fi
  return 0
)

main() {
  local default_key_file="${HOME}/trunk/chromite/ssh_keys/testing_rsa"

  # Directory for local temporary data.
  TMP_DIR="$(mktemp -d)" || die "failed to mktemp"
  # Host, port and ssh identity file for ssh to the VM.
  HOST="127.0.0.1"
  PORT="9222"
  KEYFILE="${TMP_DIR}/testing_rsa"

  local board="$1"
  local version="$2"
  local output_dir="$3"
  local keyfile_src="${4-"${default_key_file}"}"

  test -f "${keyfile_src}" || die "keyfile '${keyfile_src}' does not exist"
  test -n "${board}" || usage
  test -n "${version}" || usage

  local date="$(date +"%Y%m%d")"
  local prefix="${version}_${board}_${date}"
  local image_url="gs://chromeos-image-archive/${board}-release/${version}/\
chromiumos_test_image.tar.xz"
  local image="${TMP_DIR}/chromiumos_test_image.bin"

  if setup_keyfile "${keyfile_src}" "${KEYFILE}" \
     && fetch_image "${image_url}" ;
  then
    if cros_vm --log-level=warning --start --image-path="${image}" \
               --board="${board}" ;
    then
      if generate_data ; then
        upload_data "${prefix}" "${output_dir}"
      fi
      cros_vm --log-level=warning --stop
    else
      error "cros_vm failed to start"
    fi
  fi
  rm -rf "${TMP_DIR}"
}

main "$@"
