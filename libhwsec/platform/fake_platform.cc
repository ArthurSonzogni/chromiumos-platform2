// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/platform/fake_platform.h"

#include <base/files/file_util.h>
#include <gmock/gmock.h>

using base::FilePath;
using testing::_;
using testing::Invoke;

namespace hwsec {

FakePlatform::FakePlatform() {
  CHECK(temp_dir_.CreateUniqueTempDir());
  root_ = temp_dir_.GetPath();
  ON_CALL(*this, ReadFileToString(_, _))
      .WillByDefault(Invoke(this, &FakePlatform::ReadFileToStringInternal));
}

bool FakePlatform::ReadFileToStringInternal(const FilePath& path,
                                            std::string* contents) {
  CHECK(path.IsAbsolute());

  // Append the path (the part after "/") to root_.
  FilePath actual = root_;
  CHECK(FilePath("/").AppendRelativePath(path, &actual));
  return base::ReadFileToString(actual, contents);
}

}  // namespace hwsec
