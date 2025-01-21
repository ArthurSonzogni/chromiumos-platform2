// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_I18N_FAKE_FAKE_TRANSLATE_API_H_
#define ODML_I18N_FAKE_FAKE_TRANSLATE_API_H_

#include <string>

#include "odml/i18n/translator.h"

namespace i18n::fake {

constexpr DictionaryManagerPtr kFakeDictionaryManagerPtr = 0x1337;
constexpr DictionaryManagerPtr kFakeInvalidDictionaryManagerPtr = 0x7331;
constexpr char kDlcInvalid[] = "invalid";
constexpr char kDlcCorruptedDictionary[] = "corrupted";
constexpr char kDlcFake[] = "fake";

// Create a fake translation by reversing the string
std::string FakeTranslate(const std::string& s);

const TranslateAPI* GetTranslateApi();

}  // namespace i18n::fake

#endif  // ODML_I18N_FAKE_FAKE_TRANSLATE_API_H_
