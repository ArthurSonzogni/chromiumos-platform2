// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/i18n/fake/fake_translate_api.h"

#include <string>
#include <vector>

#include <base/logging.h>

namespace i18n::fake {
namespace {

InitializeResult Initialize(const std::string& package_dir_path,
                            const std::string& source_language,
                            const std::string& target_language) {
  if (package_dir_path == kDlcInvalid) {
    return {
        .status = TranslateStatus::kInitializationFailed,
    };
  }
  if (package_dir_path == kDlcCorruptedDictionary) {
    return {
        .status = TranslateStatus::kOk,
        .dictionary = kFakeInvalidDictionaryManagerPtr,
    };
  }
  return {
      .status = TranslateStatus::kOk,
      .dictionary = kFakeDictionaryManagerPtr,
  };
}

TranslateResult Translate(DictionaryManagerPtr dictionary,
                          const std::string& input_text) {
  if (dictionary != kFakeDictionaryManagerPtr) {
    return {
        .status = TranslateStatus::kInvalidArgument,
    };
  }
  return {
      .status = TranslateStatus::kOk,
      .translation = FakeTranslate(input_text),
  };
}

void Destroy(DictionaryManagerPtr dictionary) {}

const TranslateAPI api = {
    .Initialize = &Initialize,
    .Translate = &Translate,
    .Destroy = &Destroy,
};

}  // namespace

std::string FakeTranslate(const std::string& s) {
  return std::string(s.rbegin(), s.rend());
}

const TranslateAPI* GetTranslateApi() {
  return &api;
}
}  // namespace i18n::fake
