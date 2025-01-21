// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_I18N_TRANSLATE_API_H_
#define ODML_I18N_TRANSLATE_API_H_

#include <string>

namespace i18n {

// Opaque handle to a DictionaryManagerPtr.
using DictionaryManagerPtr = uintptr_t;

enum class TranslateStatus : int {
  kOk,
  kInitializationFailed,
  kProcessFailed,
  kInvalidArgument,
};

struct InitializeResult {
  TranslateStatus status;
  DictionaryManagerPtr dictionary;
};

struct TranslateResult {
  TranslateStatus status;
  std::string translation;
};

// Table of C API functions defined within the library.
struct TranslateAPI {
  // Initializes the dictionary.
  InitializeResult (*Initialize)(const std::string& package_dir_path,
                                 const std::string& source_language,
                                 const std::string& target_language);
  // Translates the given text.
  TranslateResult (*Translate)(DictionaryManagerPtr dictionary,
                               const std::string& input_text);

  void (*Destroy)(DictionaryManagerPtr dictionary);
};

}  // namespace i18n

#endif  // ODML_I18N_TRANSLATE_API_H_
