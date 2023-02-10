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

This service is run as root due to all the privileged operations needed
for OS installation. The [Upstart service] runs `os_install_service` in
minijail to restrict some syscalls, and there's an [SELinux policy] to
further restrict what the service can do.

[Upstart service]: conf/os_install_service.conf
[SELinux policy]: ../sepolicy/policy/chromeos/cros_os_install_service.te
