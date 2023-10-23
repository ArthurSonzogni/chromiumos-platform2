// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_STORAGE_STORAGE_UTIL_H_
#define MISSIVE_STORAGE_STORAGE_UTIL_H_

#include <string>
#include <tuple>
#include <unordered_set>

#include <base/files/file_path.h>

#include "missive/storage/storage_configuration.h"
#include "missive/util/statusor.h"

namespace reporting {

// StorageDirectory is a non-thread-safe static class that executes operations
// on a `Storage` directory that contains `StorageQueue` directories.
class StorageDirectory {
 public:
  struct Hash {
    size_t operator()(
        const std::tuple<Priority, GenerationGuid>& v) const noexcept {
      const auto& [priority, guid] = v;
      static constexpr std::hash<Priority> priority_hasher;
      static constexpr std::hash<GenerationGuid> guid_hasher;
      return priority_hasher(priority) ^ guid_hasher(guid);
    }
  };
  using Set = std::unordered_set<std::tuple<Priority, GenerationGuid>, Hash>;

  // Returns a set of <Priority, GenerationGuid> tuples corresponding to valid
  // queue directories found in the storage directory provided in `options`. For
  // legacy directories, GenerationGuid will be empty.
  static Set FindQueueDirectories(
      const base::FilePath& storage_directory,
      const StorageOptions::QueuesOptionsList& options_list);

  // Deletes all multigenerational queue directories in `storage_directory` that
  // contain no unconfirmed records. Returns false on error. Returns true
  // otherwise.
  static bool DeleteEmptyMultigenerationQueueDirectories(
      const base::FilePath& directory);

 private:
  // Returns priority/generation guid tuple from a filepath, or error status.
  static StatusOr<std::tuple<Priority, GenerationGuid>>
  GetPriorityAndGenerationGuid(
      const base::FilePath& full_name,
      const StorageOptions::QueuesOptionsList& options_list);

  // Returns generation guid from a filepath, or error status.
  static StatusOr<GenerationGuid> ParseGenerationGuidFromFilePath(
      const base::FilePath& full_name);

  // Return priority from a filepath, or error status.
  static StatusOr<Priority> ParsePriorityFromQueueDirectory(
      const base::FilePath& full_path,
      const StorageOptions::QueuesOptionsList& options_list);

  // Returns true if the filepath matches the format of a metadata file. Returns
  // false otherwise.
  static bool IsMetaDataFile(const base::FilePath& filepath);

  // Returns false if `queue_directory` contains records that have not been
  // confirmed by the server. Returns true otherwise.
  static bool QueueDirectoryContainsNoUnconfirmedRecords(
      const base::FilePath& queue_directory);
};
}  // namespace reporting

#endif  // MISSIVE_STORAGE_STORAGE_UTIL_H_
