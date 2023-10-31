// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_LOCKBOX_CACHE_MANAGER_METRICS_H_
#define CRYPTOHOME_LOCKBOX_CACHE_MANAGER_METRICS_H_

#include <metrics/metrics_library.h>

namespace cryptohome {

enum class MigrationStatus {
  kSuccess,     // Migration successful.
  kNotNeeded,   // No legacy install-attributes. Migration not needed.
  kMkdirFail,   // Fail to create new dir for install-attributes.
  kCopyFail,    // Fail to copy the legacy install-attributes to inter. path.
  kMoveFail,    // Fail to move the copy of install-attributes to new path.
  kDeleteFail,  // Fail to delete legacy install-attributes.
  kMaxValue,    // This is unused, just for counting the number of elements.
                // Note that kMaxValue should always be the last element.
};

class Metrics : private MetricsLibrary {
 public:
  Metrics() = default;
  virtual ~Metrics() = default;

  virtual void ReportInstallAttributesMigrationStatus(MigrationStatus status);

 private:
  MetricsLibraryInterface* metrics_library_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_LOCKBOX_CACHE_MANAGER_METRICS_H_
