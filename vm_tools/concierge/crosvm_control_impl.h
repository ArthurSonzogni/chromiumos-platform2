// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_CROSVM_CONTROL_IMPL_H_
#define VM_TOOLS_CONCIERGE_CROSVM_CONTROL_IMPL_H_

#include <optional>

#include <base/time/time.h>

#include "vm_tools/concierge/crosvm_control.h"

#include <string>

namespace vm_tools::concierge {

class CrosvmControlImpl : public CrosvmControl {
 public:
  static void Init();

  explicit CrosvmControlImpl(const CrosvmControl&) = delete;
  CrosvmControlImpl& operator=(const CrosvmControl&) = delete;

  bool StopVm(const std::string& socket_path) override;
  bool SuspendVm(const std::string& socket_path) override;
  bool ResumeVm(const std::string& socket_path) override;
  bool MakeRtVm(const std::string& socket_path) override;
  bool SetBalloonSize(const std::string& socket_path,
                      size_t num_bytes) override;
  uintptr_t MaxUsbDevices() override;
  ssize_t UsbList(const std::string& socket_path,
                  struct UsbDeviceEntry* entries,
                  ssize_t entries_length) override;
  bool UsbAttach(const std::string& socket_path,
                 uint8_t bus,
                 uint8_t addr,
                 uint16_t vid,
                 uint16_t pid,
                 const std::string& dev_path,
                 uint8_t* out_port) override;
  bool UsbDetach(const std::string& socket_path, uint8_t port) override;
  bool ModifyBattery(const std::string& socket_path,
                     const std::string& battery_type,
                     const std::string& property,
                     const std::string& target) override;
  bool ResizeDisk(const std::string& socket_path,
                  size_t disk_index,
                  uint64_t new_size) override;
  bool BalloonStats(const std::string& socket_path,
                    std::optional<base::TimeDelta> timeout,
                    struct BalloonStatsFfi* stats,
                    uint64_t* actual) override;
  bool SetBalloonWorkingSetConfig(const std::string& socket_path,
                                  const BalloonWSRConfigFfi* config) override;
  bool BalloonWorkingSet(const std::string& socket_path,
                         struct BalloonWSFfi* stats,
                         uint64_t* actual) override;
  bool EnableVmmSwap(const std::string& socket_path) override;
  bool VmmSwapOut(const std::string& socket_path) override;
  bool VmmSwapTrim(const std::string& socket_path) override;
  bool DisableVmmSwap(const std::string& socket_path,
                      bool slow_file_cleanup) override;
  bool VmmSwapStatus(const std::string& socket_path,
                     struct SwapStatus* status) override;

 private:
  CrosvmControlImpl() = default;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_CROSVM_CONTROL_IMPL_H_
