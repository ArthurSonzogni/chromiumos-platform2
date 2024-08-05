#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Setup a python3 virtual environment for running the study_serve.py.
# This will install the require dependencies automatically.
#
# Usage: python-venv-setup.sh [venv-path]
#
# The default venv-path is ".venv" inside of biod/study directory.

main() {
    local loc="$1"

    local script_dir
    if ! script_dir="$(dirname "${BASH_SOURCE[0]}")"; then
        echo "Error - Failed to get script's directory." >&2
        exit 1
    fi

    # Default the location to ".venv" in the study directory.
    if [[ -z "${loc}" ]]; then
        loc="${script_dir}/.venv"
    fi

    # Check whether python3-venv package exists.
    if ! python3 -c 'import venv; import ensurepip' &>/dev/null; then
        echo "Error - Python3 module venv or ensurepip is missing"
        echo
        echo "On Debian, you could do the following:"
        echo "sudo apt install -y python3-venv"
        exit 1
    fi

    # Create new python3 virtual environment.
    rm -rf "${loc}"
    if ! python3 -m venv "${loc}"; then
        echo "Error - Failed to setup a python virtualenv." >&2
        exit 1
    fi

    # Install dependencies in the new virtual environment.
    local requirements=(
        "${script_dir}/requirements.txt"
        "${script_dir}/analysis-tool/requirements.txt"
    )
    local req
    for req in "${requirements[@]}"; do
        if ! "${loc}/bin/pip3" install -r "${req}"; then
            echo "Error - Failed to install python dependencies '${req}'." >&2
            exit 1
        fi
    done

    # Setup gitignore.
    echo -e "# This directory should not be committed.\n*" >"${loc}/.gitignore"
}

main "$@"
