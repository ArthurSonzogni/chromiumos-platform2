// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_STORAGE_STORAGE_UTIL_H_
#define MISSIVE_STORAGE_STORAGE_UTIL_H_

#include <base/containers/flat_set.h>
#include <string>
#include <tuple>

#include "missive/storage/storage_configuration.h"

namespace reporting {

class StorageDirectory {
 public:
  static bool DeleteEmptySubdirectories(const base::FilePath directory);
  static base::flat_set<std::tuple<Priority, GenerationGuid>>
  FindQueueDirectories(const StorageOptions& options);

  static StatusOr<std::tuple<Priority, GenerationGuid>>
  GetPriorityAndGenerationGuid(const base::FilePath& full_name,
                               const StorageOptions& options);
  static StatusOr<GenerationGuid> ParseGenerationGuidFromFileName(
      const base::FilePath& full_name);
  static StatusOr<Priority> ParsePriorityFromQueueDirectory(
      const base::FilePath full_path, const StorageOptions& options);
};
}  // namespace reporting

#endif  // MISSIVE_STORAGE_STORAGE_UTIL_H_
