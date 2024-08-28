// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FARFETCHD_PREFETCH_HELPER_H_
#define FARFETCHD_PREFETCH_HELPER_H_

#include "base/time/time.h"
#include "libstorage/platform/platform.h"

namespace farfetchd {

class PrefetchHelper {
 public:
  explicit PrefetchHelper(libstorage::Platform* platform);
  bool PreloadFile(const base::FilePath& path);
  bool PreloadFileMmap(const base::FilePath& path);
  bool PreloadFileAsync(const base::FilePath& path);
  ~PrefetchHelper() = default;

 protected:
  libstorage::Platform* p;
};

}  // namespace farfetchd

#endif  // FARFETCHD_PREFETCH_HELPER_H_
