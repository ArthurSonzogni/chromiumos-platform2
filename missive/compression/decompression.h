// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "missive/proto/record.pb.h"

#ifndef MISSIVE_COMPRESSION_DECOMPRESSION_H_
#define MISSIVE_COMPRESSION_DECOMPRESSION_H_

namespace reporting::test {

// DecompressRecord will decompress the provided |record| and respond
// with the callback. The compression_information provided will determine
// which compression algorithm is used. On success the returned std::string
// sink will contain a decompressed EncryptedWrappedRecord string. The sink
// string then can be further updated by the caller. std::string is used
// instead of base::StringPiece because ownership is taken of |record| through
// std::move(record).
[[nodiscard]] std::string DecompressRecord(
    std::string record, CompressionInformation compression_information);

}  // namespace reporting::test

#endif  // MISSIVE_COMPRESSION_DECOMPRESSION_H_
