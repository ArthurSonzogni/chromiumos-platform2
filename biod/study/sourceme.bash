#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Source this bash script from your shell to enable the Python environment
# for all study related Python and tools.
#
# Usage:
# $ . sourceme.bash

# The goal is to simply automate the simple Python shell environment
# activation and tool PATH inclusion.
# - Do not place additional configuration env variables in this script.
# - This should not become part of a startup protocol and configuration.
# - Don't use the STUDY_DIR variable elsewhere.

_study_env_setup() {
    local STUDY_DIR
    STUDY_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
    local STUDY_VENV_DIR="${STUDY_DIR}/.venv"

    if [[ ! -d "${STUDY_VENV_DIR}" ]]; then
        echo "Error - The Python venv has not been setup." >&2
        echo >&2
        echo "Please run python-venv-setup.sh, first." >&2
        return 1
    fi

    # Shellcheck will not be able to "follow" the following include, since
    # there is variable indirection. Even if it could, this source file may
    # not exist when being checked.
    # shellcheck disable=SC1091
    . "${STUDY_VENV_DIR}/bin/activate"
    PATH="${STUDY_DIR}/analysis-tool:${PATH}"
}

_study_env_setup
unset _study_env_setup
