# os_install_service

This directory contains the OS install D-Bus service. The service is
used to install the OS to disk.

The service exposes a single method, `StartOsInstall`. This method
takes no parameters; the service chooses an appropriate disk to
install to without any user input. Updates are provided with the
`OsInstallStatusChanged` signal. Currently there is no
percentage-complete report, the signal just indicates if the install
succeeded or failed. The signal also includes the install log so that
error details can be presented.

This service (when included in the OS image) only runs when the OS is
live booted from an installer image. This is checked in the [upstart
script](conf/os_install_service.conf) by running
`is_running_from_installer`, which compares the sizes of the root-A
and root-B partitions. If they are the same size, then the OS is
considered installed, whereas if the sizes are different then the OS
is running from an installer image with a stub root-B partition. Note
that this check would break if the USB layout is ever changed to
include a full-size root-B partition.

To test the service manually:

    dbus-monitor --system sender=org.chromium.OsInstallService

    sudo -u chronos dbus-send --print-reply --system \
        --dest=org.chromium.OsInstallService \
        /org/chromium/OsInstallService \
        org.chromium.OsInstallService.StartOsInstall

## Security

The service is currently run as root. This is a list of known blockers
preventing it from running as a less-privileged user, there are
probably more issues not yet known:

* `platform2/installer/chromeos-install` expects to run as root. If
  not run as root, it sudos itself. This check could be removed, or
  altered to check something more limited (e.g. test itself for the
  `CAP_SYS_ADMIN` capability), or enabled by default but with a way to
  turn it off manually such as by setting an env var.
* `platform2/chromeos-common-script/share/chromeos-common.sh` has
  something similar with the `maybe_sudo` function. `chromeos-install`
  depends on this in a few places. Could be solved in similar ways as
  described above.
* `chromeos-install` needs to mount and unmount disk partitions. This
  is possible to do with `CAP_SYS_ADMIN`, but the currently-installed
  version (2.32) of the `mount` and `umount` utilities explicitly
  checks uid==0. This has been fixed in newer versions so could be
  fixed by upgrading the `sys-apps/util-linux` package.
* `chromeos-install` installs most partitions with `dd` copies, but
  the stateful partition is installed by creating a fresh file system
  and then using `cp` to transfer specific directories. Many of those
  files are owned by root, and the root directory of the destination
  is also owned by root.

See also b/185422901 for adding an selinux policy to further restrict
the service.
