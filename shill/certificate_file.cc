// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/certificate_file.h"

#include <sys/stat.h>

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "shill/logging.h"

using base::FilePath;
using base::SplitString;
using base::StringPrintf;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCrypto;
static string ObjectID(CertificateFile* c) { return "(certificate_file)"; }
}

const char CertificateFile::kDefaultRootDirectory[] =
    RUNDIR "/certificate_export";
const char CertificateFile::kPEMHeader[] = "-----BEGIN CERTIFICATE-----";
const char CertificateFile::kPEMFooter[] = "-----END CERTIFICATE-----";

CertificateFile::CertificateFile()
    : root_directory_(FilePath(kDefaultRootDirectory)) {
  SLOG(this, 2) << __func__;
}

CertificateFile::~CertificateFile() {
  SLOG(this, 2) << __func__;
  if (!output_file_.empty()) {
    base::DeleteFile(output_file_, false);
  }
}

FilePath CertificateFile::CreatePEMFromStrings(
    const vector<string>& pem_contents) {
  vector<string> pem_output;
  for (const auto& content : pem_contents) {
    string hex_data = ExtractHexData(content);
    if (hex_data.empty()) {
      return FilePath();
    }
    pem_output.push_back(StringPrintf(
      "%s\n%s%s\n", kPEMHeader, hex_data.c_str(), kPEMFooter));
  }
  return WriteFile(JoinString(pem_output, ""));
}

// static
string CertificateFile::ExtractHexData(const std::string& pem_data) {
  bool found_header = false;
  bool found_footer = false;
  const bool kCaseSensitive = false;
  vector<string> input_lines;
  SplitString(pem_data, '\n', &input_lines);
  vector<string> output_lines;
  for (vector<string>::const_iterator it = input_lines.begin();
       it != input_lines.end(); ++it) {
    string line;
    base::TrimWhitespaceASCII(*it, base::TRIM_ALL, &line);
    if (base::StartsWithASCII(line, kPEMHeader, kCaseSensitive)) {
      if (found_header) {
        LOG(ERROR) << "Found two PEM headers in a row.";
        return string();
      } else {
        found_header = true;
        output_lines.clear();
      }
    } else if (base::StartsWithASCII(line, kPEMFooter, kCaseSensitive)) {
      if (!found_header) {
        LOG(ERROR) << "Found a PEM footer before header.";
        return string();
      } else {
        found_footer = true;
        break;
      }
    } else if (!line.empty()) {
      output_lines.push_back(line);
    }
  }
  if (found_header && !found_footer) {
    LOG(ERROR) << "Found PEM header but no footer.";
    return string();
  }
  DCHECK_EQ(found_header, found_footer);
  output_lines.push_back("");
  return JoinString(output_lines, "\n");
}

FilePath CertificateFile::WriteFile(const string& output_data) {
  if (!base::DirectoryExists(root_directory_)) {
    if (!base::CreateDirectory(root_directory_)) {
      LOG(ERROR) << "Unable to create parent directory  "
                 << root_directory_.value();
      return FilePath();
    }
    if (chmod(root_directory_.value().c_str(),
              S_IRWXU | S_IXGRP | S_IRGRP | S_IXOTH | S_IROTH)) {
      LOG(ERROR) << "Failed to set permissions on "
                 << root_directory_.value();
      base::DeleteFile(root_directory_, true);
      return FilePath();
    }
  }
  if (!output_file_.empty()) {
    base::DeleteFile(output_file_, false);
    output_file_ = FilePath();
  }

  FilePath output_file;
  if (!base::CreateTemporaryFileInDir(root_directory_, &output_file)) {
    LOG(ERROR) << "Unable to create output file.";
    return FilePath();
  }

  size_t written =
      base::WriteFile(output_file, output_data.c_str(), output_data.length());
  if (written != output_data.length()) {
    LOG(ERROR) << "Unable to write to output file.";
    return FilePath();
  }

  if (chmod(output_file.value().c_str(),
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) {
    LOG(ERROR) << "Failed to set permissions on " << output_file.value();
    base::DeleteFile(output_file, false);
    return FilePath();
  }
  output_file_ = output_file;
  return output_file_;
}

}  // namespace shill
