// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/logging.h>
#include <rootdev/rootdev.h>

#include "storage_info/storage_capability_reporter.h"

namespace {

base::FilePath GetRootDevice() {
  char buf[PATH_MAX];
  return rootdev(buf, PATH_MAX, true, true) == 0 ? base::FilePath(buf)
                                                 : base::FilePath();
}

bool ReportMetrics(const base::FilePath& rootdev) {
  if (rootdev.empty()) {
    LOG(ERROR) << "Could not detect root device";
    return false;
  }
  return ReportCaps(CollectCaps(rootdev));
}

}  // namespace

int main(int argc, char* argv[]) {
  return ReportMetrics(GetRootDevice()) ? 0 : -1;
}
