// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_LVM_LVMD_PROXY_WRAPPER_H_
#define DLCSERVICE_LVM_LVMD_PROXY_WRAPPER_H_

#include <memory>
#include <string>
#include <vector>

#include <lvmd/proto_bindings/lvmd.pb.h>
#include <lvmd/dbus-proxies.h>

namespace dlcservice {

// Provides a simpler interface into lvmd.
class LvmdProxyWrapperInterface {
 public:
  virtual ~LvmdProxyWrapperInterface() = default;

  // Creates the logical volumes.
  virtual bool CreateLogicalVolumes(
      const std::vector<lvmd::LogicalVolumeConfiguration>& lv_configs) = 0;

  // Removes the logical volumes, if they exist.
  virtual bool RemoveLogicalVolumes(
      const std::vector<std::string>& lv_names) = 0;
};

class LvmdProxyWrapper : public LvmdProxyWrapperInterface {
 public:
  using LvmdProxyInterface = org::chromium::LvmdProxyInterface;
  explicit LvmdProxyWrapper(std::unique_ptr<LvmdProxyInterface> lvmd_proxy);
  ~LvmdProxyWrapper() = default;

  LvmdProxyWrapper(const LvmdProxyWrapper&) = delete;
  LvmdProxyWrapper& operator=(const LvmdProxyWrapper&) = delete;

  // `LvmdProxyWrapper` overrides.
  bool CreateLogicalVolumes(
      const std::vector<lvmd::LogicalVolumeConfiguration>& lv_configs) override;
  bool RemoveLogicalVolumes(const std::vector<std::string>& lv_names) override;

 private:
  bool GetPhysicalVolume(const std::string& device_path,
                         lvmd::PhysicalVolume* pv);
  bool GetVolumeGroup(const lvmd::PhysicalVolume& pv, lvmd::VolumeGroup* vg);
  bool GetThinpool(const lvmd::VolumeGroup& vg, lvmd::Thinpool* thinpool);
  bool GetLogicalVolume(const lvmd::VolumeGroup& vg,
                        const std::string& lv_name,
                        lvmd::LogicalVolume* lv);
  bool CreateLogicalVolume(const lvmd::Thinpool& thinpool,
                           const lvmd::LogicalVolumeConfiguration& lv_config,
                           lvmd::LogicalVolume* lv);
  bool RemoveLogicalVolume(const lvmd::LogicalVolume& lv);

  std::unique_ptr<LvmdProxyInterface> lvmd_proxy_;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_LVM_LVMD_PROXY_WRAPPER_H_
