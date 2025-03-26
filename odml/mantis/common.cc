// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/common.h"

#include <string_view>

#include "base/containers/contains.h"

namespace mantis {

bool IsLanguageSupported(std::string_view locale) {
  return base::Contains(kI18nLocale, locale);
}

}  // namespace mantis
