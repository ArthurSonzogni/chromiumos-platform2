// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/storage/ufs_lifetime.h"

#include <glob.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "diagnostics/base/file_utils.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// "01h" means normal device life time, which is defined in UFS spec.
inline constexpr uint8_t kUfsHealthDescPreEolInfoNormal = 0x01;
inline constexpr char kBsgNodePathPattern[] = "sys/devices/*/*/host*/ufs-bsg*";
inline constexpr char kBsgNodePathPatternArm[] =
    "sys/devices/platform/soc/*/host*/ufs-bsg*";
inline constexpr char kBsgNodeToHealthDesc[] = "../../health_descriptor/";

const std::optional<base::FilePath> Glob(const base::FilePath& pattern) {
  std::optional<base::FilePath> path;
  glob_t glob_result;
  int return_code = glob(pattern.value().c_str(), GLOB_ONLYDIR,
                         /*errfunc=*/nullptr, &glob_result);
  if (return_code == EXIT_SUCCESS && glob_result.gl_pathc == 1) {
    path = base::FilePath(glob_result.gl_pathv[0]);
  } else if (glob_result.gl_pathc != 1) {
    // This also handles the case where `return_code == GLOB_NOMATCH`.
    LOG(WARNING) << "Number of path matched by glob pattern: "
                 << pattern.value()
                 << " should be exactly 1, got: " << glob_result.gl_pathc;
  } else {
    LOG(ERROR) << "Unexpected error from glob pattern: " << pattern.value()
               << "; return code: " << return_code;
  }
  globfree(&glob_result);
  return path;
}

const std::optional<base::FilePath> GetBsgNodePath() {
  std::optional<base::FilePath> node_path =
      Glob(base::FilePath(GetRootDir().AppendASCII(kBsgNodePathPattern)));
  if (!node_path.has_value()) {
    // Fallback for ARM devices.
    node_path =
        Glob(base::FilePath(GetRootDir().AppendASCII(kBsgNodePathPatternArm)));
  }
  return node_path;
}

}  // namespace

UfsLifetimeRoutine::UfsLifetimeRoutine(
    const mojom::UfsLifetimeRoutineArgumentPtr& arg) {}

UfsLifetimeRoutine::~UfsLifetimeRoutine() = default;

void UfsLifetimeRoutine::OnStart() {
  SetRunningState();

  std::optional<base::FilePath> bsg_node_path = GetBsgNodePath();
  if (!bsg_node_path.has_value()) {
    RaiseException("Unable to determine a bsg node path");
    return;
  }

  // The bsg node path looks like "/sys/devices/.../hostx/ufs-bsgx".
  // Navigate to "/sys/devices/.../health_descriptor", where the health
  // descriptor is.
  const base::FilePath health_desc_path = base::MakeAbsoluteFilePath(
      bsg_node_path.value().Append(kBsgNodeToHealthDesc));
  if (health_desc_path.empty()) {
    RaiseException(
        "Unable to deduce health descriptor path based on the bsg node path");
    return;
  }
  SetPercentage(50);

  uint32_t pre_eol_info;
  uint32_t device_life_time_est_a;
  uint32_t device_life_time_est_b;
  if (!ReadInteger(health_desc_path, kUfsHealthDescPreEolInfo,
                   &base::HexStringToUInt, &pre_eol_info) ||
      !ReadInteger(health_desc_path, kUfsHealthDescDeviceLifeTimeEstA,
                   &base::HexStringToUInt, &device_life_time_est_a) ||
      !ReadInteger(health_desc_path, kUfsHealthDescDeviceLifeTimeEstB,
                   &base::HexStringToUInt, &device_life_time_est_b)) {
    RaiseException("Error reading content from UFS health descriptor");
    return;
  }

  bool has_passed = pre_eol_info == kUfsHealthDescPreEolInfoNormal;
  auto detail = mojom::UfsLifetimeRoutineDetail::New();
  detail->pre_eol_info = pre_eol_info;
  detail->device_life_time_est_a = device_life_time_est_a;
  detail->device_life_time_est_b = device_life_time_est_b;
  SetFinishedState(has_passed,
                   mojom::RoutineDetail::NewUfsLifetime(std::move(detail)));
}

}  // namespace diagnostics
