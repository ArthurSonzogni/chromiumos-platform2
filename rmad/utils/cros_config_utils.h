// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CROS_CONFIG_UTILS_H_
#define RMAD_UTILS_CROS_CONFIG_UTILS_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rmad {

// Rmad config and SSFC config structures defined in cros_config.
// See platform2/chromeos-config/README.md#rmad for more details.
struct SsfcComponentTypeConfig {
  std::string component_type;
  uint32_t default_value;
  std::map<std::string, uint32_t> probeable_components;
};

struct SsfcConfig {
  uint32_t mask;
  std::vector<SsfcComponentTypeConfig> component_type_configs;
};

struct RmadConfig {
  bool enabled;
  bool has_cbi;
  SsfcConfig ssfc;
  bool use_legacy_custom_label;
};

// A collection of design config parsed from each entry of cros_config database.
struct DesignConfig {
  std::string model_name;
  std::optional<uint32_t> sku_id;
  std::optional<std::string> custom_label_tag;
};

class CrosConfigUtils {
 public:
  CrosConfigUtils() = default;
  virtual ~CrosConfigUtils() = default;

  // Get cros_config attributes of the device.
  virtual bool GetRmadConfig(RmadConfig* config) const = 0;
  virtual bool GetModelName(std::string* model_name) const = 0;
  virtual bool GetBrandCode(std::string* brand_code) const = 0;
  virtual bool GetSkuId(uint32_t* sku_id) const = 0;
  virtual bool GetCustomLabelTag(std::string* custom_label_tag) const = 0;
  virtual bool GetFirmwareConfig(uint32_t* firmware_config) const = 0;

  // Get cros_config attributes of all supported designs from the database.
  virtual bool GetDesignConfigList(
      std::vector<DesignConfig>* design_config_list) const = 0;
  virtual bool GetSkuIdList(std::vector<uint32_t>* sku_id_list) const = 0;
  virtual bool GetCustomLabelTagList(
      std::vector<std::string>* custom_label_tag_list) const = 0;

  // Other helper functions.
  bool HasCustomLabel() const;
};

}  // namespace rmad

#endif  // RMAD_UTILS_CROS_CONFIG_UTILS_H_
