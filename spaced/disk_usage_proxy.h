// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPACED_DISK_USAGE_PROXY_H_
#define SPACED_DISK_USAGE_PROXY_H_

#include <memory>

#include <base/files/file_path.h>
#include <base/observer_list.h>
#include <brillo/brillo_export.h>
#include <spaced/proto_bindings/spaced.pb.h>

#include "spaced/dbus-proxies.h"
#include "spaced/disk_usage.h"

namespace spaced {

class BRILLO_EXPORT SpacedObserverInterface : public base::CheckedObserver {
 public:
  ~SpacedObserverInterface() override = default;

  virtual void OnStatefulDiskSpaceUpdate(
      const StatefulDiskSpaceUpdate& update) = 0;
};

class BRILLO_EXPORT DiskUsageProxy : public DiskUsageUtil {
 public:
  explicit DiskUsageProxy(const scoped_refptr<dbus::Bus>& bus);
  ~DiskUsageProxy() override = default;

  static std::unique_ptr<DiskUsageProxy> Generate();

  int64_t GetFreeDiskSpace(const base::FilePath& path) override;
  int64_t GetTotalDiskSpace(const base::FilePath& path) override;
  int64_t GetRootDeviceSize() override;

  void OnStatefulDiskSpaceUpdate(const spaced::StatefulDiskSpaceUpdate& space);

  void AddObserver(SpacedObserverInterface* observer);
  void RemoveObserver(SpacedObserverInterface* observer);

  void StartMonitoring();

 private:
  std::unique_ptr<org::chromium::SpacedProxy> spaced_proxy_;
  base::ObserverList<SpacedObserverInterface> observer_list_;
};

}  // namespace spaced

#endif  // SPACED_DISK_USAGE_PROXY_H_
