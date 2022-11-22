// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SMARTCTL_CHECK_SMARTCTL_CHECK_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SMARTCTL_CHECK_SMARTCTL_CHECK_H_

#include <cstdint>
#include <string>

#include <base/values.h>
#include <brillo/errors/error.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace org {
namespace chromium {
class debugdProxyInterface;
}  // namespace chromium
}  // namespace org

namespace diagnostics {

inline constexpr char kSmartctlCheckRoutineSuccess[] =
    "smartctl-check status: PASS.";
inline constexpr char kSmartctlCheckRoutineParseError[] =
    "smartctl-check status: ERROR, unable to parse smartctl output.";
inline constexpr char kSmartctlCheckRoutineDebugdError[] =
    "smartctl-check status: ERROR, debugd returns error.";
inline constexpr char kSmartctlCheckRoutineFailedAvailableSpare[] =
    "smartctl-check status: FAILED, available_spare is less than "
    "available_spare_threshold.";

// The SmartctlCheckRoutine routine to examine available_spare against
// available_spare_threshold.
class SmartctlCheckRoutine final : public DiagnosticRoutine {
 public:
  SmartctlCheckRoutine(org::chromium::debugdProxyInterface* debugd_proxy);
  SmartctlCheckRoutine(const SmartctlCheckRoutine&) = delete;
  SmartctlCheckRoutine& operator=(const SmartctlCheckRoutine&) = delete;
  ~SmartctlCheckRoutine() override;

  // DiagnosticRoutine overrides:
  void Start() override;
  void Resume() override;
  void Cancel() override;
  void PopulateStatusUpdate(ash::cros_healthd::mojom::RoutineUpdate* response,
                            bool include_output) override;
  ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum GetStatus() override;

 private:
  void OnDebugdResultCallback(const std::string& result);
  void OnDebugdErrorCallback(brillo::Error* error);
  // Updates status_, percent_, status_message_ at the same moment to ensure
  // each of them corresponds with the others.
  void UpdateStatus(
      ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum status,
      uint32_t percent,
      std::string msg);

  org::chromium::debugdProxyInterface* const debugd_proxy_;

  ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_ =
      ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kReady;
  uint32_t percent_ = 0;
  base::Value output_dict_{base::Value::Type::DICTIONARY};
  std::string status_message_;

  base::WeakPtrFactory<SmartctlCheckRoutine> weak_ptr_routine_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SMARTCTL_CHECK_SMARTCTL_CHECK_H_
