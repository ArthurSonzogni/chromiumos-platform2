// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_ANALYTICS_RESOURCE_COLLECTOR_STORAGE_H_
#define MISSIVE_ANALYTICS_RESOURCE_COLLECTOR_STORAGE_H_

#include <string_view>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <base/sequence_checker.h>
#include <base/thread_annotations.h>
#include <base/time/time.h>
#include <gtest/gtest_prod.h>

#include "missive/analytics/resource_collector.h"

namespace reporting {

// Forward declarations for `friend class` directives.
class MissiveArgsTest;
class MissiveImplTest;

namespace analytics {

class ResourceCollectorStorage : public ResourceCollector {
 public:
  ResourceCollectorStorage(base::TimeDelta interval,
                           const base::FilePath& storage_directory);
  ~ResourceCollectorStorage() override;

  // Update upload progress timestamp. Reset every time the device makes
  // progress uploading events.
  static void RecordUploadProgress(
      base::WeakPtr<ResourceCollectorStorage> self);

  // Retrieve weak pointer.
  base::WeakPtr<ResourceCollectorStorage> GetWeakPtr();

 private:
  friend class ::reporting::MissiveArgsTest;
  friend class ::reporting::MissiveImplTest;
  friend class ResourceCollectorStorageTest;
  FRIEND_TEST(ResourceCollectorStorageTest, SuccessfullySend);

  // UMA names
  static constexpr char kUmaName[] = "Platform.Missive.StorageUsage";
  static constexpr char kNonUploadingUmaName[] =
      "Platform.Missive.StorageUsageNonUploading";
  // The min of the storage usage in MiB that we are collecting: 1MiB
  static constexpr int kMin = 1;
  // The max of the storage usage in MiB that we are collecting: 301MiB.
  // Slightly larger than the limit we have to detect possible over usage.
  static constexpr int kMax = 301;
  // number of UMA buckets. Buckets are exponentially binned. Fixed to the
  // default in Chrome (50).
  static constexpr int kUmaNumberOfBuckets = 50;

  // Convert bytes into MiBs.
  static int ConvertBytesToMibs(int bytes);

  // Collect storage usage. This is not obtained from the memory resource
  // management in Missive. Rather, the storage directory is scanned once for
  // each fixed time interval as this method is called (see comments for
  // |ResourceCollector::Collect|).
  void Collect() override;

  // Send directory size data to UMA.
  bool SendDirectorySizeToUma(std::string_view uma_name, int directory_size);

  // The directory in which record files are saved.
  const base::FilePath storage_directory_;

  // Upload progress time stamp.
  base::Time upload_progress_timestamp_ GUARDED_BY_CONTEXT(sequence_checker_){
      base::Time::Now()};

  // Weak pointer factory for delayed callbacks.
  base::WeakPtrFactory<ResourceCollectorStorage> weak_ptr_factory_{this};
};

}  // namespace analytics
}  // namespace reporting

#endif  // MISSIVE_ANALYTICS_RESOURCE_COLLECTOR_STORAGE_H_
