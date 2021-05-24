// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_FILE_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_FILE_UTILS_H_

#include <string>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/strings/string_piece.h>

namespace diagnostics {

// Reads the contents of |file_path| into |out|, trims leading and trailing
// whitespace. Returns true on success.
// |StringType| can be any type which can be converted from |std::string|. For
// example, |base::Optional<std::string>|.
template <typename StringType>
bool ReadAndTrimString(const base::FilePath& file_path, StringType* out) {
  DCHECK(out);
  std::string out_raw;

  if (!ReadAndTrimString(file_path, &out_raw))
    return false;

  *out = static_cast<StringType>(out_raw);
  return true;
}

template <>
bool ReadAndTrimString<std::string>(const base::FilePath& file_path,
                                    std::string* out);

// Like ReadAndTrimString() above, but expects a |filename| within |directory|
// to be read.
template <typename StringType>
bool ReadAndTrimString(const base::FilePath& directory,
                       const std::string& filename,
                       StringType* out) {
  return ReadAndTrimString(directory.Append(filename), out);
}

// Reads an integer value from a file and converts it using the provided
// function. Returns true on success.
template <typename T>
bool ReadInteger(const base::FilePath& file_path,
                 bool (*StringToInteger)(base::StringPiece, T*),
                 T* out) {
  DCHECK(StringToInteger);
  DCHECK(out);

  std::string buffer;
  if (!ReadAndTrimString(file_path, &buffer))
    return false;

  return StringToInteger(buffer, out);
}

// Like ReadInteger() above, but expects a |filename| within |directory| to be
// read.
template <typename T>
bool ReadInteger(const base::FilePath& directory,
                 const std::string& filename,
                 bool (*StringToInteger)(base::StringPiece, T*),
                 T* out) {
  return ReadInteger(directory.AppendASCII(filename), StringToInteger, out);
}

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_FILE_UTILS_H_
