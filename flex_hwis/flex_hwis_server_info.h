// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_FLEX_HWIS_SERVER_INFO_H_
#define FLEX_HWIS_FLEX_HWIS_SERVER_INFO_H_

#include <string>

#include <base/files/file_util.h>

namespace flex_hwis {
enum class TestImageResult {
  // The image the service runs on is the test image
  TestImage,
  // The image the service runs on is not the test image.
  NotTestImage,
  // Encountered an error.
  Error
};
class ServerInfo {
 public:
  ServerInfo();
  const std::string& GetServerUrl() const;
  const std::string& GetApiKey() const;
  // Determine if the device is using a test image. If an error occurs,
  // record the error information and return TestImageResult::Error.
  // This method refers to /src/platform2/init/startup/platform_impl.cc.
  // TODO(b/308163572): extract IsTestImage function to libbrillo library.
  TestImageResult IsTestImage(const base::FilePath& lsb_file);

 private:
  std::string server_url;
  std::string api_key;
};
}  // namespace flex_hwis

#endif  // FLEX_HWIS_FLEX_HWIS_SERVER_INFO_H_
