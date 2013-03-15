// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/file_reader.h"

using base::FilePath;
using std::string;

namespace shill {

FileReader::FileReader() {
}

FileReader::~FileReader() {
}

void FileReader::Close() {
  file_.reset();
}

bool FileReader::Open(const FilePath &file_path) {
  file_.reset(file_util::OpenFile(file_path, "rb"));
  return file_.get() != NULL;
}

bool FileReader::ReadLine(string *line) {
  CHECK(line) << "Invalid argument";

  FILE *fp = file_.get();
  if (fp == NULL)
    return false;

  line->clear();
  bool line_valid = false;
  int ch;
  while ((ch = fgetc(fp)) != EOF) {
    if (ch == '\n')
      return true;
    line->push_back(static_cast<char>(ch));
    line_valid = true;
  }
  return line_valid;
}

}  // namespace shill
