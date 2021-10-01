// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_VPD_UTILS_IMPL_H_
#define RMAD_UTILS_VPD_UTILS_IMPL_H_

#include "rmad/utils/vpd_utils.h"

#include <map>
#include <string>
#include <vector>

namespace rmad {

// Calls `vpd` command to set/get RO/RW VPD values. The subprocess needs access
// to /dev/mem and has CAP_SYS_RAWIO,CAP_DAC_OVERRIDE capability if not running
// as root.
class VpdUtilsImpl : public VpdUtils {
 public:
  VpdUtilsImpl() = default;
  ~VpdUtilsImpl();

  bool GetSerialNumber(std::string* serial_number) const override;
  bool GetWhitelabelTag(std::string* whitelabel_tag) const override;
  bool GetRegion(std::string* region) const override;
  bool GetCalibbias(const std::vector<std::string>& entries,
                    std::vector<int>* calibbias) const override;
  bool SetSerialNumber(const std::string& serial_number) override;
  bool SetWhitelabelTag(const std::string& whitelabel_tag) override;
  bool SetRegion(const std::string& region) override;
  bool SetCalibbias(const std::map<std::string, int>& calibbias) override;
  bool FlushOutRoVpdCache() override;
  bool FlushOutRwVpdCache() override;

 protected:
  bool SetRoVpd(
      const std::map<std::string, std::string>& key_value_map) override;
  bool GetRoVpd(const std::string& key, std::string* value) const override;
  bool SetRwVpd(
      const std::map<std::string, std::string>& key_value_map) override;
  bool GetRwVpd(const std::string& key, std::string* value) const override;

 private:
  // RO VPD
  std::map<std::string, std::string> cache_ro_;
  // RW VPD
  std::map<std::string, std::string> cache_rw_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_VPD_UTILS_IMPL_H_
