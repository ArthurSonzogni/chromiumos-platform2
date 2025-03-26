// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_COMMON_H_
#define ODML_MANTIS_COMMON_H_

#include <array>
#include <string_view>

namespace mantis {

constexpr char kEnglishLocale[] = "en";
constexpr std::array<const char*, 3> kI18nLocale = {"fr", "de", "ja"};

// Returns `true` if the locale is a supported language by the mantis service,
// `false` otherwise.
bool IsLanguageSupported(std::string_view locale);

}  // namespace mantis

#endif  // ODML_MANTIS_COMMON_H_
