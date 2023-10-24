# device_managementd (Device Management Service)

Device Management service is mainly responsible for storage, retrieval
and removal of various device management related attributes such as
firmware management parameters, installation time attributes etc.
In future, similar kinds of attributes are expected to be held by this
service.

Firmware Management Parameters (FWMP) control the rewritable (RW)
firmware boot process. They can be used to disable developer mode on
enterprise devices. If developer mode is enabled, they can limit which
kernel key can be used to sign developer images, and/or enable developer
features such as booting from USB or legacy OS. The FWMP is stored in a
TPM NVRAM space.

Install Attributes essentially provides a name-value storage interface.
The first time a device is used, a set of installation attributes is
stored on the device and remains tamper-evident for the remainder of the
install (i.e., until the device mode changes). If a device has been
enterprise enrolled, as evidenced by a ribbon with text like “This device
is owned by yourcompany.com,” then the installation attributes correspond
to this enrollment. The datastore is made tamper-evident by serializing
it to a bytestream and persisting it to the filesystem via the Lockbox
class. This is done when InstallAttributes::Finalize() is called.
After finalization, the data becomes read-only.

# Components
This is the list of the currently supported components by device_managementd

*   [Firmware Management Parameters]: control the rewritable (RW) firmware boot process.
*   [Lockbox]: Tamper-evident, install-time system attributes storage.

[Firmware Management Parameters]: ./docs/firmware_management_parameters.md
[Lockbox]: ./docs/lockbox.md
