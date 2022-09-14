// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptorecovery/inclusion_proof.h"

#include <string>
#include <vector>

#include <absl/strings/numbers.h>
#include <base/strings/string_util.h>
#include "base/strings/string_number_conversions.h"
#include <brillo/data_encoding.h>
#include <brillo/strings/string_utils.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/sha.h>

#include "cryptohome/cryptorecovery/recovery_crypto_util.h"

namespace cryptohome {
namespace cryptorecovery {

namespace {

constexpr char kSigSplit[] = "\n\n";
constexpr char kNewline[] = "\n";
constexpr int kLeafHashPrefix = 0;
constexpr int kNodeHashPrefix = 1;
// The number of checkpoint note fields should be 2: the signaute and the text.
constexpr int kCheckpointNoteSize = 2;
// The number of checkpoint fields should be 3: origin, size, hash.
constexpr int kCheckpointSize = 3;

// Checkpoint represents a minimal log checkpoint (STH).
struct Checkpoint {
  // Origin is the string identifying the log which issued this checkpoint.
  std::string origin;
  // Size is the number of entries in the log at this checkpoint.
  int64_t size;
  // Hash is the hash which commits to the contents of the entire log.
  brillo::Blob hash;
};

// CalculateInnerProofSize breaks down inclusion proof for a leaf at the
// specified |index| in a tree of the specified |size| into 2 components. The
// splitting point between them is where paths to leaves |index| and |size-1|
// diverge. Returns lengths of the bottom proof parts.
int CalculateInnerProofSize(int index, int size) {
  DCHECK_GT(index, -1);
  DCHECK_GT(size, 0);
  int xor_number = index ^ (size - 1);
  int bits_number = 0;
  while (xor_number > 0) {
    xor_number = xor_number / 2;
    bits_number++;
  }
  return bits_number;
}

// HashLeaf computes the hash of a leaf that exists.
brillo::Blob HashLeaf(const brillo::Blob& leaf_text) {
  brillo::Blob prefix;
  prefix.push_back(kLeafHashPrefix);
  return hwsec_foundation::Sha256(brillo::CombineBlobs({prefix, leaf_text}));
}

// HashChildren computes interior nodes.
brillo::Blob HashChildren(const brillo::Blob& left, const brillo::Blob& right) {
  brillo::Blob prefix;
  prefix.push_back(kNodeHashPrefix);
  return hwsec_foundation::Sha256(brillo::CombineBlobs({prefix, left, right}));
}

bool VerifySignature(std::string signature) {
  // TODO(b:232747549): Verify the signature.
  return true;
}

// ParseCheckpoint takes a raw checkpoint string and returns a parsed checkpoint
// and any otherData in the body, providing that:
// * a valid log signature is found; and
// * the checkpoint unmarshals correctly; and
// * the log origin is that expected.
// The signatures on the note will include the log signature if no error is
// returned, plus any signatures from otherVerifiers that were found.
bool ParseCheckPoint(std::string checkpoint_note_str, Checkpoint* check_point) {
  std::vector<std::string> checkpoint_note_fields =
      brillo::string_utils::Split(checkpoint_note_str, kSigSplit);
  if (checkpoint_note_fields.size() != kCheckpointNoteSize) {
    LOG(ERROR) << "Checkpoint note is not valid.";
    return false;
  }

  if (!VerifySignature(checkpoint_note_fields[1])) {
    LOG(ERROR) << "Failed to verify the signature of the checkpoint note.";
    return false;
  }
  std::vector<std::string> checkpoint_fields =
      brillo::string_utils::Split(checkpoint_note_fields[0], kNewline);
  if (checkpoint_fields.size() != kCheckpointSize) {
    LOG(ERROR) << "Checkpoint is not valid.";
    return false;
  }

  check_point->origin = checkpoint_fields[0];
  if (!base::StringToInt64(checkpoint_fields[1], &check_point->size)) {
    LOG(ERROR) << "Failed to convert string to int64_t";
    return false;
  }
  if (check_point->size < 1) {
    LOG(ERROR) << "Checkpoint is not valid.";
    return false;
  }
  std::string check_point_hash_str;
  if (!brillo::data_encoding::Base64Decode(checkpoint_fields[2],
                                           &check_point_hash_str)) {
    LOG(ERROR) << "Failed to convert base64 string to string.";
    return false;
  }
  check_point->hash = brillo::BlobFromString(check_point_hash_str);
  return true;
}

// CalculateRootHash calculates the expected root hash for a tree of the
// given size, provided a leaf index and hash with the corresponding inclusion
// proof. Requires 0 <= index < size.
bool CalculateRootHash(const brillo::Blob& leaf_hash,
                       const std::vector<brillo::Blob>& inclusion_proof,
                       int64_t leaf_index,
                       int64_t size,
                       brillo::Blob* root_hash) {
  if (leaf_index < 0 || size < 1) {
    LOG(ERROR) << "Leaf index or inclusion proof size is not valid.";
    return false;
  }

  int64_t index = 0;
  int inner_proof_size = CalculateInnerProofSize(leaf_index, /*size=*/size);
  if (inner_proof_size > inclusion_proof.size()) {
    LOG(ERROR) << "Calculated inner proof size is not valid.";
    return false;
  }

  brillo::Blob seed = leaf_hash;
  while (index < inner_proof_size) {
    if (((leaf_index >> index) & 1) == 0) {
      seed = HashChildren(seed, inclusion_proof[index]);
    } else {
      seed = HashChildren(inclusion_proof[index], seed);
    }
    index++;
  }

  while (index < inclusion_proof.size()) {
    seed = HashChildren(inclusion_proof[index], seed);
    index++;
  }

  *root_hash = seed;

  return true;
}

}  // namespace

bool VerifyInclusionProof(const LedgerSignedProof& ledger_signed_proof) {
  // Parse checkpoint note.
  Checkpoint check_point;
  if (!ParseCheckPoint(
          brillo::BlobToString(ledger_signed_proof.checkpoint_note),
          &check_point)) {
    LOG(ERROR) << "Failed to parse checkpoint note.";
    return false;
  }

  // Calculate tree root.
  brillo::Blob calculated_root_hash;
  if (!CalculateRootHash(
          HashLeaf(ledger_signed_proof.logged_record.public_ledger_entry),
          ledger_signed_proof.inclusion_proof,
          ledger_signed_proof.logged_record.leaf_index, check_point.size,
          &calculated_root_hash)) {
    LOG(ERROR) << "Failed to calculate root hash.";
    return false;
  }

  // Verify if the root hash is as expected.
  return calculated_root_hash == check_point.hash;
}

}  // namespace cryptorecovery
}  // namespace cryptohome
