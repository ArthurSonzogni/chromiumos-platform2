// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/fuzzers/blob_mutator.h"

#include <stdint.h>

#include <algorithm>
#include <utility>

#include <base/check_op.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>

using brillo::Blob;

namespace {

// The "commands" that the MutateBlob() function uses for enterpreting the
// fuzzer input and performing the mutations it implements.
enum class BlobMutatorCommand {
  kCopyInputByte,
  kSkipInputByte,
  kAddNewByte,
  kEnd,
  kEndWithCopyingRestOfInput,
};

// Interprets the byte(s) from the fuzzed data provider as a mutation command.
// |consumed_byte_count| is populated with 1 or 2 - the number of bytes consumed
// from the fuzzed data provider. When returning |kAddNewByte|, |to_add| is
// assigned to the value to be added.
//
// The algorithm implemented in this function allows the following easy
// transformations:
// * input_blob="foo", fuzzed_data_provider=\ \ \ ==> "foo";
// * input_blob="foo", fuzzed_data_provider=\ 0x1 ==> "foo";
// * input_blob="foo", fuzzed_data_provider=b a r ==> "bar";
// * input_blob="foo", fuzzed_data_provider=\ \ b a r \ ==> "fobaro";
// * input_blob="foo", fuzzed_data_provider=\ 0x2 \ \ ==> "oo";
// etc.
BlobMutatorCommand ReadCommandFromFuzzedData(uint8_t current_byte,
                                             uint8_t next_byte,
                                             int* consumed_byte_count,
                                             uint8_t* to_add) {
  if (current_byte != '\\') {
    *consumed_byte_count = 1;
    *to_add = current_byte;
    return BlobMutatorCommand::kAddNewByte;
  }
  if (next_byte == 0) {
    *consumed_byte_count = 2;
    return BlobMutatorCommand::kEnd;
  }
  if (next_byte == 1) {
    *consumed_byte_count = 2;
    return BlobMutatorCommand::kEndWithCopyingRestOfInput;
  }
  if (next_byte == 2) {
    *consumed_byte_count = 2;
    return BlobMutatorCommand::kSkipInputByte;
  }
  if (next_byte == 3) {
    // This case allows the fuzzer to insert the backslash character. Note that
    // this is only needed for the backslash, since it has a special meaning.
    *consumed_byte_count = 2;
    *to_add = '\\';
    return BlobMutatorCommand::kAddNewByte;
  }
  if (next_byte == 4) {
    // This case is similar to below, but allows the fuzzer to insert a new byte
    // with the value from [0; 3] after a segment of bytes copied from the
    // input. This is because the backslash followed by such a value has a
    // special meaning handled above.
    *consumed_byte_count = 2;
    return BlobMutatorCommand::kCopyInputByte;
  }
  // This case is similar to above, but without consuming the character that
  // follows the backslash. This allows the fuzzer to use a compact form
  // "...\\\\\\..." in order to represent a segment copied from the input blob.
  *consumed_byte_count = 1;
  return BlobMutatorCommand::kCopyInputByte;
}

}  // namespace

Blob MutateBlob(const Blob& input_blob,
                int max_length,
                FuzzedDataProvider* fuzzed_data_provider) {
  // Begin with an empty result blob. The code below will fill it with data,
  // according to the parsed "commands".
  Blob fuzzed_blob;
  fuzzed_blob.reserve(max_length);
  int input_index = 0;
  uint8_t next_byte = fuzzed_data_provider->ConsumeIntegral<uint8_t>();
  do {
    uint8_t current_byte = next_byte;
    next_byte = fuzzed_data_provider->ConsumeIntegral<uint8_t>();

    int consumed_byte_count;
    uint8_t to_add;
    BlobMutatorCommand command = ReadCommandFromFuzzedData(
        current_byte, next_byte, &consumed_byte_count, &to_add);
    switch (command) {
      case BlobMutatorCommand::kCopyInputByte: {
        if (input_index < input_blob.size() &&
            fuzzed_blob.size() < max_length) {
          fuzzed_blob.push_back(input_blob[input_index]);
          ++input_index;
        }
        break;
      }
      case BlobMutatorCommand::kSkipInputByte: {
        if (input_index < input_blob.size())
          ++input_index;
        break;
      }
      case BlobMutatorCommand::kAddNewByte: {
        if (fuzzed_blob.size() < max_length)
          fuzzed_blob.push_back(to_add);
        break;
      }
      case BlobMutatorCommand::kEnd: {
        return fuzzed_blob;
      }
      case BlobMutatorCommand::kEndWithCopyingRestOfInput: {
        const int bytes_to_copy = std::min(input_blob.size() - input_index,
                                           max_length - fuzzed_blob.size());
        fuzzed_blob.insert(fuzzed_blob.end(), input_blob.begin() + input_index,
                           input_blob.begin() + input_index + bytes_to_copy);
        return fuzzed_blob;
      }
    }

    if (consumed_byte_count == 2) {
      // If the current command consumed both bytes from the data provider,
      // extract one more for the next command.
      next_byte = fuzzed_data_provider->ConsumeIntegral<uint8_t>();
    }
  } while (fuzzed_data_provider->remaining_bytes());
  return fuzzed_blob;
}
