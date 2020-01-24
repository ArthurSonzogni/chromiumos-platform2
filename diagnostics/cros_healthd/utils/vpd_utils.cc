// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/vpd_utils.h"

#include <string>

#include <base/logging.h>

#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

using ::chromeos::cros_healthd::mojom::CachedVpdInfo;
using ::chromeos::cros_healthd::mojom::CachedVpdInfoPtr;

constexpr char kCachedVpdPropertiesPath[] = "/cros-healthd/cached-vpd";
constexpr char kHasSkuNumberProperty[] = "has-sku-number";
constexpr char kRelativeSkuNumberDir[] = "sys/firmware/vpd/ro/";
constexpr char kSkuNumberFileName[] = "sku_number";

}  // namespace

CachedVpdFetcher::CachedVpdFetcher(brillo::CrosConfigInterface* cros_config)
    : cros_config_(cros_config) {
  DCHECK(cros_config_);
}

CachedVpdFetcher::~CachedVpdFetcher() = default;

CachedVpdInfoPtr CachedVpdFetcher::FetchCachedVpdInfo(
    const base::FilePath& root_dir) {
  CachedVpdInfo vpd_info;
  std::string has_sku_number;
  cros_config_->GetString(kCachedVpdPropertiesPath, kHasSkuNumberProperty,
                          &has_sku_number);
  if (has_sku_number == "true") {
    std::string sku_number;
    ReadAndTrimString(root_dir.Append(kRelativeSkuNumberDir),
                      kSkuNumberFileName, &sku_number);
    vpd_info.sku_number = sku_number;
  }

  return vpd_info.Clone();
}

}  // namespace diagnostics
