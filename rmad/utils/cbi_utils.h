// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CBI_UTILS_H_
#define RMAD_UTILS_CBI_UTILS_H_

#include <string>

namespace rmad {

class CbiUtils {
 public:
  CbiUtils() = default;
  virtual ~CbiUtils() = default;

  // Get the sku id of the device from cbi.
  virtual bool GetSkuId(uint32_t* sku_id) const = 0;

  // Get the dram part number of the device from cbi.
  virtual bool GetDramPartNum(std::string* dram_part_num) const = 0;

  // Get the second source factory cache of the device from cbi.
  virtual bool GetSsfc(uint32_t* ssfc) const = 0;

  // Get the firmware config of the device from cbi.
  virtual bool GetFirmwareConfig(uint32_t* firmware_config) const = 0;

  // Set the sku id of the device to cbi.
  virtual bool SetSkuId(uint32_t sku_id) = 0;

  // Set the dram part number of the device to cbi.
  virtual bool SetDramPartNum(const std::string& dram_part_num) = 0;

  // Set the second source factory cache of the device to cbi.
  virtual bool SetSsfc(uint32_t ssfc) = 0;

  // Set the firmware config of the device to cbi.
  virtual bool SetFirmwareConfig(uint32_t firmware_config) = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_CBI_UTILS_H_
