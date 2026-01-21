// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/common.h"

#include <algorithm>
#include <string_view>

namespace mantis {

bool IsLanguageSupported(std::string_view locale) {
  return std::ranges::contains(kI18nLocale, locale);
}

}  // namespace mantis
