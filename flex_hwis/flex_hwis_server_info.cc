// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis_server_info.h"

#include <base/logging.h>
#include <base/strings/string_util.h>
#include <brillo/key_value_store.h>

#if USE_FLEX_INTERNAL
#include "flex_hwis_private/server_info.h"
#endif

namespace flex_hwis {

ServerInfo::ServerInfo() {
#if USE_FLEX_INTERNAL
  const base::FilePath kLsbReleaseFile = base::FilePath("/etc/lsb-release");
  const auto is_test_image = IsTestImage(kLsbReleaseFile);
  // To avoid polluting the databases, if an error occurs when
  // confirming whether an image is a test image, the server_url and
  // api_key will be empty strings.
  if (is_test_image == TestImageResult::TestImage) {
    server_url = flex_hwis_private::kServerUrlForTesting;
    api_key = flex_hwis_private::kApiKeyForTesting;
  } else if (is_test_image == TestImageResult::NotTestImage) {
    server_url = flex_hwis_private::kServerUrl;
    api_key = flex_hwis_private::kApiKey;
  }
#endif
}

const std::string& ServerInfo::GetServerUrl() const {
  return server_url;
}

const std::string& ServerInfo::GetApiKey() const {
  return api_key;
}

TestImageResult ServerInfo::IsTestImage(const base::FilePath& lsb_file) {
  brillo::KeyValueStore store;
  if (!store.Load(lsb_file)) {
    LOG(WARNING) << "Problem parsing " << lsb_file;
    return TestImageResult::Error;
  }
  std::string value;
  if (!store.GetString("CHROMEOS_RELEASE_TRACK", &value)) {
    LOG(WARNING) << "CHROMEOS_RELEASE_TRACK not found in " << lsb_file;
    return TestImageResult::Error;
  }
  if (base::StartsWith(value, "test", base::CompareCase::SENSITIVE)) {
    return TestImageResult::TestImage;
  } else {
    return TestImageResult::NotTestImage;
  }
}

}  // namespace flex_hwis
