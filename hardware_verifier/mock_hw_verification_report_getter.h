/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef HARDWARE_VERIFIER_MOCK_HW_VERIFICATION_REPORT_GETTER_H_
#define HARDWARE_VERIFIER_MOCK_HW_VERIFICATION_REPORT_GETTER_H_

#include <optional>

#include <gmock/gmock.h>

#include "hardware_verifier/hw_verification_report_getter.h"

namespace hardware_verifier {

class MockHwVerificationReportGetter : public HwVerificationReportGetter {
 public:
  MOCK_METHOD(std::optional<HwVerificationReport>,
              Get,
              (std::string_view probe_result_file,
               std::string_view hw_verification_spec_file,
               ErrorCode* error_code,
               RuntimeHWIDRefreshPolicy refresh_runtime_hwid_policy),
              (const, override));
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_MOCK_HW_VERIFICATION_REPORT_GETTER_H_
