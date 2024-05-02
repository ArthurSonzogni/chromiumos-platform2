// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_PAYLOAD_CONSTANTS_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_PAYLOAD_CONSTANTS_H_

#include <stdint.h>

#include <limits>

#include "update_engine/update_metadata.pb.h"

namespace chromeos_update_engine {

// The major version used by Chrome OS.
// extern const uint64_t kChromeOSMajorPayloadVersion;  DEPRECATED

// The major version used by Brillo.
extern const uint64_t kBrilloMajorPayloadVersion;

// The minimum and maximum supported major version.
extern const uint64_t kMinSupportedMajorPayloadVersion;
extern const uint64_t kMaxSupportedMajorPayloadVersion;

// The minor version used for all full payloads.
extern const uint32_t kFullPayloadMinorVersion;

// The minor version used by the in-place delta generator algorithm.
// extern const uint32_t kInPlaceMinorPayloadVersion;  DEPRECATED

// The minor version used by the A to B delta generator algorithm.
extern const uint32_t kSourceMinorPayloadVersion;

// The minor version that allows per-operation source hash.
extern const uint32_t kOpSrcHashMinorPayloadVersion;

// The minor version that allows BROTLI_BSDIFF, ZERO and DISCARD operation.
extern const uint32_t kBrotliBsdiffMinorPayloadVersion;

// The minor version that allows PUFFDIFF operation.
extern const uint32_t kPuffdiffMinorPayloadVersion;

// The minor version that allows Verity hash tree and FEC generation.
extern const uint32_t kVerityMinorPayloadVersion;

// The minor version that allows partial update, e.g. kernel only update.
extern const uint32_t kPartialUpdateMinorPayloadVersion;

// The minor version that allows REPLACE_ZSTD operation.
extern const uint32_t kReplaceZstdMinorPayloadVersion;

// The minor version that allows REPLACE_ZSTD_INCREASED_WINDOW
// operation.
extern const uint32_t kReplaceZstdIncreasedWindowMinorPayloadVersion;

// The minimum and maximum supported minor version.
extern const uint32_t kMinSupportedMinorPayloadVersion;
extern const uint32_t kMaxSupportedMinorPayloadVersion;

// The maximum size of the payload header (anything before the protobuf).
extern const uint64_t kMaxPayloadHeaderSize;

// The kernel and rootfs partition names used by the BootControlInterface when
// handling update payloads with a major version 1. The names of the updated
// partitions are include in the payload itself for major version 2.
extern const char kPartitionNameKernel[];
extern const char kPartitionNameRoot[];

extern const char kBspatchPath[];
extern const char kDeltaMagic[4];

// A block number denoting a hole on a sparse file. Used on Extents to refer to
// section of blocks not present on disk on a sparse file.
const uint64_t kSparseHole = std::numeric_limits<uint64_t>::max();

// Return the name of the operation type.
const char* InstallOperationTypeName(InstallOperation::Type op_type);

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_PAYLOAD_CONSTANTS_H_
