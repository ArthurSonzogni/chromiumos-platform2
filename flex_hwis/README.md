# ChromeOS Flex HWIS

HWIS is the ChromeOS Flex Hardware Information Service. This utility and
library collects and sends hardware information from ChromeOS Flex devices
to a server. This information is used to perform impact analysis when
dealing with releases, particularly in terms of support regressions, but
also for adding support for new hardware with a new release. Having a full
hardware profile of a machine also allows us to diagnose unique hardware
component interactions.

All hardware information sent by the utility is obtained from cros_healthd
via the mojo interface and translated into the protobuf format. Before
sending the data, the following checking steps are required:

1.  Check last run time
    The ChromeOS Flex HWIS utility will run after the user successfully
    logs in. The utility needs to check if it has run successfully within
    the last 24 hours. If it has been less than 24 hours since a successful
    run, then the utility exits.

2.  Confirm user permission
    The utility needs to check if the user permission is granted. In the
    unmanaged case, the tool will check if permission has been granted
    via the OOBE. In the managed case, the utility needs to check that
    the following device management policies are enabled:
    * ReportDeviceSystemInfo
    * ReportDeviceCpuInfo
    * ReportDeviceGraphicsStatus
    * ReportDeviceMemoryInfo
    * ReportDeviceVersionInfo
    * ReportDeviceNetworkConfiguration

3.  Read and check the UUID
    The utility should read the UUID from `/proc/sys/kernel/random/uuid`
    and store it at `/var/lib/flex_hwis_tool/uuid`. If the UUID was just
    generated, the client will interact with the server by POST request.
    If the UUID already exists, then it will be a PUT request.
