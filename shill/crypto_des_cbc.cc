// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/crypto_des_cbc.h"

#include <rpc/des_crypt.h>

#include <base/file_util.h>
#include <base/string_util.h>

#include "shill/glib.h"

using base::FilePath;
using std::string;
using std::vector;

namespace shill {

const unsigned int CryptoDESCBC::kBlockSize = 8;
const char CryptoDESCBC::kID[] = "des-cbc";
const char CryptoDESCBC::kSentinel[] = "[ok]";
const char CryptoDESCBC::kVersion2Prefix[] = "02:";

CryptoDESCBC::CryptoDESCBC(GLib *glib) : glib_(glib) {}

string CryptoDESCBC::GetID() {
  return kID;
}

bool CryptoDESCBC::Encrypt(const string &plaintext, string *ciphertext) {
  // Never encrypt. We'll fall back to rot47 which doesn't depend on
  // the owner key which may change due to rotation.
  return false;
}

bool CryptoDESCBC::Decrypt(const string &ciphertext, string *plaintext) {
  CHECK_EQ(kBlockSize, key_.size());
  CHECK_EQ(kBlockSize, iv_.size());
  int version = 1;
  string b64_ciphertext = ciphertext;
  if (StartsWithASCII(ciphertext, kVersion2Prefix, true)) {
    version = 2;
    b64_ciphertext.erase(0, strlen(kVersion2Prefix));
  }

  string decoded_data;
  if (!glib_->B64Decode(b64_ciphertext, &decoded_data)) {
    LOG(ERROR) << "Unable to base64-decode DEC-CBC ciphertext.";
    return false;
  }

  vector<char> data(decoded_data.c_str(),
                    decoded_data.c_str() + decoded_data.length());
  if (data.empty() || (data.size() % kBlockSize != 0)) {
    LOG(ERROR) << "Invalid DES-CBC ciphertext size: " << data.size();
    return false;
  }

  // The IV is modified in place.
  vector<char> iv = iv_;
  int rv =
      cbc_crypt(key_.data(), data.data(), data.size(), DES_DECRYPT, iv.data());
  if (DES_FAILED(rv)) {
    LOG(ERROR) << "DES-CBC decryption failed.";
    return false;
  }
  if (data.back() != '\0') {
    LOG(ERROR) << "DEC-CBC decryption resulted in invalid plain text.";
    return false;
  }
  string text = data.data();
  if (version == 2) {
    if (!EndsWith(text, kSentinel, true)) {
      LOG(ERROR) << "DES-CBC decrypted text missing sentinel -- bad key?";
      return false;
    }
    text.erase(text.size() - strlen(kSentinel), strlen(kSentinel));
  }
  *plaintext = text;
  return true;
}

bool CryptoDESCBC::LoadKeyMatter(const FilePath &path) {
  key_.clear();
  iv_.clear();
  string matter;
  // TODO(petkov): This mimics current flimflam behavior. Fix it so that it
  // doesn't read the whole file.
  if (!file_util::ReadFileToString(path, &matter)) {
    LOG(ERROR) << "Unable to load key matter from " << path.value();
    return false;
  }
  if (matter.size() < 2 * kBlockSize) {
    LOG(ERROR) << "Key matter data not enough " << matter.size() << " < "
               << 2 * kBlockSize;
    return false;
  }
  string::const_iterator matter_start =
      matter.begin() + (matter.size() - 2 * kBlockSize);
  key_.assign(matter_start + kBlockSize, matter_start + 2 * kBlockSize);
  iv_.assign(matter_start, matter_start + kBlockSize);
  return true;
}

}  // namespace shill
