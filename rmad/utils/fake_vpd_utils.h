// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_VPD_UTILS_H_
#define RMAD_UTILS_FAKE_VPD_UTILS_H_

#include "rmad/utils/vpd_utils.h"

#include <map>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>

#include "rmad/utils/json_store.h"

namespace rmad {
namespace fake {

class FakeVpdUtils : public VpdUtils {
 public:
  explicit FakeVpdUtils(const base::FilePath& working_dir_path);
  ~FakeVpdUtils() override = default;

  bool GetSerialNumber(std::string* serial_number) const override;
  bool GetWhitelabelTag(std::string* whitelabel_tag) const override;
  bool GetRegion(std::string* region) const override;
  bool GetCalibbias(const std::vector<std::string>& entries,
                    std::vector<int>* calibbias) const override;
  bool GetRegistrationCode(std::string* ubind,
                           std::string* gbind) const override;
  bool GetStableDeviceSecret(std::string* stable_device_secret) const override;
  bool SetSerialNumber(const std::string& serial_number) override;
  bool SetWhitelabelTag(const std::string& whitelabel_tag) override;
  bool SetRegion(const std::string& region) override;
  bool SetCalibbias(const std::map<std::string, int>& calibbias) override;
  bool SetRegistrationCode(const std::string& ubind,
                           const std::string& gbind) override;
  bool SetStableDeviceSecret(const std::string& stable_device_secret) override;
  bool FlushOutRoVpdCache() override;
  bool FlushOutRwVpdCache() override;
  void ClearRoVpdCache() override;
  void ClearRwVpdCache() override;

 private:
  base::FilePath working_dir_path_;
  // Use |JsonStore| to read the fake VPD values.
  scoped_refptr<JsonStore> json_store_;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_VPD_UTILS_H_
