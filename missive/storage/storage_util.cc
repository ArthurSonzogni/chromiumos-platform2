// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/storage_util.h"

#include <string>
#include <tuple>

#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/uuid.h>

#include "missive/storage/storage_queue.h"
#include "missive/util/file.h"
#include "missive/util/status.h"

namespace reporting {

// static
StorageDirectory::Set StorageDirectory::FindQueueDirectories(
    const StorageOptions& options) {
  Set queue_params;
  base::FileEnumerator dir_enum(options.directory(),
                                /*recursive=*/false,
                                base::FileEnumerator::DIRECTORIES);
  for (auto full_name = dir_enum.Next(); !full_name.empty();
       full_name = dir_enum.Next()) {
    if (const auto priority_result =
            ParsePriorityFromQueueDirectory(full_name, options);
        priority_result.ok() && full_name.Extension().empty()) {
      // This is a legacy queue directory named just by priority with no
      // generation guid as an extension: foo/bar/Security,
      // foo/bar/FastBatch, etc.
      queue_params.emplace(
          std::make_tuple(priority_result.ValueOrDie(), GenerationGuid()));
      LOG(INFO) << "Found legacy queue directory: " << full_name;
    } else if (auto queue_param =
                   GetPriorityAndGenerationGuid(full_name, options);
               queue_param.ok()) {
      queue_params.emplace(queue_param.ValueOrDie());
    } else {
      LOG(INFO) << "Could not parse queue parameters from filename "
                << full_name.MaybeAsASCII()
                << " error = " << queue_param.status();
    }
  }
  return queue_params;
}

// static
StatusOr<std::tuple<Priority, GenerationGuid>>
StorageDirectory::GetPriorityAndGenerationGuid(const base::FilePath& full_name,
                                               const StorageOptions& options) {
  // Try to parse generation guid from file path
  const auto generation_guid = ParseGenerationGuidFromFilePath(full_name);
  if (!generation_guid.ok()) {
    return generation_guid.status();
  }
  // Try to parse a priority from file path
  const auto priority = ParsePriorityFromQueueDirectory(full_name, options);
  if (!priority.ok()) {
    return priority.status();
  }
  return std::make_tuple(priority.ValueOrDie(), generation_guid.ValueOrDie());
}

// static
StatusOr<GenerationGuid> StorageDirectory::ParseGenerationGuidFromFilePath(
    const base::FilePath& full_name) {
  // The string returned by `Extension()` includes the leading period, i.e
  // ".txt" instead of "txt", so remove the period just get the text part of
  // the extension.
  if (full_name.Extension().empty()) {
    return Status(
        error::DATA_LOSS,
        base::StrCat({"Could not parse generation GUID from queue directory ",
                      full_name.MaybeAsASCII()}));
  }

  const std::string extension_without_leading_period =
      full_name.Extension().substr(1);

  const auto generation_guid =
      base::Uuid::ParseCaseInsensitive(extension_without_leading_period);
  if (!generation_guid.is_valid()) {
    return Status(
        error::DATA_LOSS,
        base::StrCat({"Could not parse generation GUID from queue directory ",
                      full_name.MaybeAsASCII()}));
  }
  return generation_guid.AsLowercaseString();
}

// static
StatusOr<Priority> StorageDirectory::ParsePriorityFromQueueDirectory(
    const base::FilePath& full_path, const StorageOptions& options) {
  for (const auto& priority_queue_options_pair :
       options.ProduceQueuesOptionsList()) {
    if (priority_queue_options_pair.second.directory() ==
        full_path.RemoveExtension()) {
      return priority_queue_options_pair.first;
    }
  }
  return Status(error::NOT_FOUND,
                base::StrCat({"Found no priority for queue directory ",
                              full_path.MaybeAsASCII()}));
}

// static
bool StorageDirectory::IsMetaDataFile(const base::FilePath& filepath) {
  const auto found = filepath.BaseName().MaybeAsASCII().find(
      StorageQueue::kMetadataFileNamePrefix);
  return found != std::string::npos;
}

// static
bool StorageDirectory::QueueDirectoryContainsNoUnconfirmedRecords(
    const base::FilePath& queue_directory) {
  base::FileEnumerator queue_dir_enum(queue_directory,
                                      /*recursive=*/false,
                                      base::FileEnumerator::FILES);

  for (base::FilePath entry = queue_dir_enum.Next(); !entry.empty();
       entry = queue_dir_enum.Next()) {
    if (!IsMetaDataFile(entry) && queue_dir_enum.GetInfo().GetSize() > 0) {
      // It's a record that has not been confirmed.
      return false;
    }
  }
  return true;
}

// static
bool StorageDirectory::DeleteEmptyMultigenerationQueueDirectories(
    const base::FilePath& storage_directory) {
  base::FileEnumerator dir_enum(storage_directory,
                                /*recursive=*/false,
                                base::FileEnumerator::DIRECTORIES);

  const bool executed_without_error = DeleteFilesWarnIfFailed(
      dir_enum, base::BindRepeating([](const base::FilePath& queue_directory) {
        bool should_delete_queue_directory =
            ParseGenerationGuidFromFilePath(queue_directory).ok() &&
            QueueDirectoryContainsNoUnconfirmedRecords(queue_directory);

        if (!should_delete_queue_directory) {
          return false;
        }

        LOG(INFO) << "Attempting to delete multigenerational queue directory "
                  << queue_directory.MaybeAsASCII();

        const bool deleted_queue_files_successfully =
            DeleteFilesWarnIfFailed(base::FileEnumerator(
                queue_directory, false, base::FileEnumerator::FILES));

        LOG_IF(ERROR, !deleted_queue_files_successfully)
            << "Cannot delete queue directory "
            << queue_directory.MaybeAsASCII()
            << ". Failed to delete files within directory.";

        return deleted_queue_files_successfully;
      }));

  LOG_IF(ERROR, !executed_without_error)
      << "Error occurred while deleting queue directories";

  return executed_without_error;
}
}  // namespace reporting
