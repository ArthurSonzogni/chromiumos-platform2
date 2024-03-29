// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_FILESYSTEM_VERIFIER_ACTION_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_FILESYSTEM_VERIFIER_ACTION_H_

#include <sys/stat.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include <brillo/streams/stream.h>

#include "update_engine/common/action.h"
#include "update_engine/common/hash_calculator.h"
#include "update_engine/payload_consumer/install_plan.h"
#include "update_engine/payload_consumer/verity_writer_interface.h"

// This action will hash all the partitions of the target slot involved in the
// update. The hashes are then verified against the ones in the InstallPlan.
// If the target hash does not match, the action will fail. In case of failure,
// the error code will depend on whether the source slot hashes are provided and
// match.

namespace chromeos_update_engine {

// The step FilesystemVerifier is on. On kVerifyTargetHash it computes the hash
// on the target partitions based on the already populated size and verifies it
// matches the one in the target_hash in the InstallPlan.
// If the hash matches, then we skip the kVerifySourceHash step, otherwise we
// need to check if the source is the root cause of the mismatch.
enum class VerifierStep {
  kVerifyTargetHash,
  kVerifySourceHash,
};

class FilesystemVerifyDelegate {
 public:
  virtual ~FilesystemVerifyDelegate() = default;
  virtual void OnVerifyProgressUpdate(double progress) = 0;
};

class FilesystemVerifierAction : public InstallPlanAction {
 public:
  explicit FilesystemVerifierAction(
      DynamicPartitionControlInterface* dynamic_control)
      : verity_writer_(verity_writer::CreateVerityWriter()),
        dynamic_control_(dynamic_control) {
    CHECK(dynamic_control_);
  }
  FilesystemVerifierAction(const FilesystemVerifierAction&) = delete;
  FilesystemVerifierAction& operator=(const FilesystemVerifierAction&) = delete;

  ~FilesystemVerifierAction() override = default;

  void PerformAction() override;
  void TerminateProcessing() override;

  // Used for listening to progress updates
  void set_delegate(FilesystemVerifyDelegate* delegate) {
    this->delegate_ = delegate;
  }
  [[nodiscard]] FilesystemVerifyDelegate* get_delegate() const {
    return this->delegate_;
  }

  // Debugging/logging
  static std::string StaticType() { return "FilesystemVerifierAction"; }
  std::string Type() const override { return StaticType(); }

 private:
  friend class FilesystemVerifierActionTestDelegate;
  // Starts the hashing of the current partition. If there aren't any partitions
  // remaining to be hashed, it finishes the action.
  void StartPartitionHashing();

  // Schedules the asynchronous read of the filesystem.
  void ScheduleRead();

  // Called from the main loop when a single read from |src_stream_| succeeds or
  // fails, calling OnReadDoneCallback() and OnReadErrorCallback() respectively.
  void OnReadDoneCallback(size_t bytes_read);
  void OnReadErrorCallback(const brillo::Error* error);

  // When the read is done, finalize the hash checking of the current partition
  // and continue checking the next one.
  void FinishPartitionHashing();

  // Cleans up all the variables we use for async operations and tells the
  // ActionProcessor we're done w/ |code| as passed in. |cancelled_| should be
  // true if TerminateProcessing() was called.
  void Cleanup(ErrorCode code);

  // Invoke delegate callback to report progress, if delegate is not null
  void UpdateProgress(double progress);

  // The type of the partition that we are verifying.
  VerifierStep verifier_step_ = VerifierStep::kVerifyTargetHash;

  // The index in the install_plan_.partitions vector of the partition currently
  // being hashed.
  size_t partition_index_{0};

  // If not null, the FileStream used to read from the device.
  brillo::StreamPtr src_stream_;

  // Buffer for storing data we read.
  brillo::Blob buffer_;

  bool cancelled_{false};  // true if the action has been cancelled.

  // Calculates the hash of the data.
  std::unique_ptr<HashCalculator> hasher_;

  // Write verity data of the current partition.
  std::unique_ptr<VerityWriterInterface> verity_writer_;

  // Verifies the untouched dynamic partitions for partial updates.
  DynamicPartitionControlInterface* dynamic_control_{nullptr};

  // Reads and hashes this many bytes from the head of the input stream. When
  // the partition starts to be hashed, this field is initialized from the
  // corresponding InstallPlan::Partition size which is the total size
  // update_engine is expected to write, and may be smaller than the size of the
  // partition in gpt.
  uint64_t partition_size_{0};

  // The byte offset that we are reading in the current partition.
  uint64_t offset_{0};

  // An observer that observes progress updates of this action.
  FilesystemVerifyDelegate* delegate_{};
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_FILESYSTEM_VERIFIER_ACTION_H_
