#!/bin/sh

# Copyright 2012 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

. /usr/share/misc/chromeos-common.sh
. /usr/share/misc/lvm-utils.sh

# UNDO_MOUNTS stores the mounts that have currently been mounted. In case the
# startup operations fail (from unrecoverable state on disk), the mounts
# in UNDO_MOUNTS are unmounted in reverse order.
UNDO_MOUNTS=

# ENCRYPTED_STATEFUL_MNT stores the path to the initial mount point for the
# encrypted stateful partition.
ENCRYPTED_STATEFUL_MNT="/mnt/stateful_partition/encrypted"

# Flag file indicating that mount encrypted stateful failed last time.
# If the file is present and mount_encrypted failed again, machine would enter
# self-repair mode.
MOUNT_ENCRYPTED_FAILED_FILE="/mnt/stateful_partition/mount_encrypted_failed"

# USE_ENCRYPTED_REBOOT_VAULT determines whether the encrypted reboot vault
# should be created/mounted.
USE_ENCRYPTED_REBOOT_VAULT=1

# USE_LVM_STATEFUL_PARTITION determines whether the device should attempt
# to use the new LVM stateful partition format.
USE_LVM_STATEFUL_PARTITION=0

# TMPFILES_LOG defines the path that tmpfiles.d errors will be written to for
# debugging failures.
TMPFILES_LOG="/run/tmpfiles.log"

# Unmounts the incomplete mount setup during the failure path. Failure to
# set up mounts in this script result in the entire stateful partition getting
# wiped using clobber-state.
cleanup_mounts() {
  # On failure unmount all saved mount points and repair stateful
  for mount_point in ${UNDO_MOUNTS}; do
    if [ "${mount_point}" = "${ENCRYPTED_STATEFUL_MNT}" ]; then
      do_umount_var_and_home_chronos
    else
      umount -n "${mount_point}"
    fi
  done
  # Leave /mnt/stateful_partition mounted for clobber-state to handle.
  chromeos-boot-alert self_repair
  clobber-log -- \
    "Self-repair incoherent stateful partition: $*. History: ${UNDO_MOUNTS}"
  clobber-log --append_logfile "${TMPFILES_LOG}"
  crash_reporter --early --log_to_stderr --clobber_state
  exec clobber-state "fast keepimg"
}

# Adds mounts to UNDO_MOUNT.
remember_mount() {
    UNDO_MOUNTS="$1 ${UNDO_MOUNTS}"
}

# Used to mount essential mount points for the system from the stateful
# or encrypted stateful partition.
# On failure, clobbers the stateful partition.
mount_or_fail() {
  local mount_point
  # -c: Never canonicalize: it is a hazard to resolve symlinks.
  # -n: Do not write to mtab: we don't use it.
  if mount -c -n "$@" ; then
    # Last parameter contains the mount point
    shift "$(( $# - 1 ))"
    # Push it on the undo stack if we fail later
    remember_mount "$1"
    return
  fi
  cleanup_mounts "failed to mount $*"
}

# Assert that the argument is a directory.
# On failure, clobbers the stateful partition.
check_directory() {
  local path="$1"
  if [ -L "${path}" ] || [ ! -d "${path}" ]; then
    cleanup_mounts "${path} is not a directory"
  fi
}

# Checks if /var is close to being full.
# Returns exit code of 0 (boolean true) if there is less than 10MB of
# free space left in /var or if there are less than 100 inodes available on
# the underlying filesystem.
is_var_full() {
  local avail_space
  local avail_inodes
  avail_space="$(df -k --output=avail /var | grep -E -o '[0-9]+')"
  avail_inodes="$(df -k --output=iavail /var | grep -E -o '[0-9]+')"
  [ "${avail_space}" -lt 10000 ] || [ "${avail_inodes}" -lt 100 ]
}

# Returns if the TPM is owned or couldn't determine.
is_tpm_owned() {
  local tpm_owned
  # Depending on the kernel version, the file containing tpm owned information
  # can be in one of two locations. Specifically, for kernel versions 3.10 and
  # 3.14 the folder misc is used (/sys/class/misc/tpm0/device/owned). Starting
  # from version 3.18 the folder tpm is used.
  if [ -e /sys/class/misc/tpm0/device/owned ]; then
    tpm_owned="$(cat /sys/class/misc/tpm0/device/owned)"
  else
    tpm_owned="$(cat /sys/class/tpm/tpm0/device/owned)"
  fi
  [ "${tpm_owned}" != "0" ]
}

# Some startup functions are split into a separate library which may be
# different for different targets (e.g., regular Chrome OS vs. embedded).
. /usr/share/cros/startup_utils.sh

if [ -e /usr/share/cros/startup/disable_stateful_security_hardening ]; then
  DISABLE_STATEFUL_SECURITY_HARDENING="true"
else
  DISABLE_STATEFUL_SECURITY_HARDENING="false"
fi

# CROS_DEBUG equals one if we've booted in developer mode or we've
# booted a developer image.
crossystem "cros_debug?1"
CROS_DEBUG="$((! $?))"

# Developer mode functions (defined in dev_utils.sh and will be loaded
# only when CROS_DEBUG=1).
dev_check_block_dev_mode() { true; }
dev_gather_logs() { true; }
dev_mount_packages() { true; }
dev_is_debug_build() { false; }
dev_pop_paths_to_preserve() { true; }
dev_update_stateful_partition() { true; }

# do_* are wrapper functions that may be redefined in developer mode or test
# images. Find more implementation in {dev,test,factory}_utils.sh.
do_mount_var_and_home_chronos() { mount_var_and_home_chronos; }
do_umount_var_and_home_chronos() { umount_var_and_home_chronos; }

if [ "${CROS_DEBUG}" -eq 1 ]; then
  . /usr/share/cros/dev_utils.sh
fi

# Prepare to mount stateful partition
ROOT_DEV="$(rootdev -s)"
ROOTDEV_RET_CODE=$?
ROOT_DEV_DISK="$(rootdev -d -s)"
# Example root dev types we need to handle: /dev/sda2 -> /dev/sda,
# /dev/mmcblk0p0 -> /dev/mmcblk0p, /dev/ubi2_1 -> /dev/ubi
ROOTDEV_TYPE="$(echo "${ROOT_DEV}" | sed 's/[0-9_]*$//')"
ROOTDEV_NAME="${ROOT_DEV_DISK##/dev/}"
ROOTDEV_REMOVABLE="$(cat "/sys/block/${ROOTDEV_NAME}/removable")"

# Load the GPT helper functions and the image settings.
. "/usr/sbin/write_gpt.sh"
if [ "${ROOTDEV_REMOVABLE}" = "1" ]; then
  load_partition_vars
else
  load_base_vars
fi

# Path to the securityfs directory for configuring inode security policies.
LSM_INODE_POLICIES="/sys/kernel/security/chromiumos/inode_security_policies"

# Block symlink and FIFO access on the given path.
block_symlink_and_fifo() {
  printf "%s" "$1" > "${LSM_INODE_POLICIES}/block_symlink"
  printf "%s" "$1" > "${LSM_INODE_POLICIES}/block_fifo"
}

# Allow symlink access on the given path.
allow_symlink() {
  printf "%s" "$1" > "${LSM_INODE_POLICIES}/allow_symlink"
}

# Allow fifo access on the given path.
allow_fifo() {
  printf "%s" "$1" > "${LSM_INODE_POLICIES}/allow_fifo"
}

# Check if one string contains the other.
string_contains() {
  case "$1" in
    *"$2"*)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

# Check if we are booted on physical media. rootdev will fail if we are in
# an initramfs or tmpfs rootfs (ex, factory installer images. Note recovery
# image also uses initramfs but it never reach here). When using initrd+tftpboot
# (some old netboot factory installer), ROOTDEV_TYPE will be /dev/ram.
STATE_DEV=""
DEV_IMAGE=""
if [ "$ROOTDEV_RET_CODE" = "0" ] && [ "$ROOTDEV_TYPE" != "/dev/ram" ]; then
  # Find our stateful partition mount point.
  # To support multiple volumes on a single UBI device, if the stateful
  # partition is not found on ubi${PARTITION_NUM_STATE}_0, check
  # ubi0_${PARTITION_NUM_STATE}.
  if [ "${FORMAT_STATE}" = "ubi" ]; then
    STATE_DEV="/dev/ubi${PARTITION_NUM_STATE}_0"
    if [ ! -e "${STATE_DEV}" ]; then
      STATE_DEV="/dev/ubi0_${PARTITION_NUM_STATE}"
    fi
  else
      STATE_DEV="${ROOTDEV_TYPE}${PARTITION_NUM_STATE}"
  fi

  if [ "${DISABLE_STATEFUL_SECURITY_HARDENING}" = "false" ]; then
    # Block symlink traversal and opening of FIFOs on stateful. Note that we set
    # up exceptions for developer mode later on.
    block_symlink_and_fifo /mnt/stateful_partition
  fi

  # Mount the OEM partition.
  # mount_or_fail isn't used since this partition only has a filesystem
  # on some boards.
  OEM_FLAGS="ro,nodev,noexec,nosuid"
  if [ "${FORMAT_OEM}" = "ubi" ]; then
    OEM_DEV="/dev/ubi${PARTITION_NUM_OEM}_0"
    if [ ! -e "${OEM_DEV}" ]; then
      OEM_DEV="/dev/ubi0_${PARTITION_NUM_OEM}"
    fi
  else
    OEM_DEV="${ROOTDEV_TYPE}${PARTITION_NUM_OEM}"
  fi
  mount -n -t "${FS_FORMAT_OEM}" -o "${OEM_FLAGS}" "${OEM_DEV}" /usr/share/oem
fi

# This file is created by clobber-state after the transition
# to dev mode.
DEV_MODE_FILE="/mnt/stateful_partition/.developer_mode"

# Flag file indicating that encrypted stateful should be preserved across TPM
# clear. If the file is present, it's expected that TPM is not owned.
PRESERVATION_REQUEST_FILE="/mnt/stateful_partition/preservation_request"

# This file is created after the TPM is initialized and the device is owned.
INSTALL_ATTRIBUTES_FILE=\
"/mnt/stateful_partition/home/.shadow/install_attributes.pb"

# Checks if developer mode is blocked.
dev_check_block_dev_mode "${DEV_MODE_FILE}"

# File used to trigger a stateful reset.  Contains arguments for
# the "clobber-state" call.  This file may exist at boot time, as
# some use cases operate by creating this file with the necessary
# arguments and then rebooting.
RESET_FILE="/mnt/stateful_partition/factory_install_reset"

# Returns if device needs to clobber even though there's no devmode file present
# and boot is in verified mode.
needs_clobber_without_devmode_file() {
  ! is_tpm_owned && [ ! -O "${PRESERVATION_REQUEST_FILE}" ] &&
  [ -O "${INSTALL_ATTRIBUTES_FILE}" ]
}

# Check for whether we need a stateful wipe, and alert the user as
# necessary.  We can wipe for several different reasons:
#  + User requested "power wash" which will create ${RESET_FILE}.
#  + Switch from verified mode to dev mode.  We do this if we're in
#    dev mode, and ${DEV_MODE_FILE} doesn't exist.  clobber-state
#    in this case will create the file, to prevent re-wipe.
#  + Switch from dev mode to verified mode.  We do this if we're in
#    verified mode, and ${DEV_MODE_FILE} still exists.  (This check
#    isn't necessarily reliable.)
#
# Stateful wipe for dev mode switching is skipped if the build is a debug build
# or if we've booted a non-recovery image in recovery mode (for example, doing
# Esc-F3-Power on a Chromebook with DEV-signed firmware); this protects various
# development use cases, most especially prototype units or booting Chromium OS
# on non-Chrome hardware. And because crossystem is slow on some platforms, we
# want to do the additional checks only after verified DEV_MODE_FILE existence.
CLOBBER_ARGS=''
if [ -L "${RESET_FILE}" ] || [ -e "${RESET_FILE}" ]; then
  chromeos-boot-alert power_wash
  # If it's not a plain file owned by us, force a powerwash.
  if [ ! -O "${RESET_FILE}" ] || [ ! -f "${RESET_FILE}" ]; then
    CLOBBER_ARGS='keepimg'
  else
    CLOBBER_ARGS="$(cat "${RESET_FILE}")"
  fi
elif [ -z "${STATE_DEV}" ]; then
  # No physical stateful partition available, usually due to initramfs
  # (recovery image, factory install shim or netboot). Do not wipe.
  :
elif crossystem 'devsw_boot?0' && ! crossystem 'mainfw_type?recovery'; then
  if [ -O "${DEV_MODE_FILE}" ] || needs_clobber_without_devmode_file; then
    # We're transitioning from dev mode to verified boot.
    # When coming back from developer mode, we don't need to
    # clobber as aggressively.  Fast will do the trick.
    chromeos-boot-alert leave_dev
    CLOBBER_ARGS='fast keepimg'
    if [ -O "${DEV_MODE_FILE}" ]; then
      clobber-log -- "Leave developer mode, dev_mode file present"
    else
      clobber-log -- "Leave developer mode, no dev_mode file"
    fi

    # Only fast clobber the non-protected paths in debug build to preserve the
    # testing tools.
    if dev_is_debug_build; then
      dev_update_stateful_partition clobber
      reboot
      exit 0
    fi
  fi
elif crossystem 'devsw_boot?1' && ! crossystem 'mainfw_type?recovery'; then
  if [ ! -O "${DEV_MODE_FILE}" ]; then
    # We're transitioning from verified boot to dev mode.
    chromeos-boot-alert enter_dev
    CLOBBER_ARGS='keepimg'
    clobber-log -- "Enter developer mode"

    # Only fast clobber the non-protected paths in debug build to preserve the
    # testing tools.
    if dev_is_debug_build; then
      dev_update_stateful_partition clobber
      touch "${DEV_MODE_FILE}"
      reboot
      exit 0
    fi
  fi
fi

if [ -n "${CLOBBER_ARGS}" ]; then
  exec clobber-state "${CLOBBER_ARGS}"
fi

# Walk the specified path and reset any file attributes (like immutable bit).
force_clean_file_attrs() {
  local path="$1"

  # In case the dir doesn't yet exist.
  if [ ! -d "${path}" ]; then
    return
  fi

  # No physical stateful partition available, usually due to initramfs
  # (recovery image, factory install shim or netboot).  Do not check.
  if [ -z "${STATE_DEV}" ]; then
    return
  fi

  if ! file_attrs_cleaner_tool "${path}"; then
    chromeos-boot-alert self_repair
    clobber-log -- "Bad file attrs under ${path}"
    exec clobber-state "keepimg"
  fi
}

force_clean_file_attrs /mnt/stateful_partition/unencrypted

# Apply /mnt/stateful_partition specific tmpfiles.d configurations.
/bin/systemd-tmpfiles --create --remove --boot \
  --prefix /mnt/stateful_partition 2>>"${TMPFILES_LOG}" ||
    cleanup_mounts "tmpfiles.d for /mnt/stateful_partition failed"

# Mount /home.  This mount inherits nodev,noexec,nosuid from
# /mnt/stateful_partition above.
mount_or_fail --bind /mnt/stateful_partition/home /home
# Remount /home with nosymfollow: bind mounts do not accept the option
# within the same command.
mount -o remount,nosymfollow /home

if [ -f "/etc/init/tpm2-simulator.conf" ]; then
  initctl start tpm2-simulator || true
fi

# Clean up after a TPM firmware update. This must happen before mounting
# stateful, which will initialize the TPM again.
if [ -x "/usr/sbin/tpm-firmware-update-cleanup" ]; then
  /usr/sbin/tpm-firmware-update-cleanup
fi

if ! do_mount_var_and_home_chronos; then
  if [ ! -O "${MOUNT_ENCRYPTED_FAILED_FILE}" ]; then
    touch "${MOUNT_ENCRYPTED_FAILED_FILE}"
  else
    crossystem recovery_request=1
  fi
  reboot
  exit 0
fi
rm -f "${MOUNT_ENCRYPTED_FAILED_FILE}"
remember_mount "${ENCRYPTED_STATEFUL_MNT}"

# Setup the encrypted reboot vault once the encrypted stateful partition
# is available. If unlocking the encrypted reboot vault failed (due to power
# loss/reboot/invalid vault), attempt to recreate the encrypted reboot vault.
if [ "${USE_ENCRYPTED_REBOOT_VAULT}" -eq "1" ]; then
  if ! encrypted-reboot-vault --action=unlock; then
    encrypted-reboot-vault --action=create
  fi
fi

force_clean_file_attrs /var
force_clean_file_attrs /home/chronos

# If /var is too full, delete the logs so the device can boot successfully.
# It is possible that the fullness of /var was not due to logs, but that
# is very unlikely. If such a thing happens, we have a serious problem
# which should not be covered up here.
if is_var_full; then
  rm -r -f /var/log
  echo "Startup.ReclaimFullVar" > /mnt/stateful_partition/.reclaim_full_var
fi

# Gather logs if needed.  This might clear /var, so all init has to be after
# this.
dev_gather_logs

# Collect crash reports from early boot/mount failures.
crash_reporter --ephemeral_collect

if [ "${DISABLE_STATEFUL_SECURITY_HARDENING}" = "false" ]; then
  # Set up symlink traversal and FIFO blocking policy for /var, which may reside
  # on a separate file system than /mnt/stateful_partition. Block symlink
  # traversal and opening of FIFOs by default, but allow exceptions in the few
  # instances where they are used intentionally.
  block_symlink_and_fifo /var
  # Generic symlink exceptions.
  for symlink_exception in /var/cache/echo /var/cache/vpd /var/lib/timezone \
                           /var/log /home; do
    mkdir -p "${symlink_exception}"
    allow_symlink "${symlink_exception}"
  done
  # Project-specific symlink exceptions. Projects may add exceptions by adding a
  # file under /usr/share/cros/startup/symlink_exceptions/ whose contents
  # contains a list of paths (one per line) for which an exception should be
  # made. File name should use the following format:
  # <project-name>-symlink-exceptions.txt
  for path_file in /usr/share/cros/startup/symlink_exceptions/*; do
    if [ -f "${path_file}" ]; then
      while read -r path; do
        case "${path}" in
        # Ignore blank lines.
        "") ;;
        # Ignore comments.
        "#"*) ;;
        *)
          mkdir -p "${path}"
          allow_symlink "${path}"
          ;;
        esac
      done < "${path_file}"
    fi
  done
  # Project-specific FIFO exceptions. Projects may add exceptions by adding a
  # file under /usr/share/cros/startup/fifo_exceptions/ whose contents contains
  # a list of paths (one per line) for which an exception should be made. File
  # name should use the following format: <project-name>-fifo-exceptions.txt
  for path_file in /usr/share/cros/startup/fifo_exceptions/*; do
    if [ -f "${path_file}" ]; then
      while read -r path; do
        case "${path}" in
        # Ignore blank lines.
        "") ;;
        # Ignore comments.
        "#"*) ;;
        *)
          mkdir -p "${path}"
          allow_fifo "${path}"
          ;;
        esac
      done < "${path_file}"
    fi
  done
fi

# Apply /var and /home specific tmpfiles.d configurations.
/bin/systemd-tmpfiles --create --remove --boot \
  --prefix /home \
  --prefix /var 2>>"${TMPFILES_LOG}" ||
    cleanup_mounts "tmpfiles.d for /home and /var failed"

# Move from /var/lib/whitelist to /var/lib/devicesettings.
# Check whether folders are empty using rmdir, this will delete the respective
# folder only if it's empty.
# TODO(b/219506748): Remove the following lines by 2030 the latest. If there
# was a stepping stone for all boards in between, or the number of devices
# using a version that did not have this code is less than the number of
# devices suffering from disk corruption, code can be removed earlier.
if [ -d "/var/lib/whitelist" ] && ! rmdir /var/lib/whitelist && \
   rmdir /var/lib/devicesettings; then
  mv /var/lib/whitelist /var/lib/devicesettings
fi

# If we support efivarfs, mount the efivarfs interface for accessing
# EFI variables.
if grep -q efivarfs /proc/filesystems; then
  mount -t efivarfs -o nodev,noexec,nosuid efivarfs /sys/firmware/efi/efivars
fi

# /run is now tmpfs used for runtime data. Make sure /var/run and /var/lock are
# bind-mounted to /run and /run/lock respectively for backwards compatibility.
# tmpfiles.d recreates these each boot in case they were corrupted to point
# somewhere other than what we want.
mount -o bind /run /var/run
remember_mount /var/run
mount -o bind /run/lock /var/lock
remember_mount /var/lock

# Create daemon store folders.
# See https://chromium.googlesource.com/chromiumos/docs/+/HEAD/sandboxing.md#securely-mounting-cryptohome-daemon-store-folders.
for etc_daemon_store in /etc/daemon-store/*; do
  # If /etc/daemon-store is empty, $etc_daemon_store is /etc/daemon-store/*.
  # This if statement filters that out.
  if [ -d "$etc_daemon_store" ]; then
    daemon_name="${etc_daemon_store##*/}"
    run_daemon_store="/run/daemon-store/${daemon_name}"
    mkdir -p -m 0755 "${run_daemon_store}"
    mount -o bind --make-shared "${run_daemon_store}" "${run_daemon_store}"
  fi
done

# Remove /var/empty if it exists. Use /mnt/empty instead.
if [ -e /var/empty ]; then
  chattr -i /var/empty || :
  rm -rf /var/empty
fi

# Make sure that what gets written to /var/log stays in /var/log. Some notes on
# subtleties in the command below:
#  1. find's -exec with the '+' argument passes multiple found paths per shell
#     script invocation.
#  2. sh -c expects a command name before parameters, so we pass a dummy name.
#     Failure to do so would omit the first symlink since "$0" is not in "$@".
#  3. set -e causes the shell to bail out on rm failure, so we can err on the
#     safe side and do a stateful wipe instead of leaving symlinks around.
#  4. If readlink fails (e.g. due to dangling symlinks) grep will still return
#     false and so will the pipeline, so dangling symlinks get removed.
#  5. Find doesn't follow symlinks and we pass -xdev to make sure to only
#     examine symlinks in the /var/log subtree.
find /var/log -xdev -type l -exec sh -e -c '
  for path; do
    if ! readlink -f "${path}" | grep -q "^/var/log/"; then
      rm "${path}"
    fi
  done
' 'nuke_symlinks' '{}' '+'

# Bail and wipe on failure to remove a symlink.
if [ "$?" -ne 0 ]; then
  cleanup_mounts "Failed to remove symlinks under /var/log"
fi

# "--make-shared" to let ARC container access mount points under /media.
mount --make-shared -n -t tmpfs -o nodev,noexec,nosuid media /media
/bin/systemd-tmpfiles --create --remove --boot --prefix /media \
  2>>"${TMPFILES_LOG}" || :

# Restore file contexts for /var.
# TODO(fqj): use type_transition to correctly label directories at creation so
# relabel need only be started if SELinux policy updates.
if [ -f /sys/fs/selinux/enforce ]; then
  restorecon -R -D /var
  # Restoring file contexts for sysfs. debugfs and tracefs are excluded from
  # this invocation and delayed in separate jobs to improve boot time.
  restorecon -R /sys -e /sys/kernel/debug -e /sys/kernel/tracing
  # We cannot do recursive for .shadow since userdata is encrypted (including
  # file names) before user logs-in. Restoring context for it may mislabel files
  # if encrypted filename happens to match something.
  restorecon /home /home/.shadow /home/.shadow/* /home/.shadow/.* /home/.shadow/*/*
  # It's safe to recursvely restorecon /home/{user,root,chronos} since userdir
  # is not bind-mounted here before logging in.
  restorecon -R -D /home/user /home/root /home/chronos
fi

# Mount dev packages.
dev_mount_packages "${DEV_IMAGE}"
dev_pop_paths_to_preserve

# Skip rollback restore check if not supported on this device.
if [ -x "$(command -v rollback_finish_restore)" ]; then
  # If a rollback is in progress, restores preserved data. If restoring fails,
  # the TPM is reset.
  # If a rollback is not in progress, rollback_finish_restore cleans up restore
  # data and exits.
  if ! rollback_finish_restore; then
    rm /mnt/stateful_partition/unencrypted/preserve/rollback_data
    crossystem clear_tpm_owner_request=1
    reboot
  fi
fi

# Always return success to avoid killing init
exit 0
