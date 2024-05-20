# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Usage: setup-data-dir <data_dir> <ttl_timespec>
#
# Example:
# setup-data-dir ~/SomePathUnderYourHomeDir "Jun 7, 2022"
#
# This command disables duplicity backups for the given |data_dir|
# and schedules a shredding operation around the given |ttl_timespec|.
#
# We use the |at| command to schedule this deletion operation. It will
# run the operation in the background, without the user being logged in,
# any time the machine is running after |ttl_timespec|.
# Ex. If the machine is powered-off when the timespec expires, the machine
# will start the operation on next boot, before the user logs in.
#
# Since this operation can take a long time to complete, we need to ensure
# that this operation always completes. We do this by rescheduling an
# additional deletion before we start the deletion, if data in the data dir
# exists.
#
# See |man at| for help with the |ttl_timespec|.
setup-data-dir() {
    local data_dir="$1"
    local timespec="$2"

    # Bypass symbolic links and relative paths.
    local data_dir_real="$(realpath "${data_dir}")"

    # Setup names for deletion process.
    local data_dir_del_script="${data_dir_real}-nuke-now.bash"
    local data_dir_del="${data_dir_real}-deletion-in-progress"
    local data_dir_del_log="${data_dir_real}-deletion-log.txt"

    # Check that data dir already exists.
    if [[ ! -d "${data_dir_real}" ]]; then
        echo "Error - '${data_dir_real}' doesn't exist." >&2
        return 1
    fi

    echo "# Disabling backups for data dir '${data_dir_real}'."
    if ! touch "${data_dir_real}/.nobackup"; then
        echo "Error - Failed to touch ${data_dir_real}/.nobackup." >&2
        return 1
    fi

    if ! which at >/dev/null; then
        echo "Error - The 'at' pkg is not installed." >&2
        echo "This should have been installed through 'glinux-scheduler' pkg."
        exit 1
    fi

    # An idempotent command that always tries to remove the data dir.
    echo "# Writing the nuke script to '${data_dir_del_script}'."
    cat >"${data_dir_del_script}" <<EOF
#!/bin/bash

# Only allow one of these scripts to run at a time using a file lock
# on the output log file.
# Then, redirect all output to log file.
echo '# Waiting for lock on log file.'
if ! exec 1>>'${data_dir_del_log}'; then
    echo "Error - Can't open log file." >&2
    exit 1
fi
flock 1
echo '# Got lock and continuing with shredding.' >&2
echo '# Log will be written to ${data_dir_del_log}.' >&2
exec 2>&1

# Clear any broken output lines from an interrupted deletion.
echo

if [[ ! -d '${data_dir_real}' && ! -d '${data_dir_del}' ]]; then
    echo '# Data dir and deletion progress dir are already deleted.'
    echo '# Aborting on $(date).'
    exit 0
fi

echo '# Shred operation attempt on $(date).'

echo '# Rescheduling for tomorrow.'
at now + 1 day -f '${data_dir_del_script}' || echo 'Cmd at failed'

# Move to deletion dir without copy, this should be idempotent.
echo '# Moving data dir to ${data_dir_del}.'
echo '# It is okay for this to fail if a previous attempt occurred.'
mv -f '${data_dir_real}' '${data_dir_del}' || echo 'Cmd mv failure'

# When using he -u argument, shred cannot be parallelized, since the
# file names start to collide.
# We don't use -v on shred, since that would log the file name, which
# would leak participant IDs and groups.
# This really isn't an issue, but it is counterproductive, since
# shred tries hard to overwrite the file names.
echo "# Shredding files in '${data_dir_del}'."
find '${data_dir_del}' -type f | xargs shred -f -u || echo 'Cmd find failed'

# If we completed successfully OR shred failed, remove all dirs
# and possibly files.
echo "# Removing '${data_dir_del}'."
rm -rf '${data_dir_del}' || echo 'Cmd rm failed'
echo "# Finished on \$(date)."
exit 0
EOF
    chmod u=rx,g=rx "${data_dir_del_script}"

    # It is a rabbit hole to try to cover all situations that the user may put
    # us in, but we could think about creating a hard link to the data dir
    # in a location that won't change. This would allow for users to move the
    # dir on the same storage mount.

    # Ensure that the later mv command will not copy data across filesystems.
    if [[ "$(stat -c "%d" "${data_dir_real}")" != \
        "$(stat -c "%d" "${data_dir_del_script}")" ]]; then
        echo "Error - We falied to find a safe location " \
            "to move the data to for deletion." >&2
        return 1
    fi

    # The at pkg should already be installed as a dependency of
    # glinux-scheduler. The shred command is part of coreutils.
    # We schedule the deletion job as root to avoid possible permission issues
    # in the future. One example of how we might end up with root owned files
    # in the data-dir is through the use of docker, when running the FPC
    # BET tool.
    echo "# Setting up TTL for ${timespec} as root."
    sudo at "${timespec}" <<EOF
bash "${data_dir_del_script}"
EOF

    echo
    echo "# DO NOT MOVE the following data dir or nuke script:"
    echo "# * ${data_dir_real}"
    echo "# * ${data_dir_del_script}"
}

setup-data-dir-basic() {
    local data_dir="$1"
    local timespec="$2"

    touch "${data_dir}/.nobackup"

    # Must be done as user.
    at "${timespec}" <<EOF
gio trash -f '${data_dir}'
gio trash --empty
EOF
}

# Testing:
test-setup-data-dir() {
    mkdir ~/test-data-dir; pushd ~/test-data-dir
    for ((i = 0; i < 500; i++)); do
        dd if=/dev/urandom of=./data-$i.bin bs=2MB count=1
    done
    popd

    echo
    echo
    echo
    echo

    echo "# Launch multiple deletion tasks at once to look for race conditions."
    for ((i = 0; i < 5; i++)); do
        setup-data-dir ~/test-data-dir "now + 2min"
    done

    echo "# Check that the .nobackup file exists in the data dir:"
    ls -al ~/test-data-dir/.nobackup

    echo "# Check that multiple jobs are scheduled:"
    sudo atq

    echo "# Check ~/test-data-dir-deletion-log.txt to ensure that only"
    echo "  the first deletion attempt succeeded and all other were aborted."

    echo "# Check 'sudo atq' to ensure that one final job was scheduled"
    echo "  for tomorrow."

    echo "# Check that the ~/test-data-dir and"
    echo "  ~/test-data-dir-deletion-in-progress are gone."

    echo "# After all manual checks, run sudo ~/test-data-dir-nuke-now.bash"
    echo "  to check waiting for the lock."

    echo "# You should also check running ~/test-data-dir-nuke-now.bash before"
    echo "  the scheduled deletion, cause an interruption before it completes,"
    echo "  and ensure that the files are still deleted by the scheduled cmd."
}


