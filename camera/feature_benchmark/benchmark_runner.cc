// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "feature_benchmark/benchmark_runner.h"

#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/values.h>

namespace cros::tests {

BenchmarkConfig::BenchmarkConfig(const base::FilePath& file_path,
                                 const std::string& test_case_name)
    : test_case_name_(test_case_name) {
  CHECK(!file_path.empty() && base::PathExists(file_path));
  // Limiting config file size to 64 KB. Increase this if needed.
  constexpr size_t kConfigFileMaxSize = 65536;
  constexpr char kFpsKey[] = "fps";
  std::string contents;
  CHECK(base::ReadFileToStringWithMaxSize(file_path, &contents,
                                          kConfigFileMaxSize));
  std::optional<base::Value> json_values =
      base::JSONReader::Read(contents, base::JSON_ALLOW_TRAILING_COMMAS);
  CHECK(json_values.has_value());
  CHECK(json_values->is_dict());

  base::Value::Dict* test_case_config =
      json_values->GetDict().FindDict(test_case_name);
  CHECK(test_case_config != nullptr);
  test_case_config_ = std::move(*test_case_config);

  std::optional<int> fps = test_case_config_.FindInt(kFpsKey);
  CHECK(fps.has_value());
  fps_ = *fps;
}

}  // namespace cros::tests
