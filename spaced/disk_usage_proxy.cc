// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spaced/disk_usage_proxy.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>

namespace spaced {
namespace {
void LogOnSignalConnected(const std::string& interface_name,
                          const std::string& signal_name,
                          bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to signal " << signal_name
               << " of interface " << interface_name;
  }
}

}  // namespace

DiskUsageProxy::DiskUsageProxy(
    std::unique_ptr<org::chromium::SpacedProxyInterface> spaced_proxy)
    : spaced_proxy_(std::move(spaced_proxy)) {}

std::unique_ptr<DiskUsageProxy> DiskUsageProxy::Generate() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(options);
  if (!bus->Connect()) {
    LOG(ERROR) << "D-Bus system bus is not ready";
    return nullptr;
  }

  return std::make_unique<DiskUsageProxy>(
      std::make_unique<org::chromium::SpacedProxy>(bus));
}

int64_t DiskUsageProxy::GetFreeDiskSpace(const base::FilePath& path) {
  int64_t free_disk_space;
  brillo::ErrorPtr error;
  // Return -1 if call fails.
  if (!spaced_proxy_->GetFreeDiskSpace(path.value(), &free_disk_space,
                                       &error)) {
    LOG(ERROR) << "Failed to call GetFreeDiskSpace, error: "
               << error->GetMessage();
    return -1;
  }

  return free_disk_space;
}

void DiskUsageProxy::GetFreeDiskSpaceAsync(
    const base::FilePath& path, base::OnceCallback<void(int64_t)> callback) {
  auto splitted_callbacks = base::SplitOnceCallback(std::move(callback));
  spaced_proxy_->GetFreeDiskSpaceAsync(
      path.value(), std::move(splitted_callbacks.first),
      base::BindOnce(
          [](base::OnceCallback<void(int64_t)> callback, brillo::Error* error) {
            LOG(ERROR) << "Failed to GetFreeDiskSpaceAsync: "
                       << error->GetMessage();
            std::move(callback).Run(-1);
          },
          std::move(splitted_callbacks.second)));
}

int64_t DiskUsageProxy::GetTotalDiskSpace(const base::FilePath& path) {
  int64_t total_disk_space;
  brillo::ErrorPtr error;
  // Return -1 if call fails.
  if (!spaced_proxy_->GetTotalDiskSpace(path.value(), &total_disk_space,
                                        &error)) {
    LOG(ERROR) << "Failed to call GetTotalDiskSpace, error: "
               << error->GetMessage();
    return -1;
  }

  return total_disk_space;
}

int64_t DiskUsageProxy::GetRootDeviceSize() {
  int64_t root_device_size;

  brillo::ErrorPtr error;
  // Return -1 if call fails.
  if (!spaced_proxy_->GetRootDeviceSize(&root_device_size, &error)) {
    LOG(ERROR) << "Failed to call GetRootDeviceSize, error: "
               << error->GetMessage();
    return -1;
  }

  return root_device_size;
}

bool DiskUsageProxy::IsQuotaSupported(const base::FilePath& path) {
  bool is_supported = false;
  brillo::ErrorPtr error;
  // Return false if call fails.
  if (!spaced_proxy_->IsQuotaSupported(path.value(), &is_supported, &error)) {
    LOG(ERROR) << "Failed to call IsQuotaSupported, error: "
               << error->GetMessage();
    return false;
  }
  return is_supported;
}

int64_t DiskUsageProxy::GetQuotaCurrentSpaceForUid(const base::FilePath& path,
                                                   uint32_t uid) {
  int64_t current_space = 0;
  brillo::ErrorPtr error;
  // Return -1 if call fails.
  if (!spaced_proxy_->GetQuotaCurrentSpaceForUid(path.value(), uid,
                                                 &current_space, &error)) {
    LOG(ERROR) << "Failed to call GetQuotaCurrentSpaceForUid, error: "
               << error->GetMessage();
    return -1;
  }
  return current_space;
}

int64_t DiskUsageProxy::GetQuotaCurrentSpaceForGid(const base::FilePath& path,
                                                   uint32_t gid) {
  int64_t current_space = 0;
  brillo::ErrorPtr error;
  // Return -1 if call fails.
  if (!spaced_proxy_->GetQuotaCurrentSpaceForGid(path.value(), gid,
                                                 &current_space, &error)) {
    LOG(ERROR) << "Failed to call GetQuotaCurrentSpaceForGid, error: "
               << error->GetMessage();
    return -1;
  }
  return current_space;
}

int64_t DiskUsageProxy::GetQuotaCurrentSpaceForProjectId(
    const base::FilePath& path, uint32_t project_id) {
  int64_t current_space = 0;
  brillo::ErrorPtr error;
  // Return -1 if call fails.
  if (!spaced_proxy_->GetQuotaCurrentSpaceForGid(path.value(), project_id,
                                                 &current_space, &error)) {
    LOG(ERROR) << "Failed to call GetQuotaCurrentSpaceForProjectId, error: "
               << error->GetMessage();
    return -1;
  }
  return current_space;
}

bool DiskUsageProxy::SetProjectId(const base::ScopedFD& fd,
                                  uint32_t project_id,
                                  int* out_error) {
  SetProjectIdReply result;
  brillo::ErrorPtr error;
  // Return false if call fails.
  if (!spaced_proxy_->SetProjectId(fd, project_id, &result, &error)) {
    *out_error = result.error();
    LOG(ERROR) << "Failed to call SetProjectId, error: " << error->GetMessage();
    return false;
  }
  return result.success();
}

bool DiskUsageProxy::SetProjectInheritanceFlag(const base::ScopedFD& fd,
                                               bool enable,
                                               int* out_error) {
  SetProjectInheritanceFlagReply result;
  brillo::ErrorPtr error;
  // Return false if call fails.
  if (!spaced_proxy_->SetProjectInheritanceFlag(fd, enable, &result, &error)) {
    *out_error = result.error();
    LOG(ERROR) << "Failed to call SetProjectInheritanceFlag, error: "
               << error->GetMessage();
    return false;
  }
  return result.success();
}

void DiskUsageProxy::OnStatefulDiskSpaceUpdate(
    const StatefulDiskSpaceUpdate& update) {
  for (SpacedObserverInterface& observer : observer_list_) {
    observer.OnStatefulDiskSpaceUpdate(update);
  }
}

void DiskUsageProxy::AddObserver(SpacedObserverInterface* observer) {
  CHECK(observer) << "Invalid observer";
  observer_list_.AddObserver(observer);
}

void DiskUsageProxy::RemoveObserver(SpacedObserverInterface* observer) {
  CHECK(observer) << "Invalid observer";
  observer_list_.RemoveObserver(observer);
}

void DiskUsageProxy::StartMonitoring() {
  spaced_proxy_->RegisterStatefulDiskSpaceUpdateSignalHandler(
      base::BindRepeating(&DiskUsageProxy::OnStatefulDiskSpaceUpdate,
                          base::Unretained(this)),
      base::BindOnce(LogOnSignalConnected));
}

}  // namespace spaced
