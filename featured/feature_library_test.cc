// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <memory>
#include <utility>

#include <base/dcheck_is_on.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_runner.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "featured/feature_library.h"
#include "featured/service.h"

namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

}  // namespace

namespace feature {

class FeatureLibraryTest : public testing::Test {
 protected:
  FeatureLibraryTest()
      : mock_bus_(new dbus::MockBus{dbus::Bus::Options{}}),
        mock_proxy_(new dbus::MockObjectProxy(
            mock_bus_.get(),
            chromeos::kChromeFeaturesServiceName,
            dbus::ObjectPath(chromeos::kChromeFeaturesServicePath))) {}

  ~FeatureLibraryTest() { mock_bus_->ShutdownAndBlock(); }

  void SetUp() override {
    features_ = std::unique_ptr<PlatformFeatures>(
        new PlatformFeatures(mock_bus_, mock_proxy_.get()));
  }

  std::unique_ptr<dbus::Response> CreateIsEnabledResponse(
      dbus::MethodCall* call, bool enabled) {
    if (call->GetInterface() == "org.chromium.ChromeFeaturesServiceInterface" &&
        call->GetMember() == "IsFeatureEnabled") {
      std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
      dbus::MessageWriter writer(response.get());
      writer.AppendBool(enabled);
      return response;
    }
    LOG(ERROR) << "Unexpected method call " << call->ToString();
    return nullptr;
  }

  std::unique_ptr<dbus::Response> CreateGetParamsResponse(
      dbus::MethodCall* call,
      std::map<std::string, std::map<std::string, std::string>> params_map,
      std::map<std::string, bool> enabled_map) {
    if (call->GetInterface() == "org.chromium.ChromeFeaturesServiceInterface" &&
        call->GetMember() == "GetFeatureParams") {
      dbus::MessageReader reader(call);
      dbus::MessageReader array_reader(nullptr);
      if (!reader.PopArray(&array_reader)) {
        LOG(ERROR) << "Failed to read array of feature names.";
        return nullptr;
      }
      std::vector<std::string> input_features;
      while (array_reader.HasMoreData()) {
        std::string feature_name;
        if (!array_reader.PopString(&feature_name)) {
          LOG(ERROR) << "Failed to pop feature_name from array.";
          return nullptr;
        }
        input_features.push_back(feature_name);
      }

      std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
      dbus::MessageWriter writer(response.get());

      // Copied from chrome_features_service_provider.cc.
      dbus::MessageWriter array_writer(nullptr);
      // A map from feature name to:
      // * two booleans:
      //   * Whether to use the override (or the default),
      //   * What the override state is (only valid if we should use the
      //     override value).
      // * Another map, from parameter name to value.
      writer.OpenArray("{s(bba{ss})}", &array_writer);
      for (const auto& feature_name : input_features) {
        dbus::MessageWriter feature_dict_writer(nullptr);
        array_writer.OpenDictEntry(&feature_dict_writer);
        feature_dict_writer.AppendString(feature_name);
        dbus::MessageWriter struct_writer(nullptr);
        feature_dict_writer.OpenStruct(&struct_writer);

        if (enabled_map.find(feature_name) != enabled_map.end()) {
          struct_writer.AppendBool(true);  // Use override
          struct_writer.AppendBool(enabled_map[feature_name]);
        } else {
          struct_writer.AppendBool(false);  // Ignore override
          struct_writer.AppendBool(false);  // Arbitrary choice
        }

        dbus::MessageWriter sub_array_writer(nullptr);
        struct_writer.OpenArray("{ss}", &sub_array_writer);
        if (params_map.find(feature_name) != params_map.end()) {
          const auto& submap = params_map[feature_name];
          for (const auto& [key, value] : submap) {
            dbus::MessageWriter dict_writer(nullptr);
            sub_array_writer.OpenDictEntry(&dict_writer);
            dict_writer.AppendString(key);
            dict_writer.AppendString(value);
            sub_array_writer.CloseContainer(&dict_writer);
          }
        }
        struct_writer.CloseContainer(&sub_array_writer);
        feature_dict_writer.CloseContainer(&struct_writer);
        array_writer.CloseContainer(&feature_dict_writer);
      }
      writer.CloseContainer(&array_writer);

      return response;
    }
    LOG(ERROR) << "Unexpected method call " << call->ToString();
    return nullptr;
  }

  base::test::SingleThreadTaskEnvironment task_environment_;
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_proxy_;
  std::unique_ptr<PlatformFeatures> features_;
  std::unique_ptr<base::RunLoop> run_loop_;
};

// Parameterized tests, with a boolean indicating whether the feature should be
// enabled.
class FeatureLibraryParameterizedTest
    : public FeatureLibraryTest,
      public ::testing::WithParamInterface<bool> {};

INSTANTIATE_TEST_SUITE_P(FeatureLibraryParameterizedTest,
                         FeatureLibraryParameterizedTest,
                         testing::Values(true, false));

TEST_P(FeatureLibraryParameterizedTest, IsEnabled_Success) {
  bool enabled = GetParam();

  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(true); }));

  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .WillOnce(Invoke(
          [this, enabled](dbus::MethodCall* call, int timeout_ms,
                          dbus::MockObjectProxy::ResponseCallback* callback) {
            std::unique_ptr<dbus::Response> resp =
                CreateIsEnabledResponse(call, enabled);
            std::move(*callback).Run(resp.get());
          }));

  run_loop_ = std::make_unique<base::RunLoop>();

  VariationsFeature f{"Feature", FEATURE_DISABLED_BY_DEFAULT};
  features_->IsEnabled(f,
                       base::BindLambdaForTesting([this, enabled](bool actual) {
                         EXPECT_EQ(enabled, actual);
                         run_loop_->Quit();
                       }));

  run_loop_->Run();
}

TEST_P(FeatureLibraryParameterizedTest, IsEnabled_Failure_WaitForService) {
  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(false); }));

  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .Times(0);

  run_loop_ = std::make_unique<base::RunLoop>();

  bool enabled = GetParam();
  FeatureState feature_state =
      GetParam() ? FEATURE_ENABLED_BY_DEFAULT : FEATURE_DISABLED_BY_DEFAULT;
  VariationsFeature f{"Feature", feature_state};

  features_->IsEnabled(f,
                       base::BindLambdaForTesting([this, enabled](bool actual) {
                         EXPECT_EQ(enabled, actual);
                         run_loop_->Quit();
                       }));
  run_loop_->Run();
}

TEST_P(FeatureLibraryParameterizedTest, IsEnabled_Failure_NullResponse) {
  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(true); }));

  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .WillOnce(Invoke([](dbus::MethodCall* call, int timeout_ms,
                          dbus::MockObjectProxy::ResponseCallback* callback) {
        std::move(*callback).Run(nullptr);
      }));

  run_loop_ = std::make_unique<base::RunLoop>();

  bool enabled = GetParam();
  FeatureState feature_state =
      GetParam() ? FEATURE_ENABLED_BY_DEFAULT : FEATURE_DISABLED_BY_DEFAULT;
  VariationsFeature f{"Feature", feature_state};

  features_->IsEnabled(f,
                       base::BindLambdaForTesting([this, enabled](bool actual) {
                         EXPECT_EQ(enabled, actual);
                         run_loop_->Quit();
                       }));
  run_loop_->Run();
}

TEST_P(FeatureLibraryParameterizedTest, IsEnabled_Failure_EmptyResponse) {
  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(true); }));

  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .WillOnce(Invoke(
          [&response](dbus::MethodCall* call, int timeout_ms,
                      dbus::MockObjectProxy::ResponseCallback* callback) {
            std::move(*callback).Run(response.get());
          }));

  run_loop_ = std::make_unique<base::RunLoop>();

  bool enabled = GetParam();
  FeatureState feature_state =
      GetParam() ? FEATURE_ENABLED_BY_DEFAULT : FEATURE_DISABLED_BY_DEFAULT;
  VariationsFeature f{"Feature", feature_state};

  features_->IsEnabled(f,
                       base::BindLambdaForTesting([this, enabled](bool actual) {
                         EXPECT_EQ(enabled, actual);
                         run_loop_->Quit();
                       }));

  run_loop_->Run();
}

TEST_P(FeatureLibraryParameterizedTest, IsEnabledBlocking_Success) {
  bool enabled = GetParam();

  EXPECT_CALL(*mock_proxy_,
              CallMethodAndBlock(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(Invoke([this, enabled](dbus::MethodCall* call, int timeout_ms) {
        return CreateIsEnabledResponse(call, enabled);
      }));

  VariationsFeature f{"Feature", FEATURE_DISABLED_BY_DEFAULT};
  EXPECT_EQ(enabled, features_->IsEnabledBlocking(f));
}

TEST_P(FeatureLibraryParameterizedTest, IsEnabledBlocking_Failure_Null) {
  EXPECT_CALL(*mock_proxy_,
              CallMethodAndBlock(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(Invoke(
          [](dbus::MethodCall* call, int timeout_ms) { return nullptr; }));

  bool enabled = GetParam();
  FeatureState feature_state =
      GetParam() ? FEATURE_ENABLED_BY_DEFAULT : FEATURE_DISABLED_BY_DEFAULT;
  VariationsFeature f{"Feature", feature_state};

  EXPECT_EQ(enabled, features_->IsEnabledBlocking(f));
}

TEST_P(FeatureLibraryParameterizedTest, IsEnabledBlocking_Failure_Empty) {
  EXPECT_CALL(*mock_proxy_,
              CallMethodAndBlock(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(Invoke([](dbus::MethodCall* call, int timeout_ms) {
        return dbus::Response::CreateEmpty();
      }));

  bool enabled = GetParam();
  FeatureState feature_state =
      GetParam() ? FEATURE_ENABLED_BY_DEFAULT : FEATURE_DISABLED_BY_DEFAULT;
  VariationsFeature f{"Feature", feature_state};

  EXPECT_EQ(enabled, features_->IsEnabledBlocking(f));
}

TEST_F(FeatureLibraryTest, GetParamsAndEnabled_Success) {
  // Will be enabled with params.
  VariationsFeature f1{"Feature1", FEATURE_DISABLED_BY_DEFAULT};
  // Will be explicitly disabled (and hence no params).
  VariationsFeature f2{"Feature2", FEATURE_DISABLED_BY_DEFAULT};
  // Will be default state (and hence no params).
  VariationsFeature f3{"Feature3", FEATURE_DISABLED_BY_DEFAULT};
  // Will be explicitly disabled (and hence no params).
  VariationsFeature f4{"Feature4", FEATURE_ENABLED_BY_DEFAULT};
  // Will be enabled with *no* params
  VariationsFeature f5{"Feature5", FEATURE_DISABLED_BY_DEFAULT};
  // Will be enabled by default with *no* params
  VariationsFeature f6{"Feature6", FEATURE_ENABLED_BY_DEFAULT};

  std::map<std::string, std::map<std::string, std::string>> params_map{
      {f1.name, {{"key", "value"}, {"anotherkey", "anothervalue"}}},
  };
  std::map<std::string, bool> enabled_map{
      {f1.name, true},
      {f2.name, false},
      // f3 is default
      {f4.name, false},
      {f5.name, true},
      // f6 is default
  };

  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(true); }));

  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .WillOnce(Invoke([this, enabled_map, params_map](
                           dbus::MethodCall* call, int timeout_ms,
                           dbus::MockObjectProxy::ResponseCallback* callback) {
        std::unique_ptr<dbus::Response> resp =
            CreateGetParamsResponse(call, params_map, enabled_map);
        std::move(*callback).Run(resp.get());
      }));

  run_loop_ = std::make_unique<base::RunLoop>();

  PlatformFeaturesInterface::ParamsResult expected{
      {
          f1.name,
          {
              .enabled = true,
              .params = params_map[f1.name],
          },
      },
      {
          f2.name,
          {
              .enabled = false,
          },
      },
      {
          f3.name,
          {
              .enabled = false,
          },
      },
      {
          f4.name,
          {
              .enabled = false,
          },
      },
      {
          f5.name,
          {
              .enabled = true,
          },
      },
      {
          f6.name,
          {
              .enabled = true,
          },
      }};

  features_->GetParamsAndEnabled(
      {&f1, &f2, &f3, &f4, &f5, &f6},
      base::BindLambdaForTesting(
          [this, expected](PlatformFeaturesInterface::ParamsResult actual) {
            EXPECT_EQ(actual.size(), expected.size());
            for (const auto& [name, entry] : actual) {
              auto it = expected.find(name);
              ASSERT_NE(it, expected.end()) << name;
              EXPECT_EQ(entry.enabled, it->second.enabled) << name;
              EXPECT_EQ(entry.params, it->second.params) << name;
            }
            run_loop_->Quit();
          }));

  run_loop_->Run();
}

TEST_F(FeatureLibraryTest, GetParamsAndEnabled_Failure_WaitForService) {
  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(false); }));

  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .Times(0);

  run_loop_ = std::make_unique<base::RunLoop>();

  VariationsFeature f1{"Feature1", FEATURE_ENABLED_BY_DEFAULT};
  VariationsFeature f2{"Feature2", FEATURE_DISABLED_BY_DEFAULT};

  PlatformFeaturesInterface::ParamsResult expected{{
                                                       f1.name,
                                                       {
                                                           .enabled = true,
                                                       },
                                                   },
                                                   {
                                                       f2.name,
                                                       {
                                                           .enabled = false,
                                                       },
                                                   }};

  features_->GetParamsAndEnabled(
      {&f1, &f2},
      base::BindLambdaForTesting(
          [this, expected](PlatformFeaturesInterface::ParamsResult actual) {
            EXPECT_EQ(actual.size(), expected.size());
            for (const auto& [name, entry] : actual) {
              auto it = expected.find(name);
              ASSERT_NE(it, expected.end()) << name;
              EXPECT_EQ(entry.enabled, it->second.enabled) << name;
              EXPECT_EQ(entry.params, it->second.params) << name;
            }
            run_loop_->Quit();
          }));
  run_loop_->Run();
}

TEST_F(FeatureLibraryTest, GetParamsAndEnabled_Failure_Null) {
  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(true); }));

  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .WillOnce(Invoke([](dbus::MethodCall* call, int timeout_ms,
                          dbus::MockObjectProxy::ResponseCallback* callback) {
        std::move(*callback).Run(nullptr);
      }));

  run_loop_ = std::make_unique<base::RunLoop>();

  VariationsFeature f1{"Feature1", FEATURE_ENABLED_BY_DEFAULT};
  VariationsFeature f2{"Feature2", FEATURE_DISABLED_BY_DEFAULT};

  PlatformFeaturesInterface::ParamsResult expected{{
                                                       f1.name,
                                                       {
                                                           .enabled = true,
                                                       },
                                                   },
                                                   {
                                                       f2.name,
                                                       {
                                                           .enabled = false,
                                                       },
                                                   }};

  features_->GetParamsAndEnabled(
      {&f1, &f2},
      base::BindLambdaForTesting(
          [this, expected](PlatformFeaturesInterface::ParamsResult actual) {
            EXPECT_EQ(actual.size(), expected.size());
            for (const auto& [name, entry] : actual) {
              auto it = expected.find(name);
              ASSERT_NE(it, expected.end()) << name;
              EXPECT_EQ(entry.enabled, it->second.enabled) << name;
              EXPECT_EQ(entry.params, it->second.params) << name;
            }
            run_loop_->Quit();
          }));
  run_loop_->Run();
}

TEST_F(FeatureLibraryTest, GetParamsAndEnabled_Failure_Empty) {
  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(true); }));

  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .WillOnce(Invoke(
          [&response](dbus::MethodCall* call, int timeout_ms,
                      dbus::MockObjectProxy::ResponseCallback* callback) {
            std::move(*callback).Run(response.get());
          }));

  run_loop_ = std::make_unique<base::RunLoop>();

  VariationsFeature f1{"Feature1", FEATURE_ENABLED_BY_DEFAULT};
  VariationsFeature f2{"Feature2", FEATURE_DISABLED_BY_DEFAULT};

  PlatformFeaturesInterface::ParamsResult expected{{
                                                       f1.name,
                                                       {
                                                           .enabled = true,
                                                       },
                                                   },
                                                   {
                                                       f2.name,
                                                       {
                                                           .enabled = false,
                                                       },
                                                   }};

  features_->GetParamsAndEnabled(
      {&f1, &f2},
      base::BindLambdaForTesting(
          [this, expected](PlatformFeaturesInterface::ParamsResult actual) {
            EXPECT_EQ(actual.size(), expected.size());
            for (const auto& [name, entry] : actual) {
              auto it = expected.find(name);
              ASSERT_NE(it, expected.end()) << name;
              EXPECT_EQ(entry.enabled, it->second.enabled) << name;
              EXPECT_EQ(entry.params, it->second.params) << name;
            }
            run_loop_->Quit();
          }));
  run_loop_->Run();
}

// Invalid response should result in default values.
TEST_F(FeatureLibraryTest, GetParamsAndEnabled_Failure_Invalid) {
  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(true); }));

  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
  dbus::MessageWriter writer(response.get());
  writer.AppendBool(true);
  writer.AppendBool(true);
  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .WillOnce(Invoke(
          [&response](dbus::MethodCall* call, int timeout_ms,
                      dbus::MockObjectProxy::ResponseCallback* callback) {
            std::move(*callback).Run(response.get());
          }));

  run_loop_ = std::make_unique<base::RunLoop>();

  VariationsFeature f1{"Feature1", FEATURE_ENABLED_BY_DEFAULT};
  VariationsFeature f2{"Feature2", FEATURE_DISABLED_BY_DEFAULT};

  PlatformFeaturesInterface::ParamsResult expected{{
                                                       f1.name,
                                                       {
                                                           .enabled = true,
                                                       },
                                                   },
                                                   {
                                                       f2.name,
                                                       {
                                                           .enabled = false,
                                                       },
                                                   }};

  features_->GetParamsAndEnabled(
      {&f1, &f2},
      base::BindLambdaForTesting(
          [this, expected](PlatformFeaturesInterface::ParamsResult actual) {
            EXPECT_EQ(actual.size(), expected.size());
            for (const auto& [name, entry] : actual) {
              auto it = expected.find(name);
              ASSERT_NE(it, expected.end()) << name;
              EXPECT_EQ(entry.enabled, it->second.enabled) << name;
              EXPECT_EQ(entry.params, it->second.params) << name;
            }
            run_loop_->Quit();
          }));
  run_loop_->Run();
}

TEST_F(FeatureLibraryTest, GetParamsAndEnabledBlocking) {
  // Will be enabled with params.
  VariationsFeature f1{"Feature1", FEATURE_DISABLED_BY_DEFAULT};
  // Will be explicitly disabled (and hence no params).
  VariationsFeature f2{"Feature2", FEATURE_DISABLED_BY_DEFAULT};
  // Will be default state (and hence no params).
  VariationsFeature f3{"Feature3", FEATURE_DISABLED_BY_DEFAULT};
  // Will be explicitly disabled (and hence no params).
  VariationsFeature f4{"Feature4", FEATURE_ENABLED_BY_DEFAULT};
  // Will be enabled with *no* params
  VariationsFeature f5{"Feature5", FEATURE_DISABLED_BY_DEFAULT};
  // Will be enabled by default with *no* params
  VariationsFeature f6{"Feature6", FEATURE_ENABLED_BY_DEFAULT};

  std::map<std::string, std::map<std::string, std::string>> params_map{
      {f1.name, {{"key", "value"}, {"anotherkey", "anothervalue"}}},
  };
  std::map<std::string, bool> enabled_map{
      {f1.name, true},
      {f2.name, false},
      // f3 is default
      {f4.name, false},
      {f5.name, true},
      // f6 is default
  };

  EXPECT_CALL(*mock_proxy_,
              CallMethodAndBlock(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(Invoke([this, enabled_map, params_map](dbus::MethodCall* call,
                                                       int timeout_ms) {
        return CreateGetParamsResponse(call, params_map, enabled_map);
      }));

  PlatformFeaturesInterface::ParamsResult expected{
      {
          f1.name,
          {
              .enabled = true,
              .params = params_map[f1.name],
          },
      },
      {
          f2.name,
          {
              .enabled = false,
          },
      },
      {
          f3.name,
          {
              .enabled = false,
          },
      },
      {
          f4.name,
          {
              .enabled = false,
          },
      },
      {
          f5.name,
          {
              .enabled = true,
          },
      },
      {
          f6.name,
          {
              .enabled = true,
          },
      }};

  auto actual =
      features_->GetParamsAndEnabledBlocking({&f1, &f2, &f3, &f4, &f5, &f6});
  EXPECT_EQ(actual.size(), expected.size());
  for (const auto& [name, entry] : actual) {
    auto it = expected.find(name);
    ASSERT_NE(it, expected.end()) << name;
    EXPECT_EQ(entry.enabled, it->second.enabled) << name;
    EXPECT_EQ(entry.params, it->second.params) << name;
  }
}

TEST_F(FeatureLibraryTest, GetParamsAndEnabledBlocking_Failure_Null) {
  EXPECT_CALL(*mock_proxy_,
              CallMethodAndBlock(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(Invoke(
          [](dbus::MethodCall* call, int timeout_ms) { return nullptr; }));

  VariationsFeature f1{"Feature1", FEATURE_ENABLED_BY_DEFAULT};
  VariationsFeature f2{"Feature2", FEATURE_DISABLED_BY_DEFAULT};

  PlatformFeaturesInterface::ParamsResult expected{{
                                                       f1.name,
                                                       {
                                                           .enabled = true,
                                                       },
                                                   },
                                                   {
                                                       f2.name,
                                                       {
                                                           .enabled = false,
                                                       },
                                                   }};

  auto actual = features_->GetParamsAndEnabledBlocking({&f1, &f2});
  EXPECT_EQ(actual.size(), expected.size());
  for (const auto& [name, entry] : actual) {
    auto it = expected.find(name);
    ASSERT_NE(it, expected.end()) << name;
    EXPECT_EQ(entry.enabled, it->second.enabled) << name;
    EXPECT_EQ(entry.params, it->second.params) << name;
  }
}

TEST_F(FeatureLibraryTest, GetParamsAndEnabledBlocking_Failure_Empty) {
  EXPECT_CALL(*mock_proxy_,
              CallMethodAndBlock(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(Invoke([](dbus::MethodCall* call, int timeout_ms) {
        return dbus::Response::CreateEmpty();
      }));

  VariationsFeature f1{"Feature1", FEATURE_ENABLED_BY_DEFAULT};
  VariationsFeature f2{"Feature2", FEATURE_DISABLED_BY_DEFAULT};

  PlatformFeaturesInterface::ParamsResult expected{{
                                                       f1.name,
                                                       {
                                                           .enabled = true,
                                                       },
                                                   },
                                                   {
                                                       f2.name,
                                                       {
                                                           .enabled = false,
                                                       },
                                                   }};

  auto actual = features_->GetParamsAndEnabledBlocking({&f1, &f2});
  EXPECT_EQ(actual.size(), expected.size());
  for (const auto& [name, entry] : actual) {
    auto it = expected.find(name);
    ASSERT_NE(it, expected.end()) << name;
    EXPECT_EQ(entry.enabled, it->second.enabled) << name;
    EXPECT_EQ(entry.params, it->second.params) << name;
  }
}

// Invalid response should result in default values.
TEST_F(FeatureLibraryTest, GetParamsAndEnabledBlocking_Failure_Invalid) {
  EXPECT_CALL(*mock_proxy_,
              CallMethodAndBlock(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(Invoke([](dbus::MethodCall* call, int timeout_ms) {
        std::unique_ptr<dbus::Response> response =
            dbus::Response::CreateEmpty();
        dbus::MessageWriter writer(response.get());
        writer.AppendBool(true);
        writer.AppendBool(true);
        return response;
      }));

  VariationsFeature f1{"Feature1", FEATURE_ENABLED_BY_DEFAULT};
  VariationsFeature f2{"Feature2", FEATURE_DISABLED_BY_DEFAULT};

  PlatformFeaturesInterface::ParamsResult expected{{
                                                       f1.name,
                                                       {
                                                           .enabled = true,
                                                       },
                                                   },
                                                   {
                                                       f2.name,
                                                       {
                                                           .enabled = false,
                                                       },
                                                   }};

  auto actual = features_->GetParamsAndEnabledBlocking({&f1, &f2});
  EXPECT_EQ(actual.size(), expected.size());
  for (const auto& [name, entry] : actual) {
    auto it = expected.find(name);
    ASSERT_NE(it, expected.end()) << name;
    EXPECT_EQ(entry.enabled, it->second.enabled) << name;
    EXPECT_EQ(entry.params, it->second.params) << name;
  }
}

TEST_F(FeatureLibraryTest, CheckFeatureIdentity) {
  VariationsFeature f1{"Feature", FEATURE_ENABLED_BY_DEFAULT};
  // A new, unseen feature should pass the check.
  EXPECT_TRUE(features_->CheckFeatureIdentity(f1));
  // As should a feature seen a second time.
  EXPECT_TRUE(features_->CheckFeatureIdentity(f1));

  VariationsFeature f2{"Feature", FEATURE_ENABLED_BY_DEFAULT};
  // A separate feature with the same name should fail.
  EXPECT_FALSE(features_->CheckFeatureIdentity(f2));

  VariationsFeature f3{"Feature3", FEATURE_ENABLED_BY_DEFAULT};
  // A distinct feature with a distinct name should pass.
  EXPECT_TRUE(features_->CheckFeatureIdentity(f3));
  EXPECT_TRUE(features_->CheckFeatureIdentity(f3));
}

#if DCHECK_IS_ON()
using FeatureLibraryDeathTest = FeatureLibraryTest;
TEST_F(FeatureLibraryDeathTest, IsEnabledDistinctFeatureDefs) {
  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(false); }));

  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .Times(0);

  run_loop_ = std::make_unique<base::RunLoop>();

  VariationsFeature f{"Feature", FEATURE_ENABLED_BY_DEFAULT};
  features_->IsEnabled(f, base::BindLambdaForTesting([this](bool enabled) {
                         EXPECT_TRUE(enabled);  // Default value
                         run_loop_->Quit();
                       }));
  run_loop_->Run();

  VariationsFeature f2{"Feature", FEATURE_ENABLED_BY_DEFAULT};
  EXPECT_DEATH(
      features_->IsEnabled(f2, base::BindLambdaForTesting([this](bool enabled) {
                             EXPECT_TRUE(enabled);  // Default value
                             run_loop_->Quit();
                           })),
      "Feature");
}

TEST_F(FeatureLibraryDeathTest, IsEnabledBlockingDistinctFeatureDefs) {
  EXPECT_CALL(*mock_proxy_,
              CallMethodAndBlock(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .Times(1);

  VariationsFeature f{"Feature", FEATURE_ENABLED_BY_DEFAULT};
  features_->IsEnabledBlocking(f);

  VariationsFeature f2{"Feature", FEATURE_ENABLED_BY_DEFAULT};
  EXPECT_DEATH(features_->IsEnabledBlocking(f2), "Feature");
}
#endif  // DCHECK_IS_ON()

class FeatureLibraryCmdTest : public testing::Test {
 public:
  FeatureLibraryCmdTest() {}
  ~FeatureLibraryCmdTest() {}
};

TEST_F(FeatureLibraryCmdTest, MkdirTest) {
  if (base::PathExists(
          base::FilePath("/sys/kernel/debug/tracing/instances/"))) {
    const std::string sys_path = "/sys/kernel/debug/tracing/instances/unittest";
    EXPECT_FALSE(base::PathExists(base::FilePath(sys_path)));
    EXPECT_TRUE(featured::MkdirCommand(sys_path).Execute());
    EXPECT_TRUE(base::PathExists(base::FilePath(sys_path)));
    EXPECT_TRUE(base::DeleteFile(base::FilePath(sys_path)));
    EXPECT_FALSE(base::PathExists(base::FilePath(sys_path)));
  }

  if (base::PathExists(base::FilePath("/mnt"))) {
    const std::string mnt_path = "/mnt/notallowed";
    EXPECT_FALSE(base::PathExists(base::FilePath(mnt_path)));
    EXPECT_FALSE(featured::MkdirCommand(mnt_path).Execute());
    EXPECT_FALSE(base::PathExists(base::FilePath(mnt_path)));
  }
}

}  // namespace feature
