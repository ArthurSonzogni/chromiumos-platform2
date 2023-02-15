// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptorecovery/inclusion_proof.h"

#include <string>
#include <vector>

#include <absl/strings/numbers.h>
#include <base/base64.h>
#include <base/base64url.h>
#include <base/big_endian.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_tokenizer.h>
#include <base/strings/string_util.h>
#include <brillo/data_encoding.h>
#include <brillo/secure_blob.h>
#include <brillo/strings/string_utils.h>
#include <crypto/scoped_openssl_types.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <openssl/ec.h>
#include <openssl/x509.h>

#include "cryptohome/cryptorecovery/recovery_crypto_util.h"

namespace cryptohome {
namespace cryptorecovery {

namespace {

constexpr char kSigSplit[] = "\n\n";
constexpr char kNewline[] = "\n";
constexpr char kSigPrefix[] = "— ";
constexpr char kSigNameSplit[] = " ";

constexpr int kLeafHashPrefix = 0;
constexpr int kNodeHashPrefix = 1;
// The number of checkpoint note fields should be 2: the signaute and the text.
constexpr int kCheckpointNoteSize = 2;
// The number of checkpoint fields should be 3: origin, size, hash.
constexpr int kCheckpointSize = 3;
// Signature hash is defined as the first 4 bytes from signature string from the
// server.
constexpr int kSignatureHashSize = 4;
// This value is reflecting to the value from the server side.
constexpr int kMaxSignatureNumber = 100;

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

bool VerifySignature(const std::string& text,
                     const std::string& signatures,
                     const LedgerInfo& ledger_info) {
  int num_sig = 0;
  base::StringTokenizer tokenizer(signatures, kNewline);
  tokenizer.set_options(base::StringTokenizer::RETURN_DELIMS);

  while (tokenizer.GetNext()) {
    std::string signature_line = tokenizer.token();
    // Verify that the signature indeed ends with kNewline.
    if (!tokenizer.GetNext() || tokenizer.token() != kNewline) {
      LOG(ERROR) << "Failed to pull out one signature";
      return false;
    }
    num_sig++;

    // Avoid spending forever parsing a note with many signatures.
    if (num_sig > kMaxSignatureNumber)
      return false;

    if (!base::StartsWith(signature_line, kSigPrefix,
                          base::CompareCase::SENSITIVE)) {
      LOG(ERROR) << "No signature prefix is found.";
      return false;
    }

    // The ledger's name (signature_tokens[0]) could be parsed out with
    // separator of kSigNameSplit. And the signature and the key hash
    // (signature_tokens[1]) is located after kSigNameSplit.
    std::vector<std::string> signature_tokens = base::SplitString(
        signature_line.substr(strlen(kSigPrefix), signature_line.length()),
        kSigNameSplit, base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    if (signature_tokens.size() != 2) {
      LOG(ERROR) << "No signature name split is found.";
      return false;
    }
    std::string signature_str;
    if (!brillo::data_encoding::Base64Decode(signature_tokens[1],
                                             &signature_str)) {
      LOG(ERROR) << "Failed to convert base64 string to string.";
      return false;
    }

    // Determine which ledger public key to use, dev or prod, based on the
    // ledger's name and key hash.
    if (signature_str.length() < kSignatureHashSize) {
      LOG(ERROR) << "The length of the signature is not long enough.";
      return false;
    }
    uint32_t key_hash;
    base::ReadBigEndian(
        reinterpret_cast<const uint8_t*>(
            signature_str.substr(0, kSignatureHashSize).c_str()),
        &key_hash);
    if (ledger_info.name.empty()) {
      LOG(ERROR) << "Ledger name is empty.";
      return false;
    }
    if (ledger_info.public_key->empty()) {
      LOG(ERROR) << "Ledger public key is not present.";
      return false;
    }
    if (signature_tokens[0] != ledger_info.name ||
        key_hash != ledger_info.key_hash.value()) {
      LOG(ERROR) << "Unknown ledger key hash or name.";
      return false;
    }

    // Impoty Public key of PKIX, ASN.1 DER form to EC_KEY.
    std::string ledger_public_key_decoded;
    if (!base::Base64UrlDecode(ledger_info.public_key.value().to_string(),
                               base::Base64UrlDecodePolicy::IGNORE_PADDING,
                               &ledger_public_key_decoded)) {
      LOG(ERROR) << "Failed at decoding from url base64.";
      return false;
    }

    const unsigned char* asn1_ptr = reinterpret_cast<const unsigned char*>(
        ledger_public_key_decoded.c_str());
    crypto::ScopedEC_KEY public_key(
        d2i_EC_PUBKEY(nullptr, &asn1_ptr, ledger_public_key_decoded.length()));
    if (!public_key.get() || !EC_KEY_check_key(public_key.get())) {
      LOG(ERROR) << "Failed to decode ECC public key.";
      return false;
    }

    brillo::SecureBlob signature_hash =
        hwsec_foundation::Sha256(brillo::SecureBlob(text + kSigSplit[0]));
    signature_str = signature_str.substr(kSignatureHashSize);

    // Verify the signature and the hash.
    if (ECDSA_verify(
            0,
            reinterpret_cast<const unsigned char*>(signature_hash.char_data()),
            signature_hash.size(),
            reinterpret_cast<const unsigned char*>(signature_str.c_str()),
            signature_str.length(), public_key.get()) != 1) {
      return false;
    }
  }

  // Note had no verifiable signatures.
  if (num_sig == 0)
    return false;

  return true;
}

// ParseCheckpoint takes a raw checkpoint string and returns a parsed checkpoint
// and any otherData in the body, providing that:
// * a valid log signature is found; and
// * the checkpoint unmarshals correctly; and
// * the log origin is that expected.
// The signatures on the note will include the log signature if no error is
// returned, plus any signatures from otherVerifiers that were found.
bool ParseCheckPoint(std::string checkpoint_note_str,
                     const LedgerInfo& ledger_info,
                     Checkpoint* check_point) {
  std::vector<std::string> checkpoint_note_fields = brillo::string_utils::Split(
      checkpoint_note_str, kSigSplit, /*trim_whitespaces=*/false,
      /*purge_empty_strings=*/false);
  if (checkpoint_note_fields.size() != kCheckpointNoteSize) {
    LOG(ERROR) << "Checkpoint note is not valid.";
    return false;
  }

  if (!VerifySignature(checkpoint_note_fields[0], checkpoint_note_fields[1],
                       ledger_info)) {
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

bool VerifyInclusionProof(const LedgerSignedProof& ledger_signed_proof,
                          const LedgerInfo& ledger_info) {
  // Parse checkpoint note.
  Checkpoint check_point;
  if (!ParseCheckPoint(
          brillo::BlobToString(ledger_signed_proof.checkpoint_note),
          ledger_info, &check_point)) {
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
