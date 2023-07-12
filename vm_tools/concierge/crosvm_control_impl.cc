// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/crosvm_control_impl.h"

#include <memory>
#include <string>

#include <base/memory/ptr_util.h>

namespace vm_tools::concierge {

void CrosvmControlImpl::Init() {
  SetInstance(base::WrapUnique(new CrosvmControlImpl()));
}

bool CrosvmControlImpl::StopVm(const std::string& socket_path) {
  return crosvm_client_stop_vm(socket_path.c_str());
}

bool CrosvmControlImpl::SuspendVm(const std::string& socket_path) {
  return crosvm_client_suspend_vm(socket_path.c_str());
}

bool CrosvmControlImpl::ResumeVm(const std::string& socket_path) {
  return crosvm_client_resume_vm(socket_path.c_str());
}

bool CrosvmControlImpl::MakeRtVm(const std::string& socket_path) {
  return crosvm_client_make_rt_vm(socket_path.c_str());
}

bool CrosvmControlImpl::SetBalloonSize(const std::string& socket_path,
                                       size_t num_bytes) {
  return crosvm_client_balloon_vms(socket_path.c_str(), num_bytes);
}

uintptr_t CrosvmControlImpl::MaxUsbDevices() {
  return crosvm_client_max_usb_devices();
}

ssize_t CrosvmControlImpl::UsbList(const std::string& socket_path,
                                   struct UsbDeviceEntry* entries,
                                   ssize_t entries_length) {
  return crosvm_client_usb_list(socket_path.c_str(), entries, entries_length);
}

bool CrosvmControlImpl::UsbAttach(const std::string& socket_path,
                                  uint8_t bus,
                                  uint8_t addr,
                                  uint16_t vid,
                                  uint16_t pid,
                                  const std::string& dev_path,
                                  uint8_t* out_port) {
  return crosvm_client_usb_attach(socket_path.c_str(), bus, addr, vid, pid,
                                  dev_path.c_str(), out_port);
}

bool CrosvmControlImpl::UsbDetach(const std::string& socket_path,
                                  uint8_t port) {
  return crosvm_client_usb_detach(socket_path.c_str(), port);
}

bool CrosvmControlImpl::ModifyBattery(const std::string& socket_path,
                                      const std::string& battery_type,
                                      const std::string& property,
                                      const std::string& target) {
  return crosvm_client_modify_battery(socket_path.c_str(), battery_type.c_str(),
                                      property.c_str(), target.c_str());
}

bool CrosvmControlImpl::ResizeDisk(const std::string& socket_path,
                                   size_t disk_index,
                                   uint64_t new_size) {
  return crosvm_client_resize_disk(socket_path.c_str(), disk_index, new_size);
}

bool CrosvmControlImpl::BalloonStats(const std::string& socket_path,
                                     std::optional<base::TimeDelta> timeout,
                                     struct BalloonStatsFfi* stats,
                                     uint64_t* actual) {
  if (timeout) {
    uint64_t timeout_ms = timeout->InMilliseconds();
    return crosvm_client_balloon_stats_with_timeout(socket_path.c_str(),
                                                    timeout_ms, stats, actual);
  } else {
    return crosvm_client_balloon_stats(socket_path.c_str(), stats, actual);
  }
}

bool CrosvmControlImpl::SetBalloonWorkingSetConfig(
    const std::string& socket_path, const BalloonWssConfigFfi* config) {
  return crosvm_client_balloon_wss_config(socket_path.c_str(), config);
}

bool CrosvmControlImpl::BalloonWorkingSet(const std::string& socket_path,
                                          struct BalloonWSSFfi* working_set,
                                          uint64_t* actual) {
  return crosvm_client_balloon_wss(socket_path.c_str(), working_set, actual);
}

bool CrosvmControlImpl::EnableVmmSwap(const std::string& socket_path) {
  return crosvm_client_swap_enable_vm(socket_path.c_str());
}

bool CrosvmControlImpl::VmmSwapOut(const std::string& socket_path) {
  return crosvm_client_swap_swapout_vm(socket_path.c_str());
}

bool CrosvmControlImpl::VmmSwapTrim(const std::string& socket_path) {
  return crosvm_client_swap_trim(socket_path.c_str());
}

bool CrosvmControlImpl::DisableVmmSwap(const std::string& socket_path,
                                       bool slow_file_cleanup) {
  struct SwapDisableArgs args = {
      .socket_path = socket_path.c_str(),
      .slow_file_cleanup = slow_file_cleanup,
  };
  return crosvm_client_swap_disable_vm(&args);
}

bool CrosvmControlImpl::VmmSwapStatus(const std::string& socket_path,
                                      struct SwapStatus* status) {
  return crosvm_client_swap_status(socket_path.c_str(), status);
}

}  // namespace vm_tools::concierge
