// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_VPD_UTILS_IMPL_THREAD_SAFE_H_
#define RMAD_UTILS_VPD_UTILS_IMPL_THREAD_SAFE_H_

#include "rmad/utils/vpd_utils_impl.h"

#include <map>
#include <string>
#include <vector>

#include <base/memory/ref_counted.h>
#include <base/synchronization/lock.h>

namespace rmad {

// Call the `vpd` command in a multi-threaded environment to set/get the RO/RW
// VPD values. The sub-process needs to access /dev/mem and needs
// CAP_SYS_RAWIO, CAP_DAC_OVERRIDE capabilities (if not running as root).
class VpdUtilsImplThreadSafe
    : public VpdUtilsImpl,
      public base::RefCountedThreadSafe<VpdUtilsImplThreadSafe> {
 public:
  VpdUtilsImplThreadSafe() = default;

  // Override high level commands to ensure that the vpd command is only called
  // once at the same time.
  bool GetSerialNumber(std::string* serial_number) const override;
  bool GetWhitelabelTag(std::string* whitelabel_tag) const override;
  bool GetRegion(std::string* region) const override;
  bool GetCalibbias(const std::vector<std::string>& entries,
                    std::vector<int>* calibbias) const override;
  bool GetRegistrationCode(std::string* ubind,
                           std::string* gbind) const override;
  bool SetSerialNumber(const std::string& serial_number) override;
  bool SetWhitelabelTag(const std::string& whitelabel_tag) override;
  bool SetRegion(const std::string& region) override;
  bool SetCalibbias(const std::map<std::string, int>& calibbias) override;
  bool SetRegistrationCode(const std::string& ubind,
                           const std::string& gbind) override;
  bool FlushOutRoVpdCache() override;
  bool FlushOutRwVpdCache() override;

 protected:
  friend base::RefCountedThreadSafe<VpdUtilsImplThreadSafe>;
  // Refcounted object must have destructor declared protected or private.
  ~VpdUtilsImplThreadSafe() override = default;

 private:
  mutable base::Lock lock_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_VPD_UTILS_IMPL_THREAD_SAFE_H_
