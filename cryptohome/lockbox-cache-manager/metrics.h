// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_LOCKBOX_CACHE_MANAGER_METRICS_H_
#define CRYPTOHOME_LOCKBOX_CACHE_MANAGER_METRICS_H_

#include <metrics/metrics_library.h>

namespace cryptohome {

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class MigrationStatus {
  kSuccess,    // Migration successful.
  kNotNeeded,  // No legacy install-attributes. Migration not needed.
  kReadFail,   // Fail to read the install-attributes from old path.
  kCopyFail,   // Fail to copy the install-attributes from old to new path.
  kSyncFail,   // Fail to sync the new install-attributes dir.
  kMaxValue =
      kSyncFail,  // This is unused, just for counting the number of elements.
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
