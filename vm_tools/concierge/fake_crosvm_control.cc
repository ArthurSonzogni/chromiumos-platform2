// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/fake_crosvm_control.h"

namespace vm_tools::concierge {

void FakeCrosvmControl::Init() {
  CrosvmControl::SetInstance(
      std::unique_ptr<CrosvmControl>(new FakeCrosvmControl()));
}

FakeCrosvmControl* FakeCrosvmControl::Get() {
  return reinterpret_cast<FakeCrosvmControl*>(CrosvmControl::Get());
}

bool FakeCrosvmControl::StopVm(const char* socket_path) {
  target_socket_path_ = socket_path;
  return true;
}

bool FakeCrosvmControl::SuspendVm(const char* socket_path) {
  target_socket_path_ = socket_path;
  return true;
}

bool FakeCrosvmControl::ResumeVm(const char* socket_path) {
  target_socket_path_ = socket_path;
  return true;
}

bool FakeCrosvmControl::MakeRtVm(const char* socket_path) {
  target_socket_path_ = socket_path;
  return true;
}

bool FakeCrosvmControl::SetBalloonSize(const char* socket_path,
                                       size_t num_bytes) {
  target_socket_path_ = socket_path;
  return true;
}

uintptr_t FakeCrosvmControl::MaxUsbDevices() {
  return 0;
}

ssize_t FakeCrosvmControl::UsbList(const char* socket_path,
                                   struct UsbDeviceEntry* entries,
                                   ssize_t entries_length) {
  target_socket_path_ = socket_path;
  return 0;
}

bool FakeCrosvmControl::UsbAttach(const char* socket_path,
                                  uint8_t bus,
                                  uint8_t addr,
                                  uint16_t vid,
                                  uint16_t pid,
                                  const char* dev_path,
                                  uint8_t* out_port) {
  target_socket_path_ = socket_path;
  return true;
}

bool FakeCrosvmControl::UsbDetach(const char* socket_path, uint8_t port) {
  target_socket_path_ = socket_path;
  return true;
}

bool FakeCrosvmControl::ModifyBattery(const char* socket_path,
                                      const char* battery_type,
                                      const char* property,
                                      const char* target) {
  target_socket_path_ = socket_path;
  return true;
}

bool FakeCrosvmControl::ResizeDisk(const char* socket_path,
                                   size_t disk_index,
                                   uint64_t new_size) {
  target_socket_path_ = socket_path;
  return true;
}

bool FakeCrosvmControl::BalloonStats(const char* socket_path,
                                     struct BalloonStatsFfi* stats,
                                     uint64_t* actual) {
  target_socket_path_ = socket_path;
  *actual = actual_balloon_size_;
  *stats = balloon_stats_;
  return true;
}

}  // namespace vm_tools::concierge
