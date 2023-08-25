// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_PINWEAVER_MANAGER_SYNC_HASH_TREE_TYPES_H_
#define LIBHWSEC_BACKEND_PINWEAVER_MANAGER_SYNC_HASH_TREE_TYPES_H_

namespace hwsec {

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused since the status is reported as UMA.
enum class SyncOutcome {
  kStateNotReady = 0,
  kSuccessAfterLocalReconstruct = 1,
  kGetLogFailed = 2,
  kLogReplay = 3,
  kMaxValue = kLogReplay,
};

enum class LogReplayResult {
  kSuccess = 0,
  kInvalidLogEntry = 1,
  kOperationFailed = 2,
  kRemoveInsertedCredentialsError = 3,
  kMaxValue = kRemoveInsertedCredentialsError,
};

// This is a per-entry specification. It's similar to the LogReplayType enum,
// but kFullReplay is further splitted into kMismatchedHash and kSecondEntry.
// NOTE: the definition is based on our current pinweaver backend implementation
// has LogSize=2.
enum class ReplayEntryType {
  // There's only one entry need to be replayed, and the hash before the
  // operation can be found in the log.
  kNormal = 0,
  // The hash cannot be found in the log. Trying to replay the first operation
  // in the log anyway and hope that the resulting hash matches the log.
  // Replayes of this type is expected to have high failing rate.
  kMismatchedHash = 1,
  // The is a replay following a success kMismatchedHash replay. It is almost
  // same as the kNormal case, but it'll fail ReplayCheck on the label that was
  // just inserted in the previous kMismatchedHash replay.
  kSecondEntry = 2,
  kMaxValue = kSecondEntry,
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_PINWEAVER_MANAGER_SYNC_HASH_TREE_TYPES_H_
