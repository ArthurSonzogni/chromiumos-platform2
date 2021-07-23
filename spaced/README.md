# Spaced: daemon for querying disk usage data

spaced surfaces disk usage information via a D-Bus interface.
Currently, spaced supports the following interfaces:

* Method GetTotalDiskSpace: Gets the total disk space available for usage on
  the device for a given path (including currently used space).
* Method GetFreeDiskSpace: Gets the available free space for use on the device
  for a given path.
