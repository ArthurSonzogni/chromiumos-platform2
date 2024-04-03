// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_consumer/payload_constants.h"

#if BASE_VER < 822064
#include <base/logging.h>
#else
#include <base/notreached.h>
#endif

namespace chromeos_update_engine {

// const uint64_t kChromeOSMajorPayloadVersion = 1;  DEPRECATED
const uint64_t kBrilloMajorPayloadVersion = 2;

const uint64_t kMinSupportedMajorPayloadVersion = kBrilloMajorPayloadVersion;
const uint64_t kMaxSupportedMajorPayloadVersion = kBrilloMajorPayloadVersion;

const uint32_t kFullPayloadMinorVersion = 0;
// const uint32_t kInPlaceMinorPayloadVersion = 1;  DEPRECATED
const uint32_t kSourceMinorPayloadVersion = 2;
const uint32_t kOpSrcHashMinorPayloadVersion = 3;
const uint32_t kBrotliBsdiffMinorPayloadVersion = 4;
const uint32_t kPuffdiffMinorPayloadVersion = 5;
const uint32_t kVerityMinorPayloadVersion = 6;
const uint32_t kPartialUpdateMinorPayloadVersion = 7;
const uint32_t kReplaceZstdMinorPayloadVersion = 8;

const uint32_t kMinSupportedMinorPayloadVersion = kSourceMinorPayloadVersion;
const uint32_t kMaxSupportedMinorPayloadVersion =
    kReplaceZstdMinorPayloadVersion;

const uint64_t kMaxPayloadHeaderSize = 24;

const char kPartitionNameKernel[] = "kernel";
const char kPartitionNameRoot[] = "root";

const char kDeltaMagic[4] = {'C', 'r', 'A', 'U'};

const char* InstallOperationTypeName(InstallOperation::Type op_type) {
  switch (op_type) {
    case InstallOperation::REPLACE:
      return "REPLACE";
    case InstallOperation::REPLACE_BZ:
      return "REPLACE_BZ";
    case InstallOperation::SOURCE_COPY:
      return "SOURCE_COPY";
    case InstallOperation::SOURCE_BSDIFF:
      return "SOURCE_BSDIFF";
    case InstallOperation::ZERO:
      return "ZERO";
    case InstallOperation::DISCARD:
      return "DISCARD";
    case InstallOperation::REPLACE_XZ:
      return "REPLACE_XZ";
    case InstallOperation::PUFFDIFF:
      return "PUFFDIFF";
    case InstallOperation::BROTLI_BSDIFF:
      return "BROTLI_BSDIFF";
    case InstallOperation::REPLACE_ZSTD:
      return "REPLACE_ZSTD";
#ifndef __CHROMEOS__
    case InstallOperation::BSDIFF:
    case InstallOperation::MOVE:
      NOTREACHED();
#endif
  }
  return "<unknown_op>";
}

};  // namespace chromeos_update_engine
