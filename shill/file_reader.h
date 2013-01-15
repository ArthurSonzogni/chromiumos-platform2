// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_FILE_READER_H_
#define SHILL_FILE_READER_H_

#include <string>

#include <base/basictypes.h>
#include <base/file_path.h>
#include <base/file_util.h>

namespace shill {

// A helper class for reading a file line-by-line, which is expected to
// be a substitute for std::getline() as the Google C++ style guide disallows
// the use of stream.
class FileReader {
 public:
  FileReader();
  ~FileReader();

  // Closes the file.
  void Close();

  // Opens the file of a given path. Returns true on success.
  bool Open(const FilePath &file_path);

  // Reads a line, terminated by either LF or EOF, from the file into
  // a given string, with LF excluded. Returns false if no more line
  // can be read from the file.
  bool ReadLine(std::string *line);

 private:
  // The file to read.
  file_util::ScopedFILE file_;

  DISALLOW_COPY_AND_ASSIGN(FileReader);
};

}  // namespace shill

#endif  // SHILL_FILE_READER_H_
