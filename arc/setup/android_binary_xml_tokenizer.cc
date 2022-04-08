// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/setup/android_binary_xml_tokenizer.h"

#include <base/logging.h>

namespace arc {

// The first four bytes of an Android binary XML are the magic number 'ABX_'.
// The fourth byte is the version number which should be 0.
//
// static
const char AndroidBinaryXmlTokenizer::kMagicNumber[4] = {'A', 'B', 'X', 0};

AndroidBinaryXmlTokenizer::AndroidBinaryXmlTokenizer() = default;

AndroidBinaryXmlTokenizer::~AndroidBinaryXmlTokenizer() = default;

bool AndroidBinaryXmlTokenizer::Init(const base::FilePath& path) {
  file_.Initialize(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file_.IsValid()) {
    LOG(ERROR) << "Failed to initialize the file: " << path
               << ", error = " << file_.error_details();
    return false;
  }
  // Check the magic number.
  char buf[sizeof(kMagicNumber)] = {};
  if (file_.ReadAtCurrentPos(buf, sizeof(buf)) != sizeof(buf) ||
      memcmp(buf, kMagicNumber, sizeof(kMagicNumber)) != 0) {
    LOG(ERROR) << "Invalid magic number";
    return false;
  }
  return true;
}

bool AndroidBinaryXmlTokenizer::Next() {
  // Read the token.
  uint8_t value = 0;
  int result =
      file_.ReadAtCurrentPos(reinterpret_cast<char*>(&value), sizeof(value));
  if (result == 0) {  // Reached EOF.
    is_eof_ = true;
    return false;
  } else if (result != sizeof(value)) {  // Failed to read.
    LOG(ERROR) << "Failed to read the token.";
    return false;
  }

  // The lower four bits indicate the token type.
  token_ = static_cast<Token>(value & 0x0f);
  // The upper four bits indicate the data type.
  type_ = static_cast<Type>((value & 0xf0) >> 4);
  switch (token_) {
    case Token::kStartDocument:
      return true;

    case Token::kEndDocument:
      return true;
  }
  LOG(ERROR) << "Unexpected token " << static_cast<int>(token_);
  return false;
}

}  // namespace arc
