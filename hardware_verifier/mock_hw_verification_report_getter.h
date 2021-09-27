/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef HARDWARE_VERIFIER_MOCK_HW_VERIFICATION_REPORT_GETTER_H_
#define HARDWARE_VERIFIER_MOCK_HW_VERIFICATION_REPORT_GETTER_H_

#include "hardware_verifier/hw_verification_report_getter.h"

#include "gmock/gmock.h"

namespace hardware_verifier {

class MockHwVerificationReportGetter : public HwVerificationReportGetter {
 public:
  MOCK_METHOD(base::Optional<HwVerificationReport>,
              Get,
              (const base::StringPiece& probe_result_file,
               const base::StringPiece& hw_verification_spec_file,
               ErrorCode* error_code),
              (const, override));
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_MOCK_HW_VERIFICATION_REPORT_GETTER_H_
