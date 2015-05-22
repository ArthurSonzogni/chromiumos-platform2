// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromiumos-wide-profiling/test_utils.h"

#include <string.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "base/logging.h"

#include "chromiumos-wide-profiling/compat/proto.h"
#include "chromiumos-wide-profiling/perf_serializer.h"
#include "chromiumos-wide-profiling/run_command.h"
#include "chromiumos-wide-profiling/utils.h"

using quipper::PerfDataProto;
using quipper::PerfSerializer;
using quipper::TextFormat;

namespace {

// Newline character.
const char kNewLineDelimiter = '\n';

// Extension of protobuf files in text format.
const char kProtobufTextExtension[] = ".pb_text";

// Extension of build ID lists.
const char kBuildIDListExtension[] = ".buildids";

enum PerfDataType {
  kPerfDataNormal,    // Perf data is in normal format.
  kPerfDataPiped,     // Perf data is in piped format.
};

// The piped commands above produce comma-separated lines with the following
// fields:
enum {
  PERF_REPORT_OVERHEAD,
  PERF_REPORT_SAMPLES,
  PERF_REPORT_COMMAND,
  PERF_REPORT_SHARED_OBJECT,
  NUM_PERF_REPORT_FIELDS,
};

// Splits a character array by |delimiter| into a vector of strings tokens.
void SplitString(const string& str,
                 char delimiter,
                 std::vector<string>* tokens) {
  std::stringstream ss(str);
  std::string token;
  while (std::getline(ss, token, delimiter))
    tokens->push_back(token);
}

// Split a char buffer into separate lines.
void SeparateLines(const std::vector<char>& bytes, std::vector<string>* lines) {
  if (!bytes.empty())
    SplitString(string(&bytes[0], bytes.size()), kNewLineDelimiter, lines);
}

bool ReadExistingProtobufText(const string& filename, string* output_string) {
  std::vector<char> output_buffer;
  if (!quipper::FileToBuffer(filename + kProtobufTextExtension,
                               &output_buffer)) {
    LOG(ERROR) << "Could not open file " << filename + kProtobufTextExtension;
    return false;
  }
  output_string->assign(&output_buffer[0], output_buffer.size());
  return true;
}

// Given a perf data file, return its protobuf representation (given by
// PerfSerializer) as a text string.
bool GetProtobufTextFormat(const string& filename, string* output_string) {
  PerfDataProto perf_data_proto;
  PerfSerializer serializer;
  if (!serializer.SerializeFromFile(filename, &perf_data_proto)) {
    return false;
  }
  // Reset the timestamp field since it causes reproducability issues when
  // testing.
  perf_data_proto.set_timestamp_sec(0);
  if (!TextFormat::PrintToString(perf_data_proto, output_string)) {
    return false;
  }

  return true;
}

}  // namespace

namespace quipper {

const char* kSupportedMetadata[] = {
  "hostname",
  "os release",
  "perf version",
  "arch",
  "nrcpus online",
  "nrcpus avail",
  "cpudesc",
  "cpuid",
  "total memory",
  "cmdline",
  "event",
  "sibling cores",        // CPU topology.
  "sibling threads",      // CPU topology.
  "node0 meminfo",        // NUMA topology.
  "node0 cpu list",       // NUMA topology.
  "node1 meminfo",        // NUMA topology.
  "node1 cpu list",       // NUMA topology.
  NULL,
};

#ifndef QUIPPER_EXTERNAL_TEST_PATHS
string GetTestInputFilePath(const string& filename) {
  return "testdata/" + filename;
}

string GetPerfPath() {
  return "/usr/bin/perf";
}
#endif  // !QUIPPER_EXTERNAL_TEST_PATHS

int64_t GetFileSize(const string& filename) {
  FILE* fp = fopen(filename.c_str(), "rb");
  if (!fp)
    return -1;
  int64_t file_size = GetFileSizeFromHandle(fp);
  fclose(fp);
  return file_size;
}

bool CompareFileContents(const string& filename1, const string& filename2) {
  std::vector<char> file1_contents;
  std::vector<char> file2_contents;
  if (!FileToBuffer(filename1, &file1_contents) ||
      !FileToBuffer(filename2, &file2_contents)) {
    return false;
  }

  return file1_contents == file2_contents;
}

bool GetPerfBuildIDMap(const string& filename,
                       std::map<string, string>* output) {
  // Try reading from a pre-generated report.  If it doesn't exist, call perf
  // buildid-list.
  std::vector<char> buildid_list;
  LOG(INFO) << filename + kBuildIDListExtension;
  if (!quipper::FileToBuffer(filename + kBuildIDListExtension, &buildid_list)) {
    buildid_list.clear();
    if (!RunCommand({GetPerfPath(), "buildid-list", "--force", "-i", filename},
                    &buildid_list)) {
      LOG(ERROR) << "Failed to run perf buildid-list";
      return false;
    }
  }
  std::vector<string> lines;
  SeparateLines(buildid_list, &lines);

  /* The output now looks like the following:
     cff4586f322eb113d59f54f6e0312767c6746524 [kernel.kallsyms]
     c099914666223ff6403882604c96803f180688f5 /lib64/libc-2.15.so
     7ac2d19f88118a4970adb48a84ed897b963e3fb7 /lib64/libpthread-2.15.so
  */
  output->clear();
  for (string line : lines) {
    TrimWhitespace(&line);
    size_t separator = line.find(' ');
    string build_id = line.substr(0, separator);
    string filename = line.substr(separator + 1);
    (*output)[filename] = build_id;
  }

  return true;
}

namespace {
// Running tests while this is true will blindly make tests pass! So, remember
// to look at the diffs and explain them before submitting.
// TODO(dhsharp): replace this with a command-line flag.
static const bool kWriteNewGoldenFiles = false;
}  // namespace

bool CheckPerfDataAgainstBaseline(const string& filename) {
  string protobuf_text;
  if (!GetProtobufTextFormat(filename, &protobuf_text)) {
    return false;
  }
  string existing_input_file = GetTestInputFilePath(basename(filename.c_str()));
  string baseline;
  if (!ReadExistingProtobufText(existing_input_file , &baseline)) {
    return false;
  }
  bool matches_baseline = (baseline == protobuf_text);
  if (kWriteNewGoldenFiles) {
    string existing_input_pb_text =
        existing_input_file + kProtobufTextExtension + ".new";
    if (matches_baseline) {
      LOG(INFO) << "NOT writing identical golden file! "
                << existing_input_pb_text;
      return true;
    }
    LOG(INFO) << "Writing new golden file! " << existing_input_pb_text;
    BufferToFile(existing_input_pb_text, protobuf_text);
    return true;
  }
  return matches_baseline;
}

bool ComparePerfBuildIDLists(const string& file1, const string& file2) {
  std::map<string, string> output1;
  std::map<string, string> output2;

  // Generate a build id list for each file.
  CHECK(GetPerfBuildIDMap(file1, &output1));
  CHECK(GetPerfBuildIDMap(file2, &output2));

  // Compare the output strings.
  return output1 == output2;
}

PerfParser::Options GetTestOptions() {
  PerfParser::Options options;
  options.sample_mapping_percentage_threshold = 100.0f;
  return options;
}

}  // namespace quipper
