// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPACED_DISK_USAGE_PROXY_H_
#define SPACED_DISK_USAGE_PROXY_H_

#include <memory>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
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

// Spaced returns negative value on internal errors. This is a wrapper of
// org::chromium::SpacedProxy converting DBus errors into negative value
// response and provides an unified interface.
class BRILLO_EXPORT DiskUsageProxy : public DiskUsageUtil {
 public:
  explicit DiskUsageProxy(
      std::unique_ptr<org::chromium::SpacedProxyInterface> spaced_proxy);
  ~DiskUsageProxy() override = default;

  static std::unique_ptr<DiskUsageProxy> Generate();

  int64_t GetFreeDiskSpace(const base::FilePath& path) override;
  void GetFreeDiskSpaceAsync(const base::FilePath& path,
                             base::OnceCallback<void(int64_t)> callback);
  int64_t GetTotalDiskSpace(const base::FilePath& path) override;
  int64_t GetRootDeviceSize() override;
  bool IsQuotaSupported(const base::FilePath& path) override;
  int64_t GetQuotaCurrentSpaceForUid(const base::FilePath& path,
                                     uint32_t uid) override;
  int64_t GetQuotaCurrentSpaceForGid(const base::FilePath& path,
                                     uint32_t gid) override;
  int64_t GetQuotaCurrentSpaceForProjectId(const base::FilePath& path,
                                           uint32_t project_id) override;
  bool SetProjectId(const base::ScopedFD& fd,
                    uint32_t project_id,
                    int* out_error) override;
  bool SetProjectInheritanceFlag(const base::ScopedFD& fd,
                                 bool enable,
                                 int* out_error) override;

  void OnStatefulDiskSpaceUpdate(const spaced::StatefulDiskSpaceUpdate& space);

  void AddObserver(SpacedObserverInterface* observer);
  void RemoveObserver(SpacedObserverInterface* observer);

  void StartMonitoring();

 private:
  std::unique_ptr<org::chromium::SpacedProxyInterface> spaced_proxy_;
  base::ObserverList<SpacedObserverInterface> observer_list_;
};

}  // namespace spaced

#endif  // SPACED_DISK_USAGE_PROXY_H_
