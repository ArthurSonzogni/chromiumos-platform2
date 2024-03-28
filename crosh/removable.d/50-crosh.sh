#!/bin/dash
# Copyright 2009-2010 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Crosh commands that are only loaded when we're booting from removable
# media.

do_install() {
  local CHROMEOS_INSTALL="/usr/sbin/chromeos-install"

  if [ -n "$1" ]; then
    if [ "$(expr "$1" : '^/dev/[[:alnum:]]\+$')" = 0 ]; then
      help "invalid device name: $1"
      return 1
    fi

    local dst="$1"
    shift

    "${CHROMEOS_INSTALL}" --dst="${dst}" "$@"
  else
    "${CHROMEOS_INSTALL}" "$@"
  fi
}

# shellcheck disable=SC2034
USAGE_install='[<dev>]'
# shellcheck disable=SC2034
HELP_install='
  Install ChromeOS to the target device, clearing out all existing data.
'
cmd_install() (
  do_install "$1"
)

# shellcheck disable=SC2034
USAGE_upgrade='[<dev>]'
# shellcheck disable=SC2034
HELP_upgrade='
  Upgrade an existing ChromeOS installation, saving existing user data.
'
cmd_upgrade() (
  do_install "$1" --preserve_stateful
)
