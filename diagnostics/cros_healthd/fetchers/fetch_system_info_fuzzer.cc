// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/test/scoped_chromeos_version_info.h>
#include <chromeos/chromeos-config/libcros_config/fake_cros_config.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/system_fetcher_constants.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {

namespace {

const std::vector<std::pair<std::string, std::string>> kFiles{
    // VPD files
    {kRelativePathVpdRw, kFileNameActivateDate},
    {kRelativePathVpdRo, kFileNameMfgDate},
    {kRelativePathVpdRo, kFileNameModelName},
    {kRelativePathVpdRo, kFileNameRegion},
    {kRelativePathVpdRo, kFileNameSerialNumber},
    {kRelativePathVpdRo, kFileNameSkuNumber},
};

void SetUpSystemFiles(const base::FilePath& root_dir,
                      FuzzedDataProvider* provider) {
  for (const auto& [dir, file] : kFiles) {
    CHECK(WriteFileAndCreateParentDirs(root_dir.Append(dir).Append(file),
                                       provider->ConsumeRandomLengthString()));
  }
  // Populate fake DMI values.
  base::FilePath relative_dmi_info_path = root_dir.Append(kRelativeDmiInfoPath);
  CHECK(WriteFileAndCreateParentDirs(
      relative_dmi_info_path.Append(kBiosVersionFileName),
      provider->ConsumeRandomLengthString()));
  CHECK(WriteFileAndCreateParentDirs(
      relative_dmi_info_path.Append(kBoardNameFileName),
      provider->ConsumeRandomLengthString()));
  CHECK(WriteFileAndCreateParentDirs(
      relative_dmi_info_path.Append(kBoardVersionFileName),
      provider->ConsumeRandomLengthString()));
  CHECK(WriteFileAndCreateParentDirs(
      relative_dmi_info_path.Append(kChassisTypeFileName),
      provider->ConsumeRandomLengthString()));
}

}  // namespace

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOGGING_FATAL);  // Disable logging.
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  // 100 KiB max input size. Doing multiple writes and read for significantly
  // large files can potentially cause the fuzzer to timeout.
  constexpr int kMaxInputSize = 102400;
  if (size > kMaxInputSize)
    return 0;

  FuzzedDataProvider provider(data, size);
  // Populate the fake lsb-release file.
  base::test::ScopedChromeOSVersionInfo version(
      provider.ConsumeRandomLengthString(), base::Time::Now());

  MockContext mock_context;

  mock_context.Initialize();
  SetUpSystemFiles(mock_context.root_dir(), &provider);
  mock_context.fake_system_config()->SetHasSkuNumber(true);
  mock_context.fake_system_config()->SetMarketingName("fake_marketing_name");
  mock_context.fake_system_config()->SetProductName("fake_product_name");
  SystemFetcher system_fetcher{&mock_context};
  auto system_info = system_fetcher.FetchSystemInfo();

  return 0;
}

}  // namespace diagnostics
