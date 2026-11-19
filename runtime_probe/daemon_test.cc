// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/daemon.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_probe/avl_probe_config_loader.h"
#include "runtime_probe/probe_config_loader.h"
#include "runtime_probe/proto_bindings/runtime_probe.pb.h"
#include "runtime_probe/ssfc_probe_config_loader.h"
#include "runtime_probe/system/context_mock_impl.h"

namespace runtime_probe {

namespace {

using ::brillo::dbus_utils::MockDBusMethodResponse;

using ::testing::NiceMock;
using ::testing::Return;

// Build a multi-category config large enough to exceed glibc's heap trim
// threshold when freed inside ProbeCallback, catching synchronous UAF on
// ComponentCategory::component_ iteration even without ASan.
std::string BuildLargeEdidConfig(int num_components) {
  std::string json = "{\n  \"display_panel\": {\n";
  for (int i = 0; i < num_components; ++i) {
    if (i > 0) {
      json += ",\n";
    }
    json += base::StringPrintf(
        "    \"panel_%04d\": { \"eval\": { \"edid\": {} }, "
        "\"expect\": { \"vendor\": [true, \"str\", \"!eq VENDOR_%04d\"] } }",
        i, i);
  }
  json += "\n  },\n  \"wireless\": {\n";
  for (int i = 0; i < 4; ++i) {
    if (i > 0) {
      json += ",\n";
    }
    json += base::StringPrintf(
        "    \"last_comp_%d\": { \"eval\": { \"edid\": {} } }", i);
  }
  json += "\n  }\n}\n";
  return json;
}

class DaemonTest : public ::testing::Test {
 protected:
  void WriteConfigFile(const base::FilePath& rel_path,
                       const std::string& content) {
    const base::FilePath full_path = mock_context_.root_dir().Append(rel_path);
    PCHECK(base::CreateDirectoryAndGetError(full_path.DirName(), nullptr));
    PCHECK(base::WriteFile(full_path, content));
  }

  Daemon daemon_;
  org::chromium::RuntimeProbeInterface* const dbus_adaptor_{&daemon_};
  ContextMockImpl mock_context_;
};

TEST_F(DaemonTest, ProbeCategories_LoadDefaultFailed) {
  ProbeRequest request;
  request.set_probe_default_category(true);
  std::optional<ProbeResult> reply;
  auto response =
      std::make_unique<MockDBusMethodResponse<ProbeResult>>(nullptr);
  response->save_return_args(&reply);
  dbus_adaptor_->ProbeCategories(std::move(response), request);
  EXPECT_TRUE(reply);
  EXPECT_EQ(reply->error(), RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
}

TEST_F(DaemonTest, ProbeCategories_DefaultCategoryLargeConfigSuccess) {
  WriteConfigFile(
      base::FilePath(kRuntimeProbeConfigDir).Append(kAvlProbeConfigName),
      BuildLargeEdidConfig(400));

  ProbeRequest request;
  request.set_probe_default_category(true);
  std::optional<ProbeResult> reply;
  auto response =
      std::make_unique<MockDBusMethodResponse<ProbeResult>>(nullptr);
  response->save_return_args(&reply);
  dbus_adaptor_->ProbeCategories(std::move(response), request);
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(reply);
  EXPECT_EQ(reply->error(), RUNTIME_PROBE_ERROR_NOT_SET);
}

TEST_F(DaemonTest, ProbeCategories_SpecificCategorySuccess) {
  WriteConfigFile(
      base::FilePath(kRuntimeProbeConfigDir).Append(kAvlProbeConfigName),
      BuildLargeEdidConfig(50));

  ProbeRequest request;
  request.set_probe_default_category(false);
  request.add_categories(ProbeRequest::display_panel);
  std::optional<ProbeResult> reply;
  auto response =
      std::make_unique<MockDBusMethodResponse<ProbeResult>>(nullptr);
  response->save_return_args(&reply);
  dbus_adaptor_->ProbeCategories(std::move(response), request);
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(reply);
  EXPECT_EQ(reply->error(), RUNTIME_PROBE_ERROR_NOT_SET);
}

TEST_F(DaemonTest, ProbeSsfcComponents_LoadDefaultFailed) {
  ProbeSsfcComponentsRequest request;
  std::optional<ProbeSsfcComponentsResponse> reply;
  auto response =
      std::make_unique<MockDBusMethodResponse<ProbeSsfcComponentsResponse>>(
          nullptr);
  response->save_return_args(&reply);
  dbus_adaptor_->ProbeSsfcComponents(std::move(response), request);
  EXPECT_TRUE(reply);
  EXPECT_EQ(reply->error(), RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
}

TEST_F(DaemonTest, ProbeSsfcComponents_Success) {
  constexpr char kModelName[] = "ModelFoo";
  mock_context_.fake_cros_config()->SetString(
      kCrosConfigModelNamePath, kCrosConfigModelNameKey, kModelName);
  WriteConfigFile(base::FilePath(kRuntimeProbeConfigDir)
                      .Append(kModelName)
                      .Append(kSsfcProbeConfigName),
                  BuildLargeEdidConfig(400));

  ProbeSsfcComponentsRequest request;
  std::optional<ProbeSsfcComponentsResponse> reply;
  auto response =
      std::make_unique<MockDBusMethodResponse<ProbeSsfcComponentsResponse>>(
          nullptr);
  response->save_return_args(&reply);
  dbus_adaptor_->ProbeSsfcComponents(std::move(response), request);
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(reply);
  EXPECT_EQ(reply->error(), RUNTIME_PROBE_ERROR_NOT_SET);
}

}  // namespace

}  // namespace runtime_probe
