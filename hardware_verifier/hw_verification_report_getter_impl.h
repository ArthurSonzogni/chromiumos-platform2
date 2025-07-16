/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef HARDWARE_VERIFIER_HW_VERIFICATION_REPORT_GETTER_IMPL_H_
#define HARDWARE_VERIFIER_HW_VERIFICATION_REPORT_GETTER_IMPL_H_

#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "hardware_verifier/hardware_verifier.pb.h"
#include "hardware_verifier/hw_verification_report_getter.h"
#include "hardware_verifier/hw_verification_spec_getter.h"
#include "hardware_verifier/probe_result_getter.h"
#include "hardware_verifier/runtime_hwid_generator.h"
#include "hardware_verifier/verifier.h"

namespace hardware_verifier {

// The actual implementation to the |HwVerificationReportGetter|.
class HwVerificationReportGetterImpl : public HwVerificationReportGetter {
 public:
  HwVerificationReportGetterImpl();
  HwVerificationReportGetterImpl(const HwVerificationReportGetterImpl&) =
      delete;
  HwVerificationReportGetterImpl& operator=(
      const HwVerificationReportGetterImpl&) = delete;

  std::optional<HwVerificationReport> Get(
      std::string_view probe_result_file,
      std::string_view hw_verification_spec_file,
      ErrorCode* out_error_code,
      RuntimeHWIDRefreshPolicy refresh_runtime_hwid_policy =
          RuntimeHWIDRefreshPolicy::kSkip) const override;

 protected:
  // This constructor is reserved only for testing.
  explicit HwVerificationReportGetterImpl(
      std::unique_ptr<ProbeResultGetter> pr_getter,
      std::unique_ptr<HwVerificationSpecGetter> vs_getter,
      std::unique_ptr<Verifier> verifier,
      std::unique_ptr<RuntimeHWIDGenerator> runtime_hwid_generator)
      : pr_getter_(std::move(pr_getter)),
        vs_getter_(std::move(vs_getter)),
        verifier_(std::move(verifier)),
        runtime_hwid_generator_(std::move(runtime_hwid_generator)) {}

 private:
  std::unique_ptr<ProbeResultGetter> pr_getter_;
  std::unique_ptr<HwVerificationSpecGetter> vs_getter_;
  std::unique_ptr<Verifier> verifier_;
  std::unique_ptr<RuntimeHWIDGenerator> runtime_hwid_generator_;

  void RefreshRuntimeHWID(RuntimeHWIDRefreshPolicy refresh_runtime_hwid_policy,
                          const runtime_probe::ProbeResult& probe_result) const;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_HW_VERIFICATION_REPORT_GETTER_IMPL_H_
