#!/bin/sh

# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
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

# Check the date in case the RTC battery died and it initialized to something
# old (https://crbug.com/195715).  This doesn't need to be perfect, just
# somewhat recent.  We'll recover later via tlsdate.
check_clock() {
  # We manage this base timestamp by hand.  It isolates us from bad clocks on
  # the system where this image was built/modified, and on the runtime image
  # (in case a dev modified random paths while the clock was out of sync).
  # Calculated using: date -d"01 Jan $(date +%Y) UTC" +%s
  local year="2021"
  local base_secs="1609459200"

  # See if the current time is older than our fixed time.  If so, pull up.
  if [ "$(date -u +%s)" -lt "${base_secs}" ]; then
    date -u "01020000${year}"
  fi
}

add_clobber_crash_report() {
  crash_reporter --early --log_to_stderr --mount_failure --mount_device="$1"
  sync
}

# Make sure our clock is somewhat up-to-date.  We don't need any resources
# mounted below, so do this early on.
check_clock

# bootstat writes timings to tmpfs.
bootstat pre-startup

mount -n -i -c -t debugfs \
  -o nodev,noexec,nosuid,mode=0750,uid=0,gid=debugfs-access \
  debugfs /sys/kernel/debug

# /sys/kernel/debug/tracing/tracing_on is 1 right after tracefs is initialized
# in the kernel. Set it to a reasonable initial state of 0 after debugfs is
# mounted. This needs to be done early during boot to avoid interference with
# ureadahead that uses ftrace to build the list of files to preload in the block
# cache. Android's init running in the ARC++ container sets this file to 0, and
# we set it to 0 here so the the initial state of tracing_on is always 0
# regardless of ARC++.
if [ -e /sys/kernel/debug/tracing/tracing_on ]; then
  echo 0 > /sys/kernel/debug/tracing/tracing_on
fi

# Some startup functions are split into a separate library which may be
# different for different targets (e.g., regular Chrome OS vs. embedded).
. /usr/share/cros/startup_utils.sh

mkdir -p /dev/pts /dev/shm
mount -n -i -c -t tmpfs -o nodev,noexec,nosuid shmfs /dev/shm
mount -n -i -c -t devpts \
  -o noexec,nosuid,gid=5,mode=0620,ptmxmode=0666 devpts /dev/pts

# Mount configfs, if present.
if [ -d /sys/kernel/config ]; then
  mount -n -i -c -t configfs -o nodev,nosuid,noexec configfs \
    /sys/kernel/config
fi

if [ -e /usr/share/cros/startup/disable_stateful_security_hardening ]; then
  DISABLE_STATEFUL_SECURITY_HARDENING="true"
else
  DISABLE_STATEFUL_SECURITY_HARDENING="false"
fi

if [ "${DISABLE_STATEFUL_SECURITY_HARDENING}" = "false" ]; then
  # Mount securityfs as it is used to configure inode security policies below.
  mount -n -i -c -t securityfs -o nodev,noexec,nosuid securityfs \
    /sys/kernel/security
fi

# Initialize kernel sysctl settings early so that they take effect for boot
# processes.
sysctl -q --system

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

# do_* are wrapper functions that may be redefined in developer mode or test
# images. Find more implementation in {dev,test,factory}_utils.sh.
do_mount_var_and_home_chronos() { mount_var_and_home_chronos; }
do_umount_var_and_home_chronos() { umount_var_and_home_chronos; }

if [ "${CROS_DEBUG}" -eq 1 ]; then
  . /usr/share/cros/dev_utils.sh
fi

# Protect a bind mount to the Chrome mount namespace.
mount --bind /run/namespaces /run/namespaces
mount --make-private /run/namespaces

# Prepare to mount stateful partition
ROOT_DEV="$(rootdev -s)"
ROOTDEV_RET_CODE=$?
ROOT_DEV_DISK="$(rootdev -d)"
# Example root dev types we need to handle: /dev/sda2 -> /dev/sda,
# /dev/mmcblk0p0 -> /dev/mmcblk0p, /dev/ubi2_1 -> /dev/ubi
ROOTDEV_TYPE="$(echo "${ROOT_DEV}" | sed 's/[0-9_]*$//')"
ROOTDEV_NAME="${ROOTDEV_DISK##/dev/}"
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

# Configure chromiumos LSM security policies for process management.
. /usr/share/cros/config_process_mgmt_utils.sh
configure_process_mgmt_security

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
  STATE_FLAGS="nodev,noexec,nosuid,noatime"
  if [ "${FORMAT_STATE}" = "ubi" ]; then
    STATE_DEV="/dev/ubi${PARTITION_NUM_STATE}_0"
    if [ ! -e "${STATE_DEV}" ]; then
      STATE_DEV="/dev/ubi0_${PARTITION_NUM_STATE}"
    fi
  else
    STATE_DEV="${ROOTDEV_TYPE}${PARTITION_NUM_STATE}"
    if [ "${FS_FORMAT_STATE}" = "ext4" ]; then
      DIRTY_EXPIRE_CENTISECS="$(sysctl -n vm.dirty_expire_centisecs)"
      COMMIT_INTERVAL="$(( DIRTY_EXPIRE_CENTISECS / 100 ))"
      STATE_FLAGS="${STATE_FLAGS},commit=${COMMIT_INTERVAL},discard"
    fi
  fi

  # Check to see if this is a hibernate resume boot. If so,
  # the image that will soon be resumed has active RW mounts
  # on stateful that must not be disturbed. Instead, create a
  # dm-snapshot, presenting the illusion of an RW stateful world.
  if command -v hiberman >/dev/null 2>&1 && \
     hiberman cookie "${ROOT_DEV_DISK}"; then

    # As of 2021, a normal warm boot to the login screen writes about 32MB.
    STATE_COW_SIZE="512M"
    mkdir -p /run/hibernate
    truncate -s "${STATE_COW_SIZE}" /run/hibernate/stateful_cow
    COW_LOOP="$(losetup --show -f /run/hibernate/stateful_cow)"
    STATE_SIZE="$(blockdev --getsz ${STATE_DEV})"
    dmsetup create stateful-origin --table \
      "0 ${STATE_SIZE} snapshot-origin ${STATE_DEV}"

    dmsetup create stateful-rw --table \
      "0 ${STATE_SIZE} snapshot ${STATE_DEV} ${COW_LOOP} P 8"

    STATE_DEV="/dev/mapper/stateful-rw"
  fi

  if [ "${USE_LVM_STATEFUL_PARTITION}" -eq "1" ]; then
    # Attempt to get a valid volume group name.
    vg_name="$(get_volume_group "${STATE_DEV}")"
    if [ -n "${vg_name}" ]; then
      vgchange -ay "${vg_name}"
      STATE_DEV="/dev/${vg_name}/unencrypted"
      DEV_IMAGE="/dev/${vg_name}/dev-image"
    fi
  fi

  # Check if we enable ext4 features.
  if [ "${FS_FORMAT_STATE}" = "ext4" ]; then
    SB_FEATURES=""
    SB_OPTIONS=""
    FS_FEATURES=$(dumpe2fs -h "${STATE_DEV}" 2>/dev/null |
      grep -e "^Filesystem features:.*")

    # Enable directory encryption for existing install.
    if ! string_contains "${FS_FEATURES}" "encrypt"; then
      # The stateful partition is not set for encryption.
      # Check if we should migrate.
      if ext4_dir_encryption_supported; then
        # The kernel support encryption, do it!
        SB_OPTIONS="${SB_OPTIONS},encrypt"
      fi
    fi

    # Enable fs-verity for existing install.
    if ! string_contains "${FS_FEATURES}" "verity"; then
      # The stateful partition is not set for fs-verity.
      # Check if we should migrate.
      if ext4_fsverity_supported; then
        # The kernel support fs-verity, do it!
        SB_OPTIONS="${SB_OPTIONS},verity"
      fi
    fi

    # Enable/disable quota feature.
    if ! dumpe2fs -h "${STATE_DEV}" 2>/dev/null | \
         grep -qe "^Reserved blocks gid.*20119"; then
      # Add Android's AID_RESERVED_DISK to resgid.
      SB_FEATURES="${SB_FEATURES} -g 20119"
    fi
    if ext4_quota_supported; then
      # Quota is enabled in the kernel, make sure that quota is enabled in the
      # filesystem.
      if ! string_contains "${FS_FEATURES}" "quota"; then
        SB_OPTIONS="${SB_OPTIONS},quota"
        SB_FEATURES="${SB_FEATURES} -Qusrquota,grpquota"
      fi
      if ext4_prjquota_supported; then
        if ! string_contains "${FS_FEATURES}" "project"; then
          SB_FEATURES="${SB_FEATURES} -Qprjquota"
        fi
      else
        if string_contains "${FS_FEATURES}" "project"; then
          SB_FEATURES="${SB_FEATURES} -Q^prjquota"
        fi
      fi
    else
      # Quota is not enabled in the kernel, make sure that quota is disabled in
      # the filesystem.
      if string_contains "${FS_FEATURES}" "quota"; then
        SB_OPTIONS="${SB_OPTIONS},^quota"
        SB_FEATURES="${SB_FEATURES} -Q^usrquota,^grpquota,^prjquota"
      fi
    fi

    if [ -n "${SB_FEATURES}" ] || [ -n "${SB_OPTIONS}" ]; then
      # Ensure to replay the journal first so it doesn't overwrite the flag.
      e2fsck -p -E journal_only "${STATE_DEV}" || :
      if [ -n "${SB_OPTIONS}" ]; then
          SB_FEATURES="${SB_FEATURES} -O ${SB_OPTIONS#,}"
      fi
      # Remove quote to treat ' ' as argument separators.
      tune2fs ${SB_FEATURES} "${STATE_DEV}" || :
    fi
  fi

  # Mount stateful partition from STATE_DEV.
  if ! mount -n -t "${FS_FORMAT_STATE}" -o "${STATE_FLAGS}" \
         "${STATE_DEV}" /mnt/stateful_partition; then
    # Try to rebuild the stateful partition by clobber-state. (Not using fast
    # mode out of security consideration: the device might have gotten into this
    # state through power loss during dev mode transition).
    chromeos-boot-alert self_repair
    clobber-log --repair "${STATE_DEV}" \
        "Self-repair corrupted stateful partition"
    dumpe2fs -fh "${STATE_DEV}" > /run/dumpe2fs_stateful.log
    add_clobber_crash_report "stateful"
    exec clobber-state "keepimg"
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
elif [ -z "${STATE_DEV}" ] || dev_is_debug_build; then
  # No physical stateful partition available, usually due to initramfs
  # (recovery image, factory install shim or netboot), or running from a
  # debug build image. Do not wipe.
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
  fi
elif crossystem 'devsw_boot?1' && ! crossystem 'mainfw_type?recovery'; then
  if [ ! -O "${DEV_MODE_FILE}" ]; then
    # We're transitioning from verified boot to dev mode.
    chromeos-boot-alert enter_dev
    CLOBBER_ARGS='keepimg'
    clobber-log -- "Enter developer mode"
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
if [ -d "/var/lib/whitelist" ] && ! rmdir /var/lib/whitelist && \
   rmdir /var/lib/devicesettings; then
  mv /var/lib/whitelist /var/lib/devicesettings
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
  # Restoring file contexts for sysfs. tracefs is excluded from this invocation
  # and delayed in a separate job to improve boot time.
  restorecon -R /sys -e /sys/kernel/debug/tracing
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

if [ "${DISABLE_STATEFUL_SECURITY_HARDENING}" = "false" ]; then
  # Unmount securityfs so that further modifications to inode security policies
  # are not possible.
  umount /sys/kernel/security
fi

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

bootstat post-startup

# Always return success to avoid killing init
exit 0
