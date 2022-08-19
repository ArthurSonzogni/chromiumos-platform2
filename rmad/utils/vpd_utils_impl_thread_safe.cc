// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/vpd_utils_impl_thread_safe.h"

#include <map>
#include <string>
#include <vector>

#include <base/synchronization/lock.h>

namespace rmad {

bool VpdUtilsImplThreadSafe::GetSerialNumber(std::string* serial_number) const {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::GetSerialNumber(serial_number);
}

bool VpdUtilsImplThreadSafe::GetCustomLabelTag(std::string* custom_label_tag,
                                               bool use_legacy) const {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::GetCustomLabelTag(custom_label_tag, use_legacy);
}

bool VpdUtilsImplThreadSafe::GetRegion(std::string* region) const {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::GetRegion(region);
}

bool VpdUtilsImplThreadSafe::GetCalibbias(
    const std::vector<std::string>& entries,
    std::vector<int>* calibbias) const {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::GetCalibbias(entries, calibbias);
}

bool VpdUtilsImplThreadSafe::GetRegistrationCode(std::string* ubind,
                                                 std::string* gbind) const {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::GetRegistrationCode(ubind, gbind);
}

bool VpdUtilsImplThreadSafe::GetStableDeviceSecret(
    std::string* stable_device_secret) const {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::GetStableDeviceSecret(stable_device_secret);
}

bool VpdUtilsImplThreadSafe::SetSerialNumber(const std::string& serial_number) {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::SetSerialNumber(serial_number);
}

bool VpdUtilsImplThreadSafe::SetCustomLabelTag(
    const std::string& custom_label_tag, bool use_legacy) {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::SetCustomLabelTag(custom_label_tag, use_legacy);
}

bool VpdUtilsImplThreadSafe::SetRegion(const std::string& region) {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::SetRegion(region);
}

bool VpdUtilsImplThreadSafe::SetCalibbias(
    const std::map<std::string, int>& calibbias) {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::SetCalibbias(calibbias);
}

bool VpdUtilsImplThreadSafe::SetRegistrationCode(const std::string& ubind,
                                                 const std::string& gbind) {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::SetRegistrationCode(ubind, gbind);
}

bool VpdUtilsImplThreadSafe::SetStableDeviceSecret(
    const std::string& stable_device_secret) {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::SetStableDeviceSecret(stable_device_secret);
}

bool VpdUtilsImplThreadSafe::RemoveCustomLabelTag() {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::RemoveCustomLabelTag();
}

bool VpdUtilsImplThreadSafe::FlushOutRoVpdCache() {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::FlushOutRoVpdCache();
}

bool VpdUtilsImplThreadSafe::FlushOutRwVpdCache() {
  base::AutoLock scoped_lock(lock_);

  return VpdUtilsImpl::FlushOutRwVpdCache();
}

void VpdUtilsImplThreadSafe::ClearRoVpdCache() {
  base::AutoLock scoped_lock(lock_);

  VpdUtilsImpl::ClearRoVpdCache();
}

void VpdUtilsImplThreadSafe::ClearRwVpdCache() {
  base::AutoLock scoped_lock(lock_);

  VpdUtilsImpl::ClearRwVpdCache();
}

}  // namespace rmad
