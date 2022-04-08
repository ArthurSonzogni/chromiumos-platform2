// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_SETUP_ANDROID_BINARY_XML_TOKENIZER_H_
#define ARC_SETUP_ANDROID_BINARY_XML_TOKENIZER_H_

#include <base/component_export.h>
#include <base/files/file.h>
#include <base/files/file_path.h>

namespace arc {

// Tokenizer for the Android Binary XML.
// The format is defined by the implementation of Android's
// frameworks/base/core/java/com/android/internal/util/BinaryXmlSerializer.java
// and BinaryXmlPullParser.java.
class COMPONENT_EXPORT(LIBARC_SETUP) AndroidBinaryXmlTokenizer {
 public:
  // Token constants are defined in Android's
  // libcore/xml/src/main/java/org/xmlpull/v1/XmlPullParser.java
  enum class Token {
    kStartDocument = 0,
    kEndDocument = 1,
  };

  // Type constants are defined in Android's BinaryXmlSerializer.java.
  enum class Type {
    kNull = 1,
  };

  static const char kMagicNumber[4];

  AndroidBinaryXmlTokenizer();
  AndroidBinaryXmlTokenizer(const AndroidBinaryXmlTokenizer&) = delete;
  const AndroidBinaryXmlTokenizer& operator=(const AndroidBinaryXmlTokenizer&) =
      delete;
  ~AndroidBinaryXmlTokenizer();

  // Initializes this object to read the specified file.
  bool Init(const base::FilePath& path);

  // Moves to the next token.
  bool Next();

  // Returns true after reaching EOF.
  bool is_eof() const { return is_eof_; }

  // The type of the current token.
  Token token() const { return token_; }

  // The data type of the current token.
  Type type() const { return type_; }

 private:
  // The binary XML file being read.
  base::File file_;

  bool is_eof_ = false;

  Token token_ = Token::kStartDocument;
  Type type_ = Type::kNull;
};

}  // namespace arc

#endif  // ARC_SETUP_ANDROID_BINARY_XML_TOKENIZER_H_
