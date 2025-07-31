// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Netdata plugin for Intel PMT data

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <absl/time/time.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>

namespace {

// Default values for arguments.
const char kDefaultCsvFile[] = "/tmp/pmt.csv";
const int kDefaultSeconds = 2;
const int kDefaultRecords = 30;
const char kPmtToolPath[] = "/usr/local/bin/pmt_tool";

// Enum for the different data source formats.
enum SourceFormat {
  // Raw binary PMT data as produced by pmt::PmtCollector.
  RAW = 0,
  // Decoded into a CSV.
  CSV,
  // Raw data as a protobuf debug string.
  DBG,
  // Unknown source format.
  UNKNOWN
};

// Parses the "source" command line switch and returns the SourceFormat.
SourceFormat GetSourceFormat(const base::CommandLine* cl) {
  std::string source_type_str = cl->GetSwitchValueASCII("source");
  // Decoding not supported yet, only decoded CSV format accepted.
  if (source_type_str.empty() || source_type_str == "csv") {
    return SourceFormat::CSV;
  }
  return SourceFormat::UNKNOWN;
}

// Helper to remove quotes from a string.
void TrimQuotes(std::string& s) {
  base::TrimString(s, "\"", &s);
}

// Parses a timestamp string.
bool ParseTimestamp(const std::string& ts_str, base::Time* time) {
  absl::Time absl_time;
  std::string err;
  // The timestamp is formatted using absl::FormatTime in pmt_tool, which
  // defaults to a format compatible with RFC3339.
  if (!absl::ParseTime(absl::RFC3339_full, ts_str, &absl_time, &err)) {
    LOG(ERROR) << "Failed to parse timestamp string '" << ts_str
               << "': " << err;
    return false;
  }
  *time = base::Time::FromSecondsSinceUnixEpoch(absl::ToUnixSeconds(absl_time));
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  auto* cl = base::CommandLine::ForCurrentProcess();

  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_STDERR;
  logging::InitLogging(settings);

  // The first argument from netdata is 'update_every', which is ignored,
  // base::CommandLine handles this for us.

  SourceFormat source_type = GetSourceFormat(cl);

  if (source_type == SourceFormat::UNKNOWN) {
    LOG(ERROR) << "PMT ERROR: Unknown source type: "
               << cl->GetSwitchValueASCII("source");
    return 1;
  }

  std::string csv_file = cl->GetSwitchValueASCII("path");
  if (csv_file.empty()) {
    csv_file = kDefaultCsvFile;
  }

  int seconds = kDefaultSeconds;
  if (cl->HasSwitch("seconds")) {
    base::StringToInt(cl->GetSwitchValueASCII("seconds"), &seconds);
  }

  int records = kDefaultRecords;
  if (cl->HasSwitch("records")) {
    base::StringToInt(cl->GetSwitchValueASCII("records"), &records);
  }

  base::FilePath csv_path(csv_file);
  // Construct the commadnd line invocation for pmt_tool LaunchProcess.
  base::CommandLine pmt_cmd((base::FilePath(kPmtToolPath)));
  pmt_cmd.AppendSwitchASCII("i", base::NumberToString(seconds));
  pmt_cmd.AppendSwitchASCII("n", base::NumberToString(records));
  pmt_cmd.AppendSwitchASCII("f", "csv");

  while (true) {
    std::string csv_content;
    // If a source file doesn't exist at the given path, call pmt_tool to start
    // sampling PMT data and capture the output.
    // Otherwise, read the PMT data from the source file.
    if (!base::PathExists(csv_path)) {
      LOG(INFO) << "PMT INFO: CSV file not found.";
      LOG(INFO) << "PMT INFO: call pmt_tool";

      // This C++ plugin is expected to be
      // run with sufficient privileges to execute pmt_tool.
      int exit_code;
      if (!base::GetAppOutputWithExitCode(pmt_cmd, &csv_content, &exit_code) ||
          exit_code != 0 || csv_content.empty()) {
        LOG(ERROR) << "PMT ERROR: pmt_tool execution failed with exit code "
                   << exit_code << " or produced no output.";
        return 1;
      }
    } else if (!base::ReadFileToString(csv_path, &csv_content)) {
      LOG(ERROR) << "PMT ERROR: Failed to read CSV file: " << csv_file;
      return 1;
    }

    std::vector<std::string> lines = base::SplitString(
        csv_content, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

    if (lines.size() < 7) {
      LOG(ERROR) << "PMT ERROR: CSV file has insufficient data (less than 7 "
                    "lines). Deleting and retrying.";
      brillo::DeleteFile(csv_path);
      continue;
    }

    // Get the delta_t time steps between records based on timestamps.
    auto row5_cols = base::SplitString(lines[5], ",", base::KEEP_WHITESPACE,
                                       base::SPLIT_WANT_ALL);
    auto row6_cols = base::SplitString(lines[6], ",", base::KEEP_WHITESPACE,
                                       base::SPLIT_WANT_ALL);

    if (row5_cols.empty() || row6_cols.empty()) {
      LOG(ERROR) << "PMT ERROR: Failed to parse rows.";
      brillo::DeleteFile(csv_path);
      return 1;
    }

    base::Time t1, t2;
    int64_t delta_t;
    if (!ParseTimestamp(row5_cols[0], &t1) ||
        !ParseTimestamp(row6_cols[0], &t2)) {
      LOG(ERROR) << "PMT ERROR: Failed to parse timestamps."
                 << " Using default of " << seconds << "s.";
      delta_t = seconds;
    } else {
      delta_t = (t2 - t1).InSeconds();
    }

    if (delta_t <= 0) {
      LOG(WARNING) << "PMT WARNING: Invalid delta_t: " << delta_t
                   << ". Using default of " << seconds << "s.";
      delta_t = seconds;
    }

    // Read header rows.
    // Description (3rd) row not needed, so skip lines[2].
    auto guid_array = base::SplitString(lines[0], ",", base::KEEP_WHITESPACE,
                                        base::SPLIT_WANT_ALL);
    auto sample_groups = base::SplitString(lines[1], ",", base::KEEP_WHITESPACE,
                                           base::SPLIT_WANT_ALL);
    auto units_array = base::SplitString(lines[3], ",", base::KEEP_WHITESPACE,
                                         base::SPLIT_WANT_ALL);
    auto samples_array = base::SplitString(lines[4], ",", base::KEEP_WHITESPACE,
                                           base::SPLIT_WANT_ALL);
    size_t num_cols = samples_array.size();
    DLOG(INFO) << "PMT INFO: creating dimensions for "
               << (num_cols > 0 ? num_cols - 1 : 0) << " samples";

    std::vector<std::string> chart_ids(num_cols);
    std::string curr_guid, curr_sample_group;

    // Create the Netdata charts.
    // Create dimensions based on sample header row,
    // starting from the second column (index 1).
    for (size_t i = 1; i < num_cols; ++i) {
      std::string curr_unit = units_array[i];
      std::string sample_name = samples_array[i];
      TrimQuotes(curr_unit);
      TrimQuotes(sample_name);

      // Strip whitespace so it can be used as ID suffix.
      std::string unit_id;
      size_t first_space = curr_unit.find(' ');
      if (first_space != std::string::npos) {
        unit_id = curr_unit.substr(0, first_space);
      }

      if (i < sample_groups.size() && !sample_groups[i].empty()) {
        curr_sample_group = sample_groups[i];
        TrimQuotes(curr_sample_group);
      }

      // Each GUID-unit combination will have its own chart.
      if (i < guid_array.size() && !guid_array[i].empty()) {
        curr_guid = guid_array[i];
        TrimQuotes(curr_guid);
      }

      // Map chart_ids to sample indices.
      std::string chart_id = "intel_pmt.dev_" + curr_guid + "_" + unit_id;
      chart_ids[i] = chart_id;

      // Issue Netdata commands to create charts and dimensions.
      std::cout << "CHART " << chart_id << " '' 'Intel PMT Data from Device "
                << curr_guid << "' \"" << curr_unit << "\" device_" << curr_guid
                << " Intel_PMT." << curr_guid << "_" << unit_id
                << " line 10000 " << delta_t << std::endl;
      std::cout << "DIMENSION col_" << i << " '" << curr_sample_group << "_"
                << sample_name << "' absolute 1 1" << std::endl;
    }

    LOG(INFO) << "PMT INFO: charts created";
    std::cout.flush();

    size_t num_rows = lines.size();
    DLOG(INFO) << "PMT INFO: total number of rows: " << num_rows;

    // Continuously update the charts with data from the CSV file.
    for (size_t row_idx = 5; row_idx < num_rows; ++row_idx) {
      auto data_array = base::SplitString(
          lines[row_idx], ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

      std::string chart_id = chart_ids[1];
      std::cout << "BEGIN " << chart_id << std::endl;
      // Skip timestamp column.
      for (size_t i = 1; i < num_cols; ++i) {
        // Switch chart if new chart_id encountered.
        if (chart_ids[i] != chart_id) {
          std::cout << "END" << std::endl;
          chart_id = chart_ids[i];
          std::cout << "BEGIN " << chart_id << std::endl;
        }
        if (i < data_array.size()) {
          std::cout << "SET col_" << i << " = " << data_array[i] << std::endl;
        }
      }
      std::cout << "END" << std::endl;
      std::cout.flush();

      if (row_idx < num_rows - 1) {
        base::PlatformThread::Sleep(base::Seconds(delta_t));
      }
    }
    DLOG(INFO) << "PMT INFO: CSV file processed, last row: " << num_rows;

    // Archive last processed CSV file.
    base::FilePath old_csv_path("/tmp/netdata_pmt.csv.old");
    if (base::PathExists(csv_path)) {
      if (!base::CopyFile(csv_path, old_csv_path)) {
        LOG(WARNING) << "PMT WARNING: Failed to archive CSV file.";
      }
      DLOG(INFO) << "PMT INFO: deleting processed CSV file: " << csv_file;
      if (!brillo::DeleteFile(csv_path)) {
        LOG(WARNING) << "PMT WARNING: Failed to delete processed CSV file.";
      }
    } else {
      // Save csv_content into the archive file.
      if (!base::WriteFile(old_csv_path, csv_content)) {
        LOG(WARNING) << "PMT WARNING: Failed to archive CSV content.";
      }
    }

    base::PlatformThread::Sleep(base::Seconds(10));
  }
}
