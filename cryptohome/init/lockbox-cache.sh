#!/bin/sh
# Copyright 2016 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

echo "Lockbox cache started"

umask 022
mkdir -p -m 0711 $LOCKBOX_CACHE_DIR

# Exit the script if any command fails.
set -e

# Look for the existing install attributes.
# If there's any, move them to new path.
# Note: this whole process is kept as much fault-tolerant as possible.
if [ -s $OLD_INSTALL_ATTRS_FILE ]; then
  if [ ! -s $NEW_INSTALL_ATTRS_FILE ]; then
    echo "Migrating install attributes"
    # First, create a copy to the new location, then rename it.
    # If the copy/rename operation somehow gets interrupted (sudden
    # reboot), the old install_attributes.pb file will still be there at
    # the next reboot.
    # So, it will reach this step again and eventually continue from here.
    mkdir -p $INSTALL_ATTRS_NEW_PATH
    sync
    cp $OLD_INSTALL_ATTRS_FILE $COPY_INSTALL_ATTRS_FILE
    sync
    mv $COPY_INSTALL_ATTRS_FILE $NEW_INSTALL_ATTRS_FILE
    sync
  fi
  if [ -s $NEW_INSTALL_ATTRS_FILE ]; then
    echo "Removing stale install attributes"
    # It's time for removal.
    rm $OLD_INSTALL_ATTRS_FILE
    sync
  fi
fi

# /sbin/mount-encrypted emits the TPM NVRAM contents, if they exist, to a
# file on tmpfs which is used to authenticate the lockbox during cache
# creation.
if [ -O $LOCKBOX_NVRAM_FILE ]; then
  echo "Validating lockbox"
  lockbox-cache --cache=$INSTALL_ATTRS_CACHE \
                --nvram=$LOCKBOX_NVRAM_FILE \
                --lockbox=$NEW_INSTALL_ATTRS_FILE
  # There are no other consumers; remove the nvram data
  rm $LOCKBOX_NVRAM_FILE
fi
