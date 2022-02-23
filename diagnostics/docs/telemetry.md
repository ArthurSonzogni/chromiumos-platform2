# Telemetry

On-device telemetry API serves the clients on both Chromium and
platform. The [data sources listed table](#Type-Definitions) could be
scattered around different services and it is our vision to hide those tedious
details away from our clients. In addition, single source will help the data
utilization easier to process on the server side if it is our client's final
destination.

We also support a [proactive event subscription](#TODO) API make clients could
subscript the particular event and got real-time notified when the event
occurs.

If you can't find the things you want, [contact us][team-contact] for a quick
check on the latest status just in case the documentation is behind the
reality.

[team-contact]: mailto:cros-tdm-tpe-eng@google.com

[TOC]

## Usages

### Mojo interface

`ProbeTelemetryInfo(categories)` in `CrosHealthdProbeService` interface can
grab the data from selected `categories` from a single IPC call. See the Mojo
interface comment for the detail.

Note that, __Strongly recommend__ to split your request into multiple and fetch
a subset of interesting categories in each call, and setup disconnect handler
to be able to return partial data. As `ProbeTelemetryInfo()` might not be
returned under certain critical situation. (e.g. `cros_healthd` got killed or
crash, a common cases is the seccomp vialation).

TODO(b/214343538): proactive event subscription API

### CLI tool

`cros-health-tool` is a convenience tools **for testing**, it is not for production used.
We recommended to [reach us][team-contact] before using this in your project.

For telemetry, we can initiate a request via `cros-health-tool telem
--category=<xx>` where `<xx>` is the category name. The list of category names
could be checked via `cros-health-tool telem --help`.

## Type Definitions


###  Audio

#####  AudioInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| output_mute | bool | Is active output device mute or not. |
| input_mute | bool | Is active input device mute or not. |
| output_volume | uint64 | Active output device's volume in [0, 100]. |
| output_device_name | string | Active output device's name. |
| input_gain | uint32 | Active input device's gain in [0, 100]. |
| input_device_name | string | Active input device's name. |
| underruns | uint32 | Numbers of underruns. |
| severe_underruns | uint32 | Numbers of severe underruns. |


###  Backlight

#####  BacklightInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| path | string | Path to this backlight on the system. Useful if the caller needs to<br />correlate with other information. |
| max_brightness | uint32 | Maximum brightness for the backlight. |
| brightness | uint32 | Current brightness of the backlight, between 0 and max_brightness. |


###  Battery

#####  BatteryInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| cycle_count | int64 | Current charge cycle count |
| voltage_now | double | Current battery voltage (V) |
| vendor | string | Manufacturer of the battery |
| serial_number | string | Serial number of the battery |
| charge_full_design | double | Designed capacity (Ah) |
| charge_full | double | Current Full capacity (Ah) |
| voltage_min_design | double | Desired minimum output voltage (V) |
| model_name | string | Model name of battery |
| charge_now | double | Current battery charge (Ah) |
| current_now | double | Current battery current (A) |
| technology | string | Technology of the battery. Battery chemistry. <br />e.g. "NiMH", "Li-ion", "Li-poly", "LiFe", "NiCd", "LiMn" |
| status | string | Status of the battery |
| manufacture_date | string? | Manufacture date converted to yyyy-mm-dd format. (Only available on Smart Battery) |
| temperature | uint64? | Temperature in 0.1K. Included when the main battery is a Smart Battery. (Only available on Smart Battery) |

####  Charge

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) AC Adapter Wattage |

####  EC

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) ePPID (Dell exclusive data) |
|  |  | (planned) Battery soft/hard error |
|  |  | (planned) MA code |


###  Bluetooth

####  Client

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Radio Connected (Desired) |
| name |  | (planned) Name of connected client |
| address |  | (planned) MAC of connected client |
|  |  | (planned) TX Bytes (total so far) |
|  |  | (planned) RX Bytets (total so far) |
|  |  | (planned) Radio On (Desired) |
|  |  | (planned) Radio Connected (Desired) |

####  Host

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) TX Bytes (total so far) |
|  |  | (planned) RX Bytets (total so far) |
|  |  | (planned) Radio On (Desired) |

#####  BluetoothAdapterInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| name | string | The name of the adapter. |
| address | string | The MAC address of the adapter. |
| powered | bool | Indicates whether the adapter is on or off. |
| num_connected_devices | uint32 | The number of devices connected to this adapter. |


###  Bus

#####  BusDevice
| Field | Type | Description |
| ----- | ---- | ----------- |
| vendor_name | string | The vendor / product name of the device. These are extracted from the<br />databases on the system and should only be used for showing / logging.<br />Don't use these to identify the devices. |
| product_name | string | The class of the device. |
| device_class | BusDeviceClass | The info related to specific bus type. |
| bus_info | [BusInfo](#BusInfo) | These fields can be used to classify / identify the pci devices. See the<br />pci.ids database for the values. (https://github.com/gentoo/hwids) |

#####  BusInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| pci_bus_info | [PciBusInfo](#PciBusInfo) | (union/one-of type) This field is valid only if the info is related to pci. |
| usb_bus_info | [UsbBusInfo](#UsbBusInfo) | (union/one-of type) This field is valid only if the info is related to usb. |
| thunderbolt_bus_info | [ThunderboltBusInfo](#ThunderboltBusInfo) | (union/one-of type) This field is valid only if the info is related to thunderbolt. |

####  PCIe

#####  PciBusInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| class_id | uint8 |  |
| subclass_id | uint8 |  |
| prog_if_id | uint8 |  |
| vendor_id | uint16 |  |
| device_id | uint16 |  |
| driver | string? | The driver used by the device. This is the name of the matched driver which<br />is registered in the kernel. See "{kernel root}/drivers/". for the list of<br />the built in drivers. |

####  Thunderbolt

#####  ThunderboltBusInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| security_level | ThunderboltSecurityLevel | Security level none, user, secure, dponly. |
| thunderbolt_interfaces | [array<ThunderboltBusInterfaceInfo>](#ThunderboltBusInterfaceInfo) | Info of devices attached to the controller. |

#####  ThunderboltBusInterfaceInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| vendor_name | string | Vendor name of connected device interface. |
| device_name | string | Product name of connected device interface. |
| device_type | string | Type of device. |
| device_uuid | string | The device unique id. |
| tx_speed_gbs | uint32 | Transmit link speed for thunderbolt interface. |
| rx_speed_gbs | uint32 | Receive link speed for thunderbolt interface. |
| authorized | bool | Connection is authorized or not. |
| device_fw_version | string | nvm firmware version. |

####  USB

#####  UsbBusInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| class_id | uint8 | These fields can be used to classify / identify the usb devices. See the<br />usb.ids database for the values. (https://github.com/gentoo/hwids) |
| subclass_id | uint8 |  |
| protocol_id | uint8 |  |
| vendor_id | uint16 |  |
| product_id | uint16 |  |
| interfaces | [array<UsbBusInterfaceInfo>](#UsbBusInterfaceInfo) | The usb interfaces under the device. A usb device has at least one<br />interface. Each interface may or may not work independently, based on each<br />device. This allows a usb device to provide multiple features.<br />The interfaces are sorted by the |interface_number| field. |

#####  UsbBusInterfaceInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| interface_number | uint8 | The zero-based number (index) of the interface. |
| class_id | uint8 | These fields can be used to classify / identify the usb interfaces. See the<br />usb.ids database for the values. |
| subclass_id | uint8 |  |
| protocol_id | uint8 |  |
| driver | string? | The driver used by the device. This is the name of the matched driver which<br />is registered in the kernel. See "{kernel root}/drivers/". for the list of<br />the built in drivers. |


###  CPU

#####  CpuInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| num_total_threads | uint32 | Number of total threads available. |
| architecture | CpuArchitectureEnum | The CPU architecture - it's assumed all of a device's CPUs share an<br />architecture. |
| physical_cpus | [array<PhysicalCpuInfo>](#PhysicalCpuInfo) | Information about the device's physical CPUs. |
| temperature_channels | [array<CpuTemperatureChannel>](#CpuTemperatureChannel) | Information about the CPU temperature channels. |
| keylocker_info | [KeylockerInfo?](#KeylockerInfo) | Information about keylocker. |

####  C State

#####  CpuCStateInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| name | string | Name of the state. |
| time_in_state_since_last_boot_us | uint64 | Time spent in the state since the last reboot, in microseconds. |

####  KeyLocker

#####  KeylockerInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| keylocker_configured | bool | Has Keylocker been configured or not. |

####  Logical Core

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) CPU flags in /proc/cpuinfo |

#####  LogicalCpuInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| max_clock_speed_khz | uint32 | The max CPU clock speed in kHz. |
| scaling_max_frequency_khz | uint32 | Maximum frequency the CPU is allowed to run at, by policy. |
| scaling_current_frequency_khz | uint32 | Current frequency the CPU is running at. |
| user_time_user_hz | uint64 | Time spent in user mode since last boot. USER_HZ can be converted to<br />seconds with the conversion factor given by sysconf(_SC_CLK_TCK). |
| system_time_user_hz | uint64 | Time spent in system mode since last boot. USER_HZ can be converted to<br />seconds with the conversion factor given by sysconf(_SC_CLK_TCK). |
| idle_time_user_hz | uint64 | Idle time since last boot. USER_HZ can be converted to seconds with the<br />conversion factor given by sysconf(_SC_CLK_TCK). |
| c_states | [array<CpuCStateInfo>](#CpuCStateInfo) | Information about the logical CPU's time in various C-states. |
|  |  | (planned) total_time_in_ticks (time in state since beginning) |
|  |  | (planned) Current Throttle% (for each logical) |
|  |  | (planned) Used percentage |
|  |  | (planned) Average Utilization percentage |

####  Physical Core

#####  PhysicalCpuInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| model_name | string? | The CPU model name, if available.<br />For Arm devices, we will return SoC model instead. |
| logical_cpus | [array<LogicalCpuInfo>](#LogicalCpuInfo) | Logical CPUs corresponding to this physical CPU. |

####  Temperature

#####  CpuTemperatureChannel
| Field | Type | Description |
| ----- | ---- | ----------- |
| label | string? | Temperature channel label, if found on the device. |
| temperature_celsius | int32 | CPU temperature in Celsius. |

####  Virtualization

| Field | Type | Description |
| ----- | ---- | ----------- |
| VMXLockedInBIOS |  | (planned) Is VMX locked by the device BIOS |
| VMXEnabled |  | (planned) VMX - Intel Virtualisation is used to control certain features such as crostini. It is useful to know if it is enabled to allow us to gate or preempt issues with features like crostini. |
| SMTActive |  | (planned) SMT is the AMD version of Intel hyper threading, knowing its bios status is useful for assessing security vulnerabilities |
| SMTControl |  | (planned) I believe this is used to assess is the OS can control SMT |
| DevKVMExists |  | (planned) This allows us to verify if a processor supports virtualisation or not. |


###  Dell EC

####  BIOS Internal Log

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Power event log - event code / timestamp |
|  |  | (planned) LED code log - event code / timestamp |

####  Cable

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Cable Name |
|  |  | (planned) Cable Status (installed and not installed) |
|  |  | (planned) Cable change history |
|  |  | (planned) Cable time stamp |

####  Thermistor

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Location |
|  |  | (planned) Temp |
|  |  | (planned) Timestamp |
|  |  | (planned) Thermal zone |
|  |  | (planned) Thermal trip |
|  |  | (planned) Thermal hystereis |


###  Display

#####  DisplayInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| edp_info | [EmbeddedDisplayInfo](#EmbeddedDisplayInfo) | Embedded display info. |
| dp_infos | [array<ExternalDisplayInfo>?](#ExternalDisplayInfo) | External display info. |

####  Embedded Display

#####  EmbeddedDisplayInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| privacy_screen_supported | bool | Privacy screen is supported or not. |
| privacy_screen_enabled | bool | Privacy screen is enabled or not. |
| display_width | uint32? | Display width in millimeters. |
| display_height | uint32? | Display height in millimeters. |
| resolution_horizontal | uint32? | Horizontal resolution. |
| resolution_vertical | uint32? | Vertical resolution. |
| refresh_rate | double? | Refresh rate. |

####  External Display

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Number of Monitor |
|  |  | (planned) Monitor Type |
|  |  | (planned) Model Name |
|  |  | (planned) Serial |
|  |  | (planned) Vendor Specific Data |

#####  ExternalDisplayInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| display_width | uint32? | Display width in millimeters. |
| display_height | uint32? | Display height in millimeters. |
| resolution_horizontal | uint32? | Horizontal resolution. |
| resolution_vertical | uint32? | Vertical resolution. |
| refresh_rate | double? | Refresh rate. |


###  Fan

#####  FanInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| speed_rpm | uint32 | Fan speed in RPM. |

####  Dell

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Fan speed in RPM. Data source: Dell EC |
|  |  | (planned) Fan location. Data source: Dell EC |


###  Firmware

####  EFI

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) EFIFirmwareBitness. Identify if UEFI is IA32 or x86_64 as we support some 32bit UEFI devices |


###  Graphic

#####  GraphicsInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| gles_info | [GLESInfo](#GLESInfo) | OpenGL | ES information. |
| egl_info | [EGLInfo](#EGLInfo) | EGL information. |

####  EGL

#####  EGLInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| version | string | EGL version. |
| vendor | string | EGL vendor. |
| client_api | string | EGL client API. |
| extensions | array<string> | EGL extensions. |

####  GL ES

#####  GLESInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| version | string | GL version. |
| shading_version | string | GL shading version. |
| vendor | string | GL vendor. |
| renderer | string | GL renderer. |
| extensions | array<string> | GL extensions. |


###  Input

####  Touchscreen

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) does the device have a touchscreen |
|  |  | (planned) is touchscreen usable |


###  Lid

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) lid status. Open or Close. |


###  Log

####  OS crash

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) system_crach_log (detail in confluence) |


###  Memory

####  General

#####  MemoryInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| total_memory_kib | uint32 | Total memory, in KiB. |
| free_memory_kib | uint32 | Free memory, in KiB. |
| available_memory_kib | uint32 | Available memory, in KiB. |
| page_faults_since_last_boot | uint64 | Number of page faults since the last boot. |
| memory_encryption_info | [MemoryEncryptionInfo?](#MemoryEncryptionInfo) | Memory Encryption info. |

####  Memory Encryption

#####  MemoryEncryptionInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| encryption_state | EncryptionState | Memory encryption state. |
| max_key_number | uint32 | Encryption key length. |
| key_length | uint32 | Encryption key length. |
| active_algorithm | CryptoAlgorithm | Crypto algorithm currently used. |


###  Misc

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Dell ePSA report |
|  |  | (planned) Customer name/defined |


###  Network

####  Health

#####  Network
| Field | Type | Description |
| ----- | ---- | ----------- |
| type | NetworkType | The network interface type. |
| state | NetworkState | The current status of this network. e.g. Online, Disconnected. |
| guid | string? | The unique identifier for the network when a network service exists. |
| name | string? | The user facing name of the network if available. |
| mac_address | string? | Optional string for the network's mac_address. |
| signal_strength | uint32? | Signal strength of the network provided only for wireless networks. Values<br />are normalized between 0 to 100 inclusive. Values less than 30 are<br />considered potentially problematic for the connection. See<br />src/platform2/shill/doc/service-api.txt for more details. |
| ipv4_address | string? | Optional string for the network's ipv4_address. This is only intended to be<br />used for display and is not meant to be parsed. |
| ipv6_addresses | array<string> | Optional list of strings for the network's ipv6_addresses. A single network<br />can have multiple addresses (local, global, temporary etc.). This is only<br />intended to be used for display and is not meant to be parsed. |
| portal_state | PortalState | An enum of the network's captive portal state. This information is<br />supplementary to the NetworkState. |
| signal_strength_stats | SignalStrengthStats? | The statistics of the signal strength for wireless networks over a 15<br />minute period. See SignalStrengthStats for more details. |

#####  NetworkHealthState
| Field | Type | Description |
| ----- | ---- | ----------- |
| networks | [array<Network>](#Network) | This is a list of networking devices and any associated connections.<br />Only networking technologies that are present on the device are included.<br />Networks will be sorted with active connections listed first. |

####  Interface

#####  NetworkInterfaceInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| wireless_interface_info | [WirelessInterfaceInfo](#WirelessInterfaceInfo) | Wireless interfaces. |

####  LAN

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) RX Bytes |
|  |  | (planned) TX Bytes |
|  |  | (planned) Interface Name |
|  |  | (planned) LAN Speed |

####  WLAN

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) RX Bytes |
|  |  | (planned) TX Bytes |
|  |  | (planned) Radio On/Off |

####  WWAN / modem

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Manufacturer |
|  |  | (planned) Model |
|  |  | (planned) IMEI |

####  Wifi Interface

#####  WirelessInterfaceInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| interface_name | string | Interface name. |
| power_management_on | bool | Is power management enabled for wifi or not. |
| wireless_link_info | WirelessLinkInfo? | Link info only available when device is connected to an access point. |
| access_point_address_str | string | Access point address. |
| tx_bit_rate_mbps | uint32 | Tx bit rate measured in Mbps. |
| rx_bit_rate_mbps | uint32 | Rx bit rate measured in Mbps. |
| tx_power_dBm | int32 | Transmission power measured in dBm. |
| encyption_on | bool | Is wifi encryption key on or not. |
| link_quality | uint32 | Wifi link quality. |
| signal_level_dBm | int32 | Wifi signal level in dBm. |


###  OS

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) OS uptime |
|  |  | (planned) PQL (Process Queue Length) |
|  |  | (planned) hostname |


###  Performance

####  Booting

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Last restart time (differ from shutdown?) |

#####  BootPerformanceInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| boot_up_seconds | double | Total time since power on to login screen prompt. |
| boot_up_timestamp | double | The timestamp when power on. |
| shutdown_seconds | double | Total time(rough) since shutdown start to power off.<br />Only meaningful when shutdown_reason is not "N/A". |
| shutdown_timestamp | double | The timestamp when shutdown.<br />Only meaningful when shutdown_reason is not "N/A". |
| shutdown_reason | string | The shutdown reason (including reboot). |


###  Storage

####  Device

#####  NonRemovableBlockDeviceInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| vendor_id | BlockDeviceVendor | Device vendor identification. |
| product_id | BlockDeviceProduct | Device product identification. |
| revision | BlockDeviceRevision | Device revision. |
| name | string | Device model. |
| size | uint64 | Device size in bytes. |
| firmware_version | BlockDeviceFirmware | Firmware version. |
| type | string | Storage type, could be MMC / NVMe / ATA, based on udev subsystem. |
| purpose | StorageDevicePurpose | Purpose of the device e.g. "boot", "swap". |

####  Device (SMART)

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) SMART - Temperature |
|  |  | (planned) SMART - total block read |
|  |  | (planned) SMART - total block write |
|  |  | (planned) SMART - model name |
|  |  | (planned) SMART - Temperature |
|  |  | (planned) SMART - power cycle count |
|  |  | (planned) SMART - power on hours |
|  |  | (planned) NVMe Dell Smart Attribute |

####  IO

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Idle time |

#####  NonRemovableBlockDeviceInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| bytes_read_since_last_boot | uint64 | Bytes read since last boot. |
| bytes_written_since_last_boot | uint64 | Bytes written since last boot. |
| read_time_seconds_since_last_boot | uint64 | Time spent reading since last boot. |
| write_time_seconds_since_last_boot | uint64 | Time spent writing since last boot. |
| io_time_seconds_since_last_boot | uint64 | Time spent doing I/O since last boot. Counts the time the disk and queue<br />were busy, so unlike the fields above, parallel requests are not counted<br />multiple times. |
| discard_time_seconds_since_last_boot | uint64? | Time spent discarding since last boot. Discarding is writing to clear<br />blocks which are no longer in use. Supported on kernels 4.18+. |

####  Logical Drive

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Name |
|  |  | (planned) Size_MB |
|  |  | (planned) Type |
|  |  | (planned) Freespace_MB |

####  Others

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) ePPID (Dell exclusive data) |

#####  NonRemovableBlockDeviceInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| path | string | The path of this storage on the system. It is useful if caller needs to<br />correlate with other information. |
| manufacturer_id | uint8 | Manufacturer ID, 8 bits. |
| serial | uint32 | PSN: Product serial number, 32 bits |

####  Partition

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) LSB size |
|  |  | (planned) Partition size |
|  |  | (planned) Partition Free Size |

####  StatefulPartition

#####  StatefulPartitionInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| available_space | uint64 | Available space for user data storage in the device in bytes. |
| total_space | uint64 | Total space for user data storage in the device in bytes. |
| filesystem | string | File system on stateful partition. e.g. ext4. |
| mount_source | string | Source of stateful partition. e.g. /dev/mmcblk0p1. |


###  System

####  CPU

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) CPU - serial number |

####  DMI (SMBIOS)

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) DIMM - location |
|  |  | (planned) DIMM - manufacturer |
|  |  | (planned) DIMM - part number |
|  |  | (planned) DIMM - serial number |
|  |  | (planned) BIOS Version |
|  |  | (planned) Chassis type/System Type |
|  |  | (planned) Motherboard product name |
|  |  | (planned) Motherboard serial number |
|  |  | (planned) Motherboard version |
|  |  | (planned) Service Tag |

#####  DmiInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| bios_vendor | string? | The BIOS vendor. |
| bios_version | string? | The BIOS version. |
| board_name | string? | The product name of the motherboard. |
| board_vendor | string? | The vendor of the motherboard. |
| board_version | string? | The version of the motherboard. |
| chassis_vendor | string? | The vendor of the chassis. |
| chassis_type | uint64? | The chassis type of the device. The values reported by chassis type are<br />mapped in<br />www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.0.0.pdf. |
| product_family | string? | The product family name. |
| product_name | string? | The product name (model) of the system. |
| product_version | string? | The product version. |
| sys_vendor | string? | The system vendor name. |

####  OS Env

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Language locale |
|  |  | (planned) Display language |
|  |  | (planned) timezone |

####  OS Image

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) OS Architecture (x86, x64, arm, arm64) |

#####  OsInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| code_name | string | Google code name for the given model. While it is OK to use this string for<br />human-display purposes (such as in a debug log or help dialog), or for a<br />searchable-key in metrics collection, it is not recommended to use this<br />property for creating model-specific behaviors. |
| marketing_name | string? | Contents of CrosConfig in /arc/build-properties/marketing-name. |
| os_version | [OsVersion](#OsVersion) | The OS version of the system. |
| boot_mode | BootMode | The boot flow used by the current boot. |

#####  OsVersion
| Field | Type | Description |
| ----- | ---- | ----------- |
| release_milestone | string | The OS version release milestone (e.g. "87"). |
| build_number | string | The OS version build number (e.g. "13544"). |
| patch_number | string | The OS version patch number (e.g. "59.0"). |
| release_channel | string | The OS release channel (e.g. "stable-channel"). |

####  Time zone

#####  TimezoneInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| posix | string | The timezone of the device in POSIX standard. |
| region | string | The timezone region of the device. |

####  VPD

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) System Model |
|  |  | (planned) Dell product name |
|  |  | (planned) Asset Tag |
|  |  | (planned) UUID |
|  |  | (planned) Manufacture Date |
|  |  | (planned) First Power Date |
|  |  | (planned) SKU number |
|  |  | (planned) System ID |

#####  VpdInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| serial_number | string? | A unique identifier of the device. (Required RO VPD field) |
| region | string? | Defines a market region where devices share a particular configuration of<br />keyboard layout, language, and timezone. (Required VPD field) |
| mfg_date | string? | The date the device was manufactured. (Required RO VPD field)<br />Format: YYYY-MM-DD. |
| activate_date | string? | The date the device was first activated. (Runtime RW VPD field)<br />Format: YYYY-WW. |
| sku_number | string? | The product SKU number. (Optional RO VPD field. b/35512367) |
| model_name | string? | The product model name. (Optional RO VPD field. b/35512367) |


###  TPM

#####  TpmInfo
| Field | Type | Description |
| ----- | ---- | ----------- |
| version | [TpmVersion](#TpmVersion) | TPM version related information. |
| status | [TpmStatus](#TpmStatus) | TPM status related information. |
| dictionary_attack | [TpmDictionaryAttack](#TpmDictionaryAttack) | TPM dictionary attack (DA) related information. |
| attestation | [TpmAttestation](#TpmAttestation) | TPM attestation related information. |
| supported_features | [TpmSupportedFeatures](#TpmSupportedFeatures) | TPM supported features information. |
| did_vid | string? | [Do NOT use] TPM did_vid file. This field is only used in Cloudready<br />project. It is going to drop the support in few milestone. |

####  Attestation

#####  TpmAttestation
| Field | Type | Description |
| ----- | ---- | ----------- |
| prepared_for_enrollment | bool | Is prepared for enrollment? True if prepared for *any* CA. |
| enrolled | bool | Is enrolled (AIK certificate created)? True if enrolled with *any* CA. |

####  Dictionary Attack

#####  TpmDictionaryAttack
| Field | Type | Description |
| ----- | ---- | ----------- |
| counter | uint32 | The current dictionary attack counter value. |
| threshold | uint32 | The current dictionary attack counter threshold. |
| lockout_in_effect | bool | Whether the TPM is in some form of dictionary attack lockout. |
| lockout_seconds_remaining | uint32 | The number of seconds remaining in the lockout. |

####  TPM Status

#####  TpmStatus
| Field | Type | Description |
| ----- | ---- | ----------- |
| enabled | bool | Whether a TPM is enabled on the system. |
| owned | bool | Whether the TPM has been owned. |
| owner_password_is_present | bool | Whether the owner password is still retained. |

#####  TpmSupportedFeatures
| Field | Type | Description |
| ----- | ---- | ----------- |
| support_u2f | bool | Whether the u2f is supported or not. |
| support_pinweaver | bool | Whether the pinweaver is supported or not. |
| support_runtime_selection | bool | Whether the platform supports runtime TPM selection or not. |
| is_allowed | bool | Whether the TPM is allowed to use or not. |

#####  TpmVersion
| Field | Type | Description |
| ----- | ---- | ----------- |
| gsc_version | TpmGSCVersion | GSC version. |
| family | uint32 | TPM family. We use the TPM 2.0 style encoding, e.g.:<br /> * TPM 1.2: "1.2" -> 0x312e3200<br /> * TPM 2.0: "2.0" -> 0x322e3000 |
| spec_level | uint64 | TPM spec level. |
| manufacturer | uint32 | Manufacturer code. |
| tpm_model | uint32 | TPM model number. |
| firmware_version | uint64 | Firmware version. |
| vendor_specific | string? | Vendor specific information. |


###  Video

| Field | Type | Description |
| ----- | ---- | ----------- |
|  |  | (planned) Video Controller name |
|  |  | (planned) Video RAM (Bytes) |
