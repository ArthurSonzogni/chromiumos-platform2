// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/stateful_partition_fetcher.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <brillo/errors/error.h>
#include <libmount/libmount.h>
#include <spaced/proto_bindings/spaced.pb.h>
// NOLINTNEXTLINE(build/include_alpha) dbus-proxies.h needs spaced.pb.h
#include <spaced/dbus-proxies.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

class State {
 public:
  State() = default;
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  ~State() = default;

  // Handle the response of available space of stateful partition.
  void HandleFreeDiskSpaceResponse(brillo::Error* err, int64_t free_space_byte);

  // Handle the response of total space of stateful partition.
  void HandleTotalDiskSpaceResponse(brillo::Error* err,
                                    int64_t total_space_byte);

  // Handle the response from spaced and collect info from mtab.
  void GetMtabInfo(FetchStatefulPartitionInfoCallback callback,
                   const base::FilePath& root_dir,
                   bool is_finished);

 private:
  // The info to be returned.
  mojom::StatefulPartitionInfoPtr info_ = mojom::StatefulPartitionInfo::New();
  // The error to be returned.
  mojom::ProbeErrorPtr error_;
};

void State::HandleFreeDiskSpaceResponse(brillo::Error* err,
                                        int64_t free_space_byte) {
  if (err) {
    error_ = CreateAndLogProbeError(
        mojom::ErrorType::kSystemUtilityError,
        "Failed to collect available space of stateful partition");
    return;
  }
  info_->available_space = free_space_byte;
}

void State::HandleTotalDiskSpaceResponse(brillo::Error* err,
                                         int64_t total_space_byte) {
  if (err) {
    error_ = CreateAndLogProbeError(
        mojom::ErrorType::kSystemUtilityError,
        "Failed to collect total space of stateful partition");
    return;
  }
  info_->total_space = total_space_byte;
}

void State::GetMtabInfo(FetchStatefulPartitionInfoCallback callback,
                        const base::FilePath& root_dir,
                        bool is_finished) {
  if (!is_finished) {
    std::move(callback).Run(
        mojom::StatefulPartitionResult::NewError(CreateAndLogProbeError(
            mojom::ErrorType::kSystemUtilityError,
            "Failed to collect stateful partition info from spaced")));
    return;
  }

  if (!error_.is_null()) {
    std::move(callback).Run(
        mojom::StatefulPartitionResult::NewError(std::move(error_)));
    return;
  }

  const auto mtab = mnt_new_table();
  const auto mtab_path = root_dir.Append(kMtabPath);
  mnt_table_parse_mtab(mtab, mtab_path.value().c_str());

  const auto stateful_partition_path = root_dir.Append(kStatefulPartitionPath);
  const auto fs = mnt_table_find_target(
      mtab, stateful_partition_path.value().c_str(), MNT_ITER_FORWARD);

  if (!fs || !mnt_fs_get_fstype(fs) || !mnt_fs_get_source(fs)) {
    mnt_free_table(mtab);
    std::move(callback).Run(
        mojom::StatefulPartitionResult::NewError(CreateAndLogProbeError(
            mojom::ErrorType::kSystemUtilityError,
            "Failed to collect stateful partition info from mtab")));
    return;
  }

  info_->filesystem = mnt_fs_get_fstype(fs);
  info_->mount_source = mnt_fs_get_source(fs);
  mnt_free_table(mtab);

  std::move(callback).Run(
      mojom::StatefulPartitionResult::NewPartitionInfo(std::move(info_)));
}

}  // namespace

void FetchStatefulPartitionInfo(Context* context,
                                FetchStatefulPartitionInfoCallback callback) {
  const auto& root_dir = GetRootDir();
  auto state = std::make_unique<State>();
  State* state_ptr = state.get();
  CallbackBarrier barrier{base::BindOnce(&State::GetMtabInfo, std::move(state),
                                         std::move(callback), root_dir)};

  const auto stateful_partition_path = root_dir.Append(kStatefulPartitionPath);

  auto [free_space_success_cb, free_space_error_cb] =
      SplitDbusCallback(barrier.Depend(base::BindOnce(
          &State::HandleFreeDiskSpaceResponse, base::Unretained(state_ptr))));
  context->spaced_proxy()->GetFreeDiskSpaceAsync(
      stateful_partition_path.value(), std::move(free_space_success_cb),
      std::move(free_space_error_cb));

  auto [total_space_success_cb, total_space_error_cb] =
      SplitDbusCallback(barrier.Depend(base::BindOnce(
          &State::HandleTotalDiskSpaceResponse, base::Unretained(state_ptr))));
  context->spaced_proxy()->GetTotalDiskSpaceAsync(
      stateful_partition_path.value(), std::move(total_space_success_cb),
      std::move(total_space_error_cb));
}

}  // namespace diagnostics
