/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hardware_verifier/hw_verification_report_getter_impl.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <gmock/gmock.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "base/files/file_util.h"
#include "hardware_verifier/hw_verification_report_getter.h"
#include "hardware_verifier/hw_verification_spec_getter.h"
#include "hardware_verifier/observer.h"
#include "hardware_verifier/probe_result_getter.h"
#include "hardware_verifier/runtime_hwid_generator.h"
#include "hardware_verifier/test_utils.h"
#include "hardware_verifier/verifier.h"

namespace hardware_verifier {

namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

using ReportGetterErrorCode = HwVerificationReportGetter::ErrorCode;
using RuntimeHWIDRefreshPolicy =
    HwVerificationReportGetter::RuntimeHWIDRefreshPolicy;

class MockProbeResultGetter : public ProbeResultGetter {
 public:
  MOCK_METHOD(std::optional<runtime_probe::ProbeResult>,
              GetFromRuntimeProbe,
              (),
              (const, override));
  MOCK_METHOD(std::optional<runtime_probe::ProbeResult>,
              GetFromFile,
              (const base::FilePath& file_path),
              (const, override));
};

class MockHwVerificationSpecGetter : public HwVerificationSpecGetter {
 public:
  MOCK_METHOD(std::optional<HwVerificationSpec>,
              GetDefault,
              (),
              (const, override));
  MOCK_METHOD(std::optional<HwVerificationSpec>,
              GetFromFile,
              (const base::FilePath& file_path),
              (const, override));
};

class MockVerifier : public Verifier {
 public:
  MOCK_METHOD(std::optional<HwVerificationReport>,
              Verify,
              (const runtime_probe::ProbeResult& probe_result,
               const HwVerificationSpec& hw_verification_spec),
              (const, override));
};

class MockRuntimeHWIDGenerator : public RuntimeHWIDGenerator {
 public:
  MOCK_METHOD(bool,
              ShouldGenerateRuntimeHWID,
              (const runtime_probe::ProbeResult&),
              (const, override));
  MOCK_METHOD(std::optional<std::string>,
              Generate,
              (const runtime_probe::ProbeResult&),
              (const, override));
  MOCK_METHOD(bool,
              GenerateToDevice,
              (const runtime_probe::ProbeResult&),
              (const, override));
};

class HwVerificationReportGetterImplForTesting
    : public HwVerificationReportGetterImpl {
 public:
  explicit HwVerificationReportGetterImplForTesting(
      std::unique_ptr<ProbeResultGetter> pr_getter,
      std::unique_ptr<HwVerificationSpecGetter> vs_getter,
      std::unique_ptr<Verifier> verifier,
      std::unique_ptr<RuntimeHWIDGenerator> runtime_hwid_generator)
      : HwVerificationReportGetterImpl(std::move(pr_getter),
                                       std::move(vs_getter),
                                       std::move(verifier),
                                       std::move(runtime_hwid_generator)) {}
};

class HwVerificationReportGetterImplTest : public BaseFileTest {
 protected:
  void SetUp() override {
    auto mock_pr_getter = std::make_unique<NiceMock<MockProbeResultGetter>>();
    auto mock_vs_getter =
        std::make_unique<NiceMock<MockHwVerificationSpecGetter>>();
    auto mock_verifier = std::make_unique<NiceMock<MockVerifier>>();
    auto mock_runtime_hwid_generator =
        std::make_unique<NiceMock<MockRuntimeHWIDGenerator>>();
    auto mock_metrics = std::make_unique<NiceMock<MetricsLibraryMock>>();
    mock_pr_getter_ = mock_pr_getter.get();
    mock_vs_getter_ = mock_vs_getter.get();
    mock_verifier_ = mock_verifier.get();
    mock_runtime_hwid_generator_ = mock_runtime_hwid_generator.get();
    mock_metrics_ = mock_metrics.get();

    vr_getter_ = std::make_unique<HwVerificationReportGetterImplForTesting>(
        std::move(mock_pr_getter), std::move(mock_vs_getter),
        std::move(mock_verifier), std::move(mock_runtime_hwid_generator));
    Observer::GetInstance()->SetMetricsLibrary(std::move(mock_metrics));

    // set everything works by default.
    HwVerificationReport positive_report;
    positive_report.set_is_compliant(true);
    ON_CALL(*mock_pr_getter_, GetFromRuntimeProbe())
        .WillByDefault(Return(runtime_probe::ProbeResult()));
    ON_CALL(*mock_vs_getter_, GetDefault())
        .WillByDefault(Return(HwVerificationSpec()));
    ON_CALL(*mock_verifier_, Verify(_, _))
        .WillByDefault(Return(positive_report));
  }

  void TearDown() override {
    // We have to clear the MetricsLibraryMock manually, because
    // Observer::GetInstance() object is a singleton, which won't be destroyed
    // across the tests.
    Observer::GetInstance()->SetMetricsLibrary(nullptr);
  }

  std::unique_ptr<HwVerificationReportGetterImplForTesting> vr_getter_;
  MockProbeResultGetter* mock_pr_getter_;
  MockHwVerificationSpecGetter* mock_vs_getter_;
  MockVerifier* mock_verifier_;
  MockRuntimeHWIDGenerator* mock_runtime_hwid_generator_;
  MetricsLibraryMock* mock_metrics_;
};

TEST_F(HwVerificationReportGetterImplTest, TestBasicFlow) {
  HwVerificationReport vr;
  vr.set_is_compliant(true);
  ON_CALL(*mock_verifier_, Verify(_, _)).WillByDefault(Return(vr));
  ReportGetterErrorCode error_code;

  EXPECT_CALL(*mock_metrics_, SendToUMA(_, _, _, _, _)).Times(1);
  auto result = vr_getter_->Get("", "", &error_code);
  EXPECT_TRUE(result);
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(*result, vr));
  EXPECT_EQ(error_code, ReportGetterErrorCode::kErrorCodeNoError);

  EXPECT_CALL(*mock_metrics_, SendToUMA(_, _, _, _, _)).Times(1);
  result = vr_getter_->Get("", "", nullptr);
  EXPECT_TRUE(result);
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(*result, vr));
}

TEST_F(HwVerificationReportGetterImplTest, TestHandleWaysToGetProbeResults) {
  ReportGetterErrorCode error_code;

  ON_CALL(*mock_pr_getter_, GetFromRuntimeProbe())
      .WillByDefault(Return(std::nullopt));
  EXPECT_CALL(*mock_metrics_, SendToUMA(_, _, _, _, _)).Times(1);
  EXPECT_FALSE(vr_getter_->Get("", "", &error_code));
  EXPECT_EQ(error_code, ReportGetterErrorCode::kErrorCodeProbeFail);

  ON_CALL(*mock_pr_getter_, GetFromFile(_))
      .WillByDefault(Return(runtime_probe::ProbeResult()));
  EXPECT_CALL(*mock_metrics_, SendToUMA(_, _, _, _, _)).Times(0);
  EXPECT_TRUE(vr_getter_->Get("path", "", &error_code));
  EXPECT_EQ(error_code, ReportGetterErrorCode::kErrorCodeNoError);

  ON_CALL(*mock_pr_getter_, GetFromFile(_)).WillByDefault(Return(std::nullopt));
  EXPECT_CALL(*mock_metrics_, SendToUMA(_, _, _, _, _)).Times(0);
  EXPECT_FALSE(vr_getter_->Get("path2", "", &error_code));
  EXPECT_EQ(error_code,
            ReportGetterErrorCode::kErrorCodeInvalidProbeResultFile);
}

TEST_F(HwVerificationReportGetterImplTest,
       TestHandleWaysToGetHwVerificationSpec) {
  ReportGetterErrorCode error_code;

  ON_CALL(*mock_vs_getter_, GetDefault()).WillByDefault(Return(std::nullopt));
  EXPECT_FALSE(vr_getter_->Get("", "", &error_code));
  EXPECT_EQ(
      error_code,
      ReportGetterErrorCode::kErrorCodeMissingDefaultHwVerificationSpecFile);

  ON_CALL(*mock_vs_getter_, GetFromFile(_))
      .WillByDefault(Return(HwVerificationSpec()));
  EXPECT_TRUE(vr_getter_->Get("", "path", &error_code));
  EXPECT_EQ(error_code, ReportGetterErrorCode::kErrorCodeNoError);

  ON_CALL(*mock_vs_getter_, GetFromFile(_)).WillByDefault(Return(std::nullopt));
  EXPECT_FALSE(vr_getter_->Get("", "path2", &error_code));
  EXPECT_EQ(error_code,
            ReportGetterErrorCode::kErrorCodeInvalidHwVerificationSpecFile);
}

TEST_F(HwVerificationReportGetterImplTest, TestVerifyFail) {
  ReportGetterErrorCode error_code;

  ON_CALL(*mock_verifier_, Verify(_, _)).WillByDefault(Return(std::nullopt));
  EXPECT_FALSE(vr_getter_->Get("", "", &error_code));
  EXPECT_EQ(error_code,
            ReportGetterErrorCode::
                kErrorCodeProbeResultHwVerificationSpecMisalignment);

  HwVerificationReport positive_report;
  positive_report.set_is_compliant(true);
  ON_CALL(*mock_verifier_, Verify(_, _)).WillByDefault(Return(positive_report));
  auto result = vr_getter_->Get("", "", &error_code);
  EXPECT_TRUE(result);
  EXPECT_EQ(error_code, ReportGetterErrorCode::kErrorCodeNoError);
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      *result, positive_report));

  HwVerificationReport negative_report;
  negative_report.set_is_compliant(false);
  ON_CALL(*mock_verifier_, Verify(_, _)).WillByDefault(Return(negative_report));
  result = vr_getter_->Get("", "", &error_code);
  EXPECT_TRUE(result);
  EXPECT_EQ(error_code, ReportGetterErrorCode::kErrorCodeNoError);
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      *result, negative_report));
}

class HwVerificationReportGetterImplRuntimeHWIDTest
    : public HwVerificationReportGetterImplTest {
 protected:
  void SetUp() override {
    HwVerificationReportGetterImplTest::SetUp();
    verification_report_.set_is_compliant(true);
    ON_CALL(*mock_verifier_, Verify(_, _))
        .WillByDefault(Return(verification_report_));
    ON_CALL(*mock_runtime_hwid_generator_, GenerateToDevice)
        .WillByDefault(Return(true));
  }

  HwVerificationReport verification_report_;
};

TEST_F(HwVerificationReportGetterImplRuntimeHWIDTest, TestDefaultPolicy) {
  ReportGetterErrorCode error_code;
  EXPECT_CALL(*mock_runtime_hwid_generator_, ShouldGenerateRuntimeHWID)
      .Times(0);
  EXPECT_CALL(*mock_runtime_hwid_generator_, GenerateToDevice).Times(0);
  SetFile(kRuntimeHWIDFilePath, "fake-file");

  auto result = vr_getter_->Get("", "", &error_code);

  EXPECT_TRUE(result);
  EXPECT_EQ(error_code, ReportGetterErrorCode::kErrorCodeNoError);
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      *result, verification_report_));

  std::string file_content;
  const auto runtime_hwid_path = GetPathUnderRoot(kRuntimeHWIDFilePath);
  EXPECT_TRUE(base::ReadFileToString(runtime_hwid_path, &file_content));
  EXPECT_EQ(file_content, "fake-file");
}

TEST_F(HwVerificationReportGetterImplRuntimeHWIDTest, TestSkipPolicy) {
  ReportGetterErrorCode error_code;
  EXPECT_CALL(*mock_runtime_hwid_generator_, ShouldGenerateRuntimeHWID)
      .Times(0);
  EXPECT_CALL(*mock_runtime_hwid_generator_, GenerateToDevice).Times(0);
  SetFile(kRuntimeHWIDFilePath, "fake-file");

  auto result =
      vr_getter_->Get("", "", &error_code, RuntimeHWIDRefreshPolicy::kSkip);

  EXPECT_TRUE(result);
  EXPECT_EQ(error_code, ReportGetterErrorCode::kErrorCodeNoError);

  std::string file_content;
  const auto runtime_hwid_path = GetPathUnderRoot(kRuntimeHWIDFilePath);
  EXPECT_TRUE(base::ReadFileToString(runtime_hwid_path, &file_content));
  EXPECT_EQ(file_content, "fake-file");
}

TEST_F(HwVerificationReportGetterImplRuntimeHWIDTest,
       TestRefreshPolicy_ShouldGenerate) {
  ReportGetterErrorCode error_code;
  EXPECT_CALL(*mock_runtime_hwid_generator_, ShouldGenerateRuntimeHWID)
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_runtime_hwid_generator_, GenerateToDevice).Times(1);

  auto result =
      vr_getter_->Get("", "", &error_code, RuntimeHWIDRefreshPolicy::kRefresh);

  EXPECT_TRUE(result);
  EXPECT_EQ(error_code, ReportGetterErrorCode::kErrorCodeNoError);
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      *result, verification_report_));
}

TEST_F(HwVerificationReportGetterImplRuntimeHWIDTest,
       TestRefreshPolicy_ShouldNotGenerate) {
  ReportGetterErrorCode error_code;
  EXPECT_CALL(*mock_runtime_hwid_generator_, ShouldGenerateRuntimeHWID)
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_runtime_hwid_generator_, GenerateToDevice).Times(0);
  SetFile(kRuntimeHWIDFilePath, "fake-file");

  auto result =
      vr_getter_->Get("", "", &error_code, RuntimeHWIDRefreshPolicy::kRefresh);

  EXPECT_TRUE(result);
  EXPECT_EQ(error_code, ReportGetterErrorCode::kErrorCodeNoError);
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      *result, verification_report_));
  const auto runtime_hwid_path = GetPathUnderRoot(kRuntimeHWIDFilePath);
  EXPECT_FALSE(base::PathExists(runtime_hwid_path));
}

TEST_F(HwVerificationReportGetterImplRuntimeHWIDTest, TestForceGeneratePolicy) {
  ReportGetterErrorCode error_code;
  EXPECT_CALL(*mock_runtime_hwid_generator_, ShouldGenerateRuntimeHWID)
      .Times(0);
  EXPECT_CALL(*mock_runtime_hwid_generator_, GenerateToDevice).Times(1);

  auto result = vr_getter_->Get("", "", &error_code,
                                RuntimeHWIDRefreshPolicy::kForceGenerate);

  EXPECT_TRUE(result);
  EXPECT_EQ(error_code, ReportGetterErrorCode::kErrorCodeNoError);
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      *result, verification_report_));
}

}  // namespace

}  // namespace hardware_verifier
