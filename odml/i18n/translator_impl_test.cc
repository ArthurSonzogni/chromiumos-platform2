// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/i18n/translator_impl.h"

#include <base/memory/raw_ref.h>
#include <base/test/bind.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "odml/i18n/fake/fake_translate_api.h"
#include "odml/i18n/translate_api.h"
#include "odml/utils/odml_shim_loader_mock.h"

namespace i18n {
namespace {
using base::test::TestFuture;
using i18n::fake::FakeTranslate;
using i18n::fake::kDlcCorruptedDictionary;
using i18n::fake::kDlcFake;
using i18n::fake::kDlcInvalid;
using i18n::fake::kFakeDictionaryManagerPtr;
using ::testing::Return;

using TranslateAPIGetter = const TranslateAPI* (*)();

class TranslatorTestImpl : public testing::Test {
 public:
  TranslatorTestImpl() : translator_(raw_ref(shim_loader_)) {}
  void SetupDlc(const std::string& path) {
    auto dlc_path = base::FilePath(path);
    cros::DlcClient::SetDlcPathForTest(&dlc_path);
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  TranslatorImpl translator_;
  odml::OdmlShimLoaderMock shim_loader_;
};

TEST_F(TranslatorTestImpl, TranslateSuccess) {
  const LangPair kFakeLangPair{"en", "ja"};
  const LangPair kFakeReverseLangPair{"ja", "en"};
  constexpr char kFakeInputText[] = "to be translated";
  // Initializing Translator.
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetTranslateAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(TranslateAPIGetter(
          []() -> const TranslateAPI* { return fake::GetTranslateApi(); }))));
  base::RunLoop run_loop_init;
  translator_.Initialize(base::BindLambdaForTesting([&](bool result) {
    EXPECT_EQ(result, true);
    run_loop_init.Quit();
  }));
  run_loop_init.Run();
  // Downalding DLC.
  SetupDlc(kDlcFake);
  base::RunLoop run_loop_dlc;
  translator_.DownloadDlc(kFakeLangPair,
                          base::BindLambdaForTesting([&](bool result) {
                            EXPECT_EQ(result, true);
                            run_loop_dlc.Quit();
                          }));
  run_loop_dlc.Run();
  // Translating.
  auto translation = translator_.Translate(kFakeLangPair, kFakeInputText);
  ASSERT_TRUE(translation.has_value());
  EXPECT_TRUE(translation.value() == FakeTranslate(kFakeInputText));
  // Translating reversely.
  translation = translator_.Translate(kFakeReverseLangPair, kFakeInputText);
  ASSERT_TRUE(translation.has_value());
  EXPECT_TRUE(translation.value() == FakeTranslate(kFakeInputText));
}

TEST_F(TranslatorTestImpl, TranslateLoadDictionaryCorrupted) {
  // Translating shall fail when dictionary failed to be loaded.
  const LangPair kFakeLangPair{"en", "ja"};
  constexpr char kFakeInputText[] = "to be translated";
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetTranslateAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(TranslateAPIGetter(
          []() -> const TranslateAPI* { return fake::GetTranslateApi(); }))));
  base::RunLoop run_loop_init;
  translator_.Initialize(base::BindLambdaForTesting([&](bool result) {
    EXPECT_EQ(result, true);
    run_loop_init.Quit();
  }));
  run_loop_init.Run();
  SetupDlc(kDlcCorruptedDictionary);
  base::RunLoop run_loop_dlc;
  translator_.DownloadDlc(kFakeLangPair,
                          base::BindLambdaForTesting([&](bool result) {
                            EXPECT_EQ(result, true);
                            run_loop_dlc.Quit();
                          }));
  run_loop_dlc.Run();
  // Corrupted dictionary makes translator fail to translate.
  auto translation = translator_.Translate(kFakeLangPair, kFakeInputText);
  EXPECT_FALSE(translation.has_value());
}

TEST_F(TranslatorTestImpl, TranslateLoadDictionaryFailure) {
  // Translating shall fail when dictionary failed to be loaded.
  const LangPair kFakeLangPair{"en", "ja"};
  constexpr char kFakeInputText[] = "to be translated";
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetTranslateAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(TranslateAPIGetter(
          []() -> const TranslateAPI* { return fake::GetTranslateApi(); }))));
  base::RunLoop run_loop_init;
  translator_.Initialize(base::BindLambdaForTesting([&](bool result) {
    EXPECT_EQ(result, true);
    run_loop_init.Quit();
  }));
  run_loop_init.Run();
  // Invalid DLC makes translator fail to load dictionary.
  SetupDlc(kDlcInvalid);
  base::RunLoop run_loop_dlc;
  translator_.DownloadDlc(kFakeLangPair,
                          base::BindLambdaForTesting([&](bool result) {
                            EXPECT_EQ(result, true);
                            run_loop_dlc.Quit();
                          }));
  run_loop_dlc.Run();
  auto translation = translator_.Translate(kFakeLangPair, kFakeInputText);
  EXPECT_FALSE(translation.has_value());
}

TEST_F(TranslatorTestImpl, TranslateDlcUnavailable) {
  // Translating shall fail without DLC downloaded.
  const LangPair kFakeLangPair{"en", "ja"};
  constexpr char kFakeInputText[] = "to be translated";
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetTranslateAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(TranslateAPIGetter(
          []() -> const TranslateAPI* { return fake::GetTranslateApi(); }))));
  base::RunLoop run_loop_init;
  translator_.Initialize(base::BindLambdaForTesting([&](bool result) {
    EXPECT_EQ(result, true);
    run_loop_init.Quit();
  }));
  run_loop_init.Run();
  auto translation = translator_.Translate(kFakeLangPair, kFakeInputText);
  EXPECT_FALSE(translation.has_value());
}

TEST_F(TranslatorTestImpl, TranslateNotInitialized) {
  // Translating shall fail with uninitialized translator.
  const LangPair kFakeLangPair{"en", "ja"};
  constexpr char kFakeInputText[] = "to be translated";
  auto translation = translator_.Translate(kFakeLangPair, kFakeInputText);
  EXPECT_FALSE(translation.has_value());
}

TEST_F(TranslatorTestImpl, InitializeNullAPI) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetTranslateAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(TranslateAPIGetter(
          []() -> const TranslateAPI* { return nullptr; }))));
  base::RunLoop run_loop_init;
  translator_.Initialize(base::BindLambdaForTesting([&](bool result) {
    EXPECT_EQ(result, false);
    run_loop_init.Quit();
  }));
  run_loop_init.Run();
}

TEST_F(TranslatorTestImpl, InitializeNullAPIGetter) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetTranslateAPI"))
      .WillOnce(Return(nullptr));
  base::RunLoop run_loop_init;
  translator_.Initialize(base::BindLambdaForTesting([&](bool result) {
    EXPECT_EQ(result, false);
    run_loop_init.Quit();
  }));
  run_loop_init.Run();
}

}  // namespace
}  // namespace i18n
