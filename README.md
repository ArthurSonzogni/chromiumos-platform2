# The ChromiumOS Platform

This repo holds (most) of the custom code that makes up the ChromiumOS
platform.  That largely covers daemons, programs, and libraries that were
written specifically for ChromiumOS.

We moved from multiple separate repos in platform/ to a single repo in
platform2/ for a number of reasons:

* Make it easier to work across multiple projects simultaneously
* Increase code re-use (via common libs) rather than duplicate utility
  functions multiple items over
* Share the same build system

While most projects were merged, not all of them were.  Some projects were
standalone already (such as vboot), or never got around to being folded in
(such as imageloader).  Some day those extra projects might get merged in.

Similarly, some projects that were merged in, were then merged back out.
This was due to the evolution of the Brillo project and collaboration with
Android.  That means the AOSP repos are the upstream and ChromiumOS carries
copies.

# Local Project Directory

| Project | Description |
|---------|-------------|
| [anx7625-ta](./anx7625-ta/) | ANX7625 controller trusted application for hardware DRM |
| [arc](./arc/) | Tools/deamons/init-scripts to run ARC |
| [attestation](./attestation/) | Daemon and client for managing remote attestation |
| [avtest_label_detect](./avtest_label_detect/) | Test tool for OCRing device labels |
| [biod](./biod/) | Biometrics daemon |
| [bootid-logger](./bootid-logger/) | Simple command to record the current boot id to the log. |
| [bootlockbox](./bootlockbox/) | Daemon and client for boot lockbox service.|
| [bootsplash](./bootsplash/) | Frecon-based animated boot splash service |
| [bootstat](./bootstat/) | Tools for tracking points in the overall boot process (for metrics) |
| [bpf-mons](./bpf-mons/) | Collection of BPF monitoring programs for in-depth tracing |
| [build_overrides](./build_overrides/) | Customize GN-based third party products for direct platform2 integration |
| [camera](./camera/) | ChromeOS Camera daemon |
| [cecservice](./cecservice/) | Service for switching CEC enabled TVs on and off |
| [cfm-dfu-notification](./cfm-dfu-notification/) | CFM specific library for DFU notifications |
| [chaps](./chaps/) | PKCS #11 implementation for TPM 1 devices |
| [chromeos-common-script](./chromeos-common-script/) | Shared scripts for partitions and basic disk information |
| [chromeos-config](./chromeos-config/) | CrOS unified build runtime config manager |
| [chromeos-dbus-bindings](./chromeos-dbus-bindings/) | Simplifies the implementation of D-Bus daemons and proxies |
| [chromeos-nvt-tcon-updater](./chromeos-nvt-tcon-updater/) | Library for integrating the Novatek TCON firmware updater into a CrOS device |
| [codelab](./codelab/) | Codelab exercise |
| [common-mk](./common-mk/) | Common build & test logic for platform2 projects |
| [crash-reporter](./crash-reporter/) | The system crash handler & reporter |
| [cros-codecs](./cros-codecs/) | Hardware accelerated video middleware |
| [cros-disks](./cros-disks/) | Daemon for mounting removable media (e.g. USB sticks and SD cards) |
| [cros-toolchain](./cros-toolchain/) | ChromeOS toolchain projects (generally tests that run in CQ) |
| [crosdns](./crosdns/) | Hostname resolution service for ChromeOS |
| [crosh](./crosh/) | The ChromiumOS shell |
| [crosier-chrome](./crosier-chrome/) | Crosier testing framework |
| [croslog](./croslog/) | The log manipulation command |
| [cryptohome](./cryptohome/) | Daemon and tools for managing encrypted /home and /var directories |
| [cups_proxy](./cups_proxy/) | Daemon for proxying CUPS printing request |
| [dbus_perfetto_producer](./dbus_perfetto_producer/) | A D-bus event producer of perfetto |
| [debugd](./debugd/) | Centralized debug daemon for random tools |
| [desktop_ota_recovery](./desktop_ota_recovery/) | Utility and modules to recover via network |
| [dev-install](./dev-install/) | Tools & settings for managing the developer environment on the device |
| [device_management](./device_management/) | Daemon for handling device management related attributes (e.g. fwmp, install_attributes etc) |
| [diagnostics](./diagnostics/) | Device telemetry and diagnostics daemons |
| [disk_updater](./disk_updater/) | Utility for updating root disk firmware (e.g. SSDs and eMMC) |
| [dlcservice](./dlcservice/) | Downloadable Content (DLC) Service daemon |
| [dlp](./dlp/) | Date Leak Prevention (DLP) daemon |
| [dns-proxy](./dns-proxy/) | DNS Proxy daemon |
| [easy-unlock](./easy-unlock/) | Daemon for handling Easy Unlock requests (e.g. unlocking Chromebooks with an Android device) |
| [ethernet-hide](./ethernet-hide/) | Tool for hiding Ethernet interfaces while enabling the SSH connection |
| [extended-updates](./extended-updates/) | Utilities supporting the Extended Auto Updates process |
| [farfetchd](./farfetchd/) | Farfetchd Readahead Daemon |
| [fbpreprocessor](./fbpreprocessor/) | Debug file preprocessing for feedback reports |
| [feature_usage](./feature_usage/) | Library to provide a unified approach to report feature usage events |
| [featured](./featured/) | Feature daemon for enabling and managing platform features |
| [feedback](./feedback/) | Daemon for headless systems that want to gather feedback (normally Chrome manages it) |
| [ferrochrome](./ferrochrome/) | Components to run Ferrochrome |
| [flex_bluetooth](./flex_bluetooth/) | Updates Floss overrides for ChromeOS Flex |
| [flex_hwis](./flex_hwis/) | Utility for collecting hardware information and sending it to a remote API |
| [flex_id](./flex_id/) | Utility for generating flex_id, a machine identifier for devices without VPD info |
| [flexor](./flexor/) | Experimental ChromeOS Flex installer |
| [foomatic_shell](./foomatic_shell/) | Simple shell used by the foomatic-rip package |
| [fusebox](./fusebox/) | FuseBox service |
| [glib-bridge](./glib-bridge/) | library for libchrome-glib message loop interoperation |
| [goldfishd](./goldfishd/) | Android Emulator Daemon |
| [gsclog](./gsclog/) | GSC Log Fetcher |
| [hammerd](./hammerd/) | Firmware updater utility for hammer hardware |
| [hardware_verifier](./hardware_verifier/) | Hardware verifier tool |
| [heartd](./heartd/) | Health ensure and accident resolve treatment daemon |
| [heatmap-recorder](./heatmap-recorder/) | Heatmap recorder tool |
| [hermes](./hermes/) | ChromeOS LPA implementation for eSIM hardware support |
| [hps](./hps/) | ChromeOS HPS daemon and utilities |
| [hwdrm-videoproc-ta](./hwdrm-videoproc-ta/) | Hwdrm video processing trusted application |
| [hwsec-host-utils](./hwsec-host-utils/) | Hwsec-related host-only utilities |
| [hwsec-optee-plugin](./hwsec-optee-plugin/) | Hwsec-related optee plugin |
| [hwsec-optee-ta](./hwsec-optee-ta/) | Hwsec-related optee plugin trusted application |
| [hwsec-test-utils](./hwsec-test-utils/) | Hwsec-related test-only features |
| [hwsec-utils](./hwsec-utils/) | Hwsec-related features |
| [iioservice](./iioservice/) | Daemon and libraries that provide sensor data to all processes |
| [image-burner](./image-burner/) | Daemon for writing disk images (e.g. recovery) to USB sticks & SD cards |
| [imageloader](./imageloader/) | Daemon for mounting signed disk images |
| [init](./init/) | CrOS common startup init scripts and boot time helpers |
| [installer](./installer/) | CrOS installer utility (for AU/recovery/etc...) |
| [ippusb_bridge](./ippusb_bridge/) | HTTP proxy to IPP-enabled printers |
| [kdump](./kdump/) | Fully featured kernel core debugging after a crash |
| [kerberos](./kerberos/) | Daemon for managing Kerberos tickets |
| [libarc-attestation](./libarc-attestation/) | Library to facilitate Android Attestation and Remote Key Provisioning for ARC Keymint Daemon |
| [libbrillo](./libbrillo/) | Common platform utility library |
| [libchromeos-rs](./libchromeos-rs/) | Common platform utility library for Rust |
| [libcontainer](./libcontainer/) ||
| [libcrossystem](./libcrossystem/) | Library for getting ChromeOS system properties |
| [libec](./libec/) | Library for interacting with [EC](https://chromium.googlesource.com/chromiumos/platform/ec/) |
| [libhwsec](./libhwsec/) | Library for the utility functions of all TPM related daemons except for trunks and trousers |
| [libhwsec-foundation](./libhwsec-foundation/) | Library for the utility functions of all TPM related daemons and libraries |
| [libipp](./libipp/) | Library for building and parsing IPP (Internet Printing Protocol) frames |
| [libmems](./libmems/) | Utility library to configure, manage and retrieve events from IIO sensors |
| [libpasswordprovider](./libpasswordprovider/) | Password Provider library for securely managing credentials with system services |
| [libpmt](./libpmt/) | Library for processing [Intel PMT](https://github.com/intel/Intel-PMT) data |
| [libsar](./libsar/) | Utility library to read the config file of IIO Sar sensors |
| [libsegmentation](./libsegmentation/) | Library to check which software features are allowed |
| [libstorage](./libstorage/) | Library presenting files, block devices and filesystems |
| [libtouchraw](./libtouchraw/) | Library for processing HID raw touch data |
| [login_manager](./login_manager/) | Session manager for handling the life cycle of the main session (e.g. Chrome) |
| [lorgnette](./lorgnette/) | Daemon for managing attached USB scanners via [SANE](https://en.wikipedia.org/wiki/Scanner_Access_Now_Easy) |
| [lvmd](./lvmd/) | ChromeOS LVM daemon |
| [machine-id-regen](./machine-id-regen/) | Utility to periodically update machine-id |
| [media_capabilities](./media_capabilities/) | Command line tool to show video and camera capabilities |
| [mems_setup](./mems_setup/) | Boot-time initializer tool for sensors |
| [metrics](./metrics/) | Client side user metrics collection |
| [midis](./midis/) | [MIDI](https://en.wikipedia.org/wiki/MIDI) service |
| [mini_udisks](./mini_udisks/) | Daemon providing a partial UDisks2 API for Flex firmware updates |
| [minios](./minios/) | A minimal OS used during recovery |
| [missive](./missive/) | Daemon for the storage of encrypted records for managed devices. |
| [mist](./mist/) | Modem USB Interface Switching Tool |
| [ml](./ml/) | Machine learning service |
| [ml_benchmark](./ml_benchmark/) | ML performance benchmark for ChromeOS |
| [ml_core](./ml_core/) | Machine learning feature library |
| [modem-utilities](./modem-utilities/) ||
| [modemfwd](./modemfwd/) | Daemon for managing modem firmware updaters |
| [modemloggerd](./modemloggerd/) | Daemon for managing modem logging tools |
| [mojo_service_manager](./mojo_service_manager/) | Daemon for managing mojo services |
| [mtpd](./mtpd/) | Daemon for handling Media Transfer Protocol (MTP) with devices (e.g. phones) |
| [net-base](./net-base/) | library of networking primitive data structure and common utilities |
| [nnapi](./nnapi/) | Implementation of the Android [Neural Networks API](https://developer.android.com/ndk/guides/neuralnetworks) |
| [ocr](./ocr/) | Optical Character Recognition (OCR) service for ChromeOS |
| [odml](./odml/) | On-device ML service for ChromeOS |
| [oobe_config](./oobe_config/) | Utilities for saving and restoring OOBE config state |
| [os_install_service](./os_install_service/) | Service that can be triggered by the UI to install CrOS to disk from a USB device |
| [p2p](./p2p/) | Service for sharing files between CrOS devices (e.g. updates) |
| [parallax](./parallax/) | Visual Analysis Framework |
| [patchmaker](./patchmaker/) | Utility for bsdiff-encoding a directory of binaries |
| [patchpanel](./patchpanel/) | Platform networking daemons |
| [pciguard](./pciguard/) | Daemon to secure external PCI devices (thunderbolt etc) |
| [perfetto_simple_producer](./perfetto_simple_producer/) | A simple producer of perfetto: An example demonstrating how to produce Perfetto performance trace data |
| [permission_broker](./permission_broker/) ||
| [pmt_tool](./pmt_tool/) | Command-line utility for sampling and decoding of Intel PMT data |
| [policy_proto](./policy_proto/) | Build file to compile policy proto file |
| [policy_utils](./policy_utils/) | Tools and related library to set or override device policies |
| [power_manager](./power_manager/) | Userspace power management daemon and associated tools |
| [primary_io_manager](./primary_io_manager/) | Tracks primary input devices for chromeboxes |
| [print_tools](./print_tools/) | Various tools related to the native printing system |
| [printscanmgr](./printscanmgr/) | ChromeOS Printing and Scanning Daemon |
| [privacy](./privacy/) | ChromeOS privacy tools |
| [private_computing](./private_computing/) | Daemon to save and retrieve device active date status into and from preserved file.
| [pwgtocanonij](./pwgtocanonij/) | CUPS filter for certain Canon printers |
| [regions](./regions/) ||
| [regmon](./regmon/) | Daemon to report policy violations of first-party network traffic. |
| [resourced](./resourced/) | Resource Management Daemon |
| [rgbkbd](./rgbkbd/) | ChromeOS RGB Keyboard Daemon |
| [rmad](./rmad/) | ChromeOS RMA Daemon |
| [routing-simulator](./routing-simulator/) | Debugging tool for routing subsystem |
| [run_oci](./run_oci/) | Minimalistic container runtime |
| [runtime_probe](./runtime_probe/) | Runtime probe tool for ChromeOS |
| [sandboxing-codelab](./sandboxing-codelab/) | Sandboxing exercise |
| [screen-capture-utils](./screen-capture-utils/) | Utilities for screen capturing (screenshot) |
| [secagentd](./secagentd/) | Daemon for detecting and reporting security related events |
| [secanomalyd](./secanomalyd/) | Daemon for detecting and reporting security anomalies |
| [secure-wipe](./secure-wipe/) | Secure disk wipe |
| [secure_erase_file](./secure_erase_file/) | Helper tools for securely erasing files from storage (e.g. keys and PII data) |
| [sepolicy](./sepolicy/) | SELinux policy for ChromeOS |
| [shadercached](./shadercached/) | Shader cache management daemon |
| [shill](./shill/) | ChromeOS Connection Manager |
| [smbfs](./smbfs/) | FUSE-based filesystem for accessing Samba / Windows networking shares |
| [smbprovider](./smbprovider/) | Daemon for connecting Samba / Windows networking shares to the Files.app |
| [soul](./soul/) | Daemon and utilities for system logs |
| [spaced](./spaced/) | Disk space information daemon |
| [storage_info](./storage_info/) | Helper shell functions for retrieving disk information) |
| [swap_management](./swap_management/) | Swap management service |
| [syslog-cat](./syslog-cat/) | Helper command to forward stdout/stderr from process to syslog |
| [system-proxy](./system-proxy/) | Daemon for web proxy authentication support on ChromeOS |
| [system_api](./system_api/) | Headers and .proto files etc. to be shared with chromium |
| [thinpool_migrator](./thinpool_migrator/) | Tool for migrating the stateful filesystem to use LVM |
| [timberslide](./timberslide/) | Tool for working with EC crashes for reporting purposes |
| [touch_firmware_calibration](./touch_firmware_calibration/) ||
| [tpm2-simulator](./tpm2-simulator/) | A software TPM 2.0 implementation (for testing/debugging) |
| [tpm_manager](./tpm_manager/) | Daemon and client for managing TPM setup and operations |
| [tpm_softclear_utils](./tpm_softclear_utils/) | Utilities that soft-clear TPM (for testing only) |
| [trim](./trim/) | Service to manage filesystem trim operations in the background |
| [trunks](./trunks/) | Middleware and resource manager for interfacing with TPM 2.0 hardware |
| [typecd](./typecd/) | System daemon to keep track of USB Type C state |
| [u2fd](./u2fd/) | U2FHID emulation daemon for systems with secure elements (not TPMs) |
| [update_engine](./update_engine/) | System updater daemon |
| [ureadahead-diff](./ureadahead-diff/) | Tool to calculate difference between 2 ureadahead packs |
| [usb-debug-utils](./usb-debug-utils/) | Extra tools for debugging USB |
| [usb_bouncer](./usb_bouncer/) | Tools for managing USBGuard white-lists and configuration on ChromeOS |
| [userfeedback](./userfeedback/) | Various utilities to gather extended data for user feedback reports |
| [uwbd](./uwbd/) | Daemon for the UWB on ChromeOS |
| [verity](./verity/) | Userspace tools for working dm-verity (verified disk images) |
| [virtual_file_provider](./virtual_file_provider/) ||
| [vm_tools](./vm_tools/) | Utilities for Virtual Machine (VM) orchestration |
| [vtpm](./vtpm/) | ChromeOS virtual TPM Daemon |

# AOSP Project Directory

These projects can be found here:
https://chromium.googlesource.com/aosp/platform/
