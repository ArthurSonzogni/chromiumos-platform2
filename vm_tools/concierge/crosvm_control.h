// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_CROSVM_CONTROL_H_
#define VM_TOOLS_CONCIERGE_CROSVM_CONTROL_H_

#include <crosvm/crosvm_control.h>

#include <memory>
#include <optional>
#include <string>

#include <base/time/time.h>

namespace vm_tools::concierge {

// Wrapper class for the crosvm_control library.
// Provides a simple pass through to the library, but also allows for a
// mock to be injected for testing.

class CrosvmControl {
 public:
  // Returns the global instance.
  static CrosvmControl* Get();

  // Resets the global instance.
  static void Reset();

  // Stops the crosvm instance whose control socket is listening on
  // `socket_path`.
  //
  // The function returns true on success or false if an error occurred.
  virtual bool StopVm(const std::string& socket_path);

  // Suspends the crosvm instance whose control socket is listening on
  // `socket_path`.
  //
  // The function returns true on success or false if an error occurred.
  virtual bool SuspendVm(const std::string& socket_path);

  // Resumes the crosvm instance whose control socket is listening on
  // `socket_path`.
  //
  // The function returns true on success or false if an error occurred.
  virtual bool ResumeVm(const std::string& socket_path);

  // Creates an RT vCPU for the crosvm instance whose control socket is
  // listening on `socket_path`.
  //
  // The function returns true on success or false if an error occurred.
  virtual bool MakeRtVm(const std::string& socket_path);

  // Adjusts the balloon size of the crosvm instance whose control socket is
  // listening on `socket_path`.
  //
  // The function returns true on success or false if an error occurred.
  virtual bool SetBalloonSize(const std::string& socket_path,
                              size_t num_bytes,
                              std::optional<base::TimeDelta> timeout);

  // Returns the maximum possible number of USB devices.
  virtual size_t MaxUsbDevices();

  // Returns all USB devices passed through the crosvm instance whose control
  // socket is listening on `socket_path`.
  //
  // The function returns the number of entries written.
  // Arguments
  //
  // `socket_path` - Path to the crosvm control socket.
  // `entries` - Pointer to an array of `UsbDeviceEntry` where the details
  //  about the attached devices will be written to.
  // `entries_length` - Number of entries in the array specified by `entries`
  //
  // Use the value returned by [`crosvm_client_max_usb_devices()`] to determine
  // the size of the input array to this function.
  virtual ssize_t UsbList(const std::string& socket_path,
                          struct UsbDeviceEntry* entries,
                          ssize_t entries_length);

  // Attaches a network tap device to crosvm instance whose control socket is
  // listening on `socket_path`.
  //
  // Arguments
  // `socket_path` - Path to the crosvm control socket
  // `tap_name` - Interface name of tap device.
  // `out_bus` - guest bus number will be written here.
  //
  // The function returns true on success or false if an error occurred.
  virtual bool NetAttach(const std::string& socket_path,
                         const std::string& tap_name,
                         uint8_t* out_bus);

  // Detaches a network tap device to crosvm instance whose control socket is
  // listening on `socket_path`.
  //
  // Arguments
  // `socket_path` - Path to the crosvm control socket
  // `bus` - guest bus number of the device to remove
  //
  // The function returns true on success or false if an error occurred.
  virtual bool NetDetach(const std::string& socket_path, uint8_t bus);

  // Attaches an USB device to crosvm instance whose control socket is listening
  // on `socket_path`.
  //
  // The function returns the number of entries written.
  // Arguments
  //
  // `socket_path` - Path to the crosvm control socket
  // `bus` - USB device bus ID (unused)
  // `addr` - USB device address (unused)
  // `vid` - USB device vendor ID (unused)
  // `pid` - USB device product ID (unused)
  // `dev_path` - Path to the USB device (Most likely
  // `/dev/bus/usb/<bus>/<addr>`).
  // `out_port` - (optional) internal port will be written here if provided.
  //
  // The function returns true on success or false if an error occurred.
  virtual bool UsbAttach(const std::string& socket_path,
                         uint8_t bus,
                         uint8_t addr,
                         uint16_t vid,
                         uint16_t pid,
                         const std::string& dev_path,
                         uint8_t* out_port);

  // Detaches an USB device from crosvm instance whose control socket is
  // listening on `socket_path`. `port` determines device to be detached.
  //
  // The function returns true on success or false if an error occurred.
  virtual bool UsbDetach(const std::string& socket_path, uint8_t port);

  // Modifies the battery status of crosvm instance whose control socket is
  // listening on `socket_path`.
  //
  // The function returns true on success or false if an error occurred.
  virtual bool ModifyBattery(const std::string& socket_path,
                             const std::string& battery_type,
                             const std::string& property,
                             const std::string& target);

  // Resizes the disk of the crosvm instance whose control socket is listening
  // on `socket_path`.
  //
  // The function returns true on success or false if an error occurred.
  virtual bool ResizeDisk(const std::string& socket_path,
                          size_t disk_index,
                          uint64_t new_size);

  // Returns balloon stats of the crosvm instance whose control socket is
  // listening on `socket_path`.
  //
  // The parameters `stats` and `actual` are optional and will only be written
  // to if they are non-null.
  //
  // The function returns true on success or false if an error occurred.
  //
  // Note
  //
  // Entries in `BalloonStatsFfi` that are not available will be set to `-1`.
  virtual bool BalloonStats(const std::string& socket_path,
                            std::optional<base::TimeDelta> timeout,
                            struct BalloonStatsFfi* stats,
                            uint64_t* actual);

  // Set working set config in guest.
  // The function returns true on success or false if an error occurred.
  virtual bool SetBalloonWorkingSetConfig(const std::string& socket_path,
                                          const BalloonWSRConfigFfi* config);

  // Returns guest working set of the crosvm instance whose control socket is
  // listening on `socket_path`.
  //
  // The function returns true on success or false if an error occurred.
  virtual bool BalloonWorkingSet(const std::string& socket_path,
                                 struct BalloonWSFfi* ws,
                                 uint64_t* actual);

  // Enable vmm-swap of crosvm and move all the guest memory to the staging
  // memory.
  //
  // This affects the crosvm instance whose control socket is listening on
  // `socket_path`.
  virtual bool EnableVmmSwap(const std::string& socket_path);

  // Swap out the staging memory to the swap file.
  //
  // This affects the crosvm instance whose control socket is listening on
  // `socket_path`.
  virtual bool VmmSwapOut(const std::string& socket_path);

  // Trim static pages and zero pages in the staging memory.
  //
  // This affects the crosvm instance whose control socket is listening on
  // `socket_path`.
  virtual bool VmmSwapTrim(const std::string& socket_path);

  // Disable vmm-swap of crosvm.
  //
  // This affects the crosvm instance whose control socket is listening on
  // `socket_path`. If `slow_file_cleanup` is true, allows crosvm to clean
  // up the swap file in the background.
  virtual bool DisableVmmSwap(const std::string& socket_path,
                              bool slow_file_cleanup);

  // Returns vmm-swap status of the crosvm instance whose control socket is
  // listening on `socket_path`.
  //
  // The parameters `status`is optional and will only be written to if they are
  // non-null.
  //
  // The function returns true on success or false if an error occurred.
  virtual bool VmmSwapStatus(const std::string& socket_path,
                             struct SwapStatus* status);

  virtual ~CrosvmControl() = default;

 protected:
  static void SetInstance(std::unique_ptr<CrosvmControl> instance);
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_CROSVM_CONTROL_H_
