// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_CROSSYSTEM_UTILS_H_
#define RMAD_UTILS_FAKE_CROSSYSTEM_UTILS_H_

#include "rmad/utils/crossystem_utils.h"

#include <string>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>

#include "rmad/utils/json_store.h"

namespace rmad {
namespace fake {

class FakeCrosSystemUtils : public CrosSystemUtils {
 public:
  explicit FakeCrosSystemUtils(const base::FilePath& working_dir_path);
  ~FakeCrosSystemUtils() override = default;

  bool SetInt(const std::string& key, int value) override;
  bool GetInt(const std::string& key, int* value) const override;
  bool SetString(const std::string& key, const std::string& value) override;
  bool GetString(const std::string& key, std::string* value) const override;

 private:
  base::FilePath working_dir_path_;
  // Use |JsonStore| to read the fake crossystem values.
  scoped_refptr<JsonStore> json_store_;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_CROSSYSTEM_UTILS_H_
