// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_VPD_UTILS_IMPL_H_
#define RMAD_UTILS_VPD_UTILS_IMPL_H_

#include "rmad/utils/vpd_utils.h"

#include <string>

namespace rmad {

// Calls `vpd` command to set/get RO/RW VPD values. The subprocess needs access
// to /dev/mem and has CAP_SYS_RAWIO,CAP_DAC_OVERRIDE capability if not running
// as root.
class VpdUtilsImpl : public VpdUtils {
 public:
  VpdUtilsImpl() = default;
  ~VpdUtilsImpl() = default;

  bool GetSerialNumber(std::string* serial_number) const override;
  bool GetWhitelabelTag(std::string* whitelabel_tag) const override;
  bool GetRegion(std::string* region) const override;
  bool SetSerialNumber(const std::string& serial_number) override;
  bool SetWhitelabelTag(const std::string& whitelabel_tag) override;
  bool SetRegion(const std::string& region) override;

 protected:
  bool SetRoVpd(const std::string& key, const std::string& value) override;
  bool GetRoVpd(const std::string& key, std::string* value) const override;
  bool SetRwVpd(const std::string& key, const std::string& value) override;
  bool GetRwVpd(const std::string& key, std::string* value) const override;
};

}  // namespace rmad

#endif  // RMAD_UTILS_VPD_UTILS_IMPL_H_
