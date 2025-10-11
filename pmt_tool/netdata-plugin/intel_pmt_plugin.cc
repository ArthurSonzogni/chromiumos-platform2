// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Netdata plugin for Intel PMT data

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <absl/time/time.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>
#include <brillo/flag_helper.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <libpmt/pmt.h>

#include "base/strings/stringprintf.h"

namespace {

// Default values for arguments.
constexpr char kDefaultCsvFile[] = "/tmp/pmt.csv";
constexpr int kDefaultSeconds = 2;
constexpr int kDefaultRecords = 30;
// Constants.
constexpr char kPmtToolPath[] = "/usr/local/bin/pmt_tool";
constexpr char kHeartdPmtPath[] = "/var/lib/heartd/intel_pmt";
constexpr int kHeartdBufSize = 8640;
constexpr int kChartPriority = 1000;

// Enum for the different data source formats.
enum SourceFormat {
  // Raw binary PMT data as produced by pmt::PmtCollector.
  RAW = 0,
  // Decoded into a CSV.
  CSV,
  // Raw data as a protobuf debug string.
  DBG,
  // Periodically sampled raw data by haertd.
  HEARTD,
  // Unknown source format.
  UNKNOWN
};

// Parses the "source" command line switch and returns the SourceFormat.
inline SourceFormat GetSourceFormat(const base::CommandLine* cl) {
  std::string source_type_str = cl->GetSwitchValueASCII("source");
  // Debug not supported yet, only decoded CSV and RAW format accepted.
  if (source_type_str.empty() || source_type_str == "csv") {
    return SourceFormat::CSV;
  } else if (source_type_str == "heartd") {
    return SourceFormat::HEARTD;
  }
  return SourceFormat::UNKNOWN;
}

// Helper to remove quotes from a string.
inline void TrimQuotes(std::string& s) {
  base::TrimString(s, "\"", &s);
}

// Parses a timestamp string.
inline bool ParseTimestamp(const std::string& ts_str, base::Time* time) {
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

inline void CreateChart(const std::string& chart_id,
                        const std::string& guid,
                        const std::string& unit,
                        const std::string& unit_id,
                        int frequency,
                        size_t dimension_index,
                        const std::string& dimension_group,
                        const std::string& dimension_name) {
  std::cout << "CHART " << chart_id << " '' 'Intel PMT Data from Device "
            << guid << "' \"" << unit << "\" device_" << guid << " Intel_PMT."
            << guid << "_" << unit_id << " line " << kChartPriority << " "
            << frequency << std::endl;
  std::cout << "DIMENSION col_" << dimension_index << " '" << dimension_group
            << "_" << dimension_name << "' absolute 1 1" << std::endl;
}

bool SeekLatestSnapshot(int fd,
                        const base::FilePath& counter_path,
                        size_t header_size,
                        size_t snapshot_size) {
  std::string counter_str;
  int counter = 0;
  if (base::ReadFileToString(counter_path, &counter_str)) {
    base::TrimWhitespaceASCII(counter_str, base::TRIM_ALL, &counter_str);
    if (!base::StringToInt(counter_str, &counter)) {
      LOG(ERROR) << "PMT ERROR: Failed to parse counter.";
      return false;
    }
  }
  // Decrement and wrap around because of circular buffer.
  counter =
      (((counter - 1) % kHeartdBufSize) + kHeartdBufSize) % kHeartdBufSize;
  // Seek to the position of the latest snapshot.
  lseek(fd, header_size + counter * snapshot_size, SEEK_SET);
  return true;
}

bool DecodeSnapshot(int fd,
                    size_t snapshot_size,
                    pmt::PmtDecoder* decoder,
                    pmt::Snapshot* snapshot,
                    const pmt::DecodingResult** result) {
  google::protobuf::io::FileInputStream file_stream(fd);
  google::protobuf::io::CodedInputStream coded_stream(&file_stream);

  auto limit = coded_stream.PushLimit(snapshot_size);
  if (!snapshot->ParseFromCodedStream(&coded_stream) ||
      !coded_stream.ConsumedEntireMessage()) {
    LOG(ERROR) << "PMT ERROR: Failed to parse snapshot.";
    return false;
  }
  coded_stream.PopLimit(limit);
  *result = decoder->Decode(snapshot);
  if (!*result) {
    LOG(ERROR) << "PMT ERROR: Failed to decode snapshot.";
    return false;
  }
  return true;
}

int ParseHeartdData() {
  base::FilePath heartd_pmt_path(kHeartdPmtPath);
  base::FilePath pmt_log_path = heartd_pmt_path.Append("intel_pmt.log");

  if (!base::PathExists(pmt_log_path)) {
    LOG(ERROR) << "PMT ERROR: heartd PMT log file not found at "
               << pmt_log_path.value();
    return 1;
  }

  base::FilePath heartd_config_path = heartd_pmt_path.Append("config");
  if (!base::PathExists(heartd_config_path)) {
    LOG(ERROR) << "PMT ERROR: heartd config file not found at "
               << heartd_config_path.value();
    return 1;
  }
  // Read the sampling frequency from heartd config (json).
  std::string config_content;
  if (!base::ReadFileToString(heartd_config_path, &config_content)) {
    LOG(ERROR) << "PMT ERROR: Failed to read heartd config file.";
    return 1;
  }
  auto config_dict = base::JSONReader::ReadDict(
      config_content, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!config_dict) {
    LOG(ERROR) << "PMT ERROR: Failed to parse heartd config file.";
    return 1;
  }
  int freq = config_dict->FindInt("sample_frequency").value_or(10);

  // Set up the PMT decoder.
  pmt::PmtDecoder decoder;
  auto guids = decoder.DetectMetadata();
  if (guids.empty()) {
    LOG(ERROR) << "PMT ERROR: No PMT metadata found for decoding.";
    return 1;
  }
  if (decoder.SetUpDecoding(guids) != 0) {
    LOG(ERROR) << "PMT ERROR: Failed to set up PMT decoder.";
    return 1;
  }

  int fd = open(pmt_log_path.value().c_str(), O_RDONLY);
  if (fd < 0) {
    LOG(ERROR) << "PMT ERROR: Failed to open " << pmt_log_path.value();
    return 1;
  }

  // Temporary stream to read just the header.
  google::protobuf::io::FileInputStream header_stream(fd);
  google::protobuf::io::CodedInputStream header_coded_stream(&header_stream);
  // Read the header to get snapshot size.
  pmt::LogHeader header;
  header.set_snapshot_size(1);
  uint32_t header_size = header.ByteSizeLong();
  auto limit = header_coded_stream.PushLimit(header_size);
  if (!header.ParseFromCodedStream(&header_coded_stream) ||
      !header_coded_stream.ConsumedEntireMessage()) {
    LOG(ERROR) << "PMT ERROR: Failed to parse log header.";
    close(fd);
    return 1;
  }
  header_coded_stream.PopLimit(limit);
  size_t snapshot_size = header.snapshot_size();

  // Read the counter to seek the latest record.
  base::FilePath counter_path = heartd_pmt_path.Append("counter");
  if (!SeekLatestSnapshot(fd, counter_path, header_size, snapshot_size)) {
    close(fd);
    return 1;
  }

  pmt::Snapshot snapshot;
  const pmt::DecodingResult* result;
  if (!DecodeSnapshot(fd, snapshot_size, &decoder, &snapshot, &result)) {
    close(fd);
    return 1;
  }

  // Map sample indices to charts so we can batch updates per chart.
  std::unordered_map<std::string, std::vector<size_t>> chart_ids;

  // Create Netdata charts and dimensions.
  for (size_t i = 0; i < result->meta_.size(); ++i) {
    const auto& meta = result->meta_[i];
    // Each GUID-unit combination will have its own chart.
    std::string unit_id = meta.unit_;
    size_t first_space = unit_id.find(' ');
    if (first_space != std::string::npos) {
      unit_id = unit_id.substr(0, first_space);
    }
    std::string guid_hex = base::StringPrintf("0x%x", meta.guid_);
    std::string chart_id = "intel_pmt.dev_" + guid_hex + "_" + unit_id;
    chart_ids[chart_id].push_back(i);

    // Issue Netdata commands to create charts and dimensions.
    CreateChart(chart_id, guid_hex, meta.unit_, unit_id, freq, i, meta.group_,
                meta.name_);
  }

  // Process remaining snapshots.
  while (true) {
    // Iterate over each chart and update its dimensions.
    for (const auto& pair : chart_ids) {
      const std::string& chart_id = pair.first;
      const std::vector<size_t>& sample_indices = pair.second;

      std::cout << "BEGIN " << chart_id << std::endl;
      for (size_t i : sample_indices) {
        const auto& meta = result->meta_[i];
        const auto& value = result->values_[i];
        double val_to_print =
            (meta.type_ == pmt::DataType::FLOAT) ? value.f_ : value.i64_;
        std::cout << "SET col_" << i << " = " << val_to_print << std::endl;
      }
      std::cout << "END" << std::endl;
    }

    base::PlatformThread::Sleep(base::Seconds(freq));

    // Re-read the counter and seek to the latest snapshot to ensure we always
    // process the most recent data.
    if (!SeekLatestSnapshot(fd, counter_path, header_size, snapshot_size)) {
      LOG(ERROR) << "PMT ERROR: Failed to seek to latest snapshot, stopping.";
      break;
    }

    if (!DecodeSnapshot(fd, snapshot_size, &decoder, &snapshot, &result)) {
      break;
    }
  }

  close(fd);
  return 0;
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
  } else if (source_type == SourceFormat::HEARTD) {
    // If source is from heartd, decode it and process it periodically.
    return ParseHeartdData();
  }

  // Else, source_type is a file at "path".
  // Parse the existing file at the given path or start PMT sampling.
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

    // Map sample indices to charts so we can batch updates per chart.
    std::unordered_map<std::string, std::vector<size_t>> chart_ids;
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

      // Save sample indices in their respective chart for future updates.
      std::string chart_id = "intel_pmt.dev_" + curr_guid + "_" + unit_id;
      chart_ids[chart_id].push_back(i);

      // Issue Netdata commands to create charts and dimensions.
      CreateChart(chart_id, curr_guid, curr_unit, unit_id, delta_t, i,
                  curr_sample_group, sample_name);
    }

    LOG(INFO) << "PMT INFO: charts created";

    size_t num_rows = lines.size();
    DLOG(INFO) << "PMT INFO: total number of rows: " << num_rows;

    // Continuously update the charts with data from the CSV file.
    for (size_t row_idx = 5; row_idx < num_rows; ++row_idx) {
      auto data_array = base::SplitString(
          lines[row_idx], ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
      // Iterate over each chart and update its dimensions.
      for (const auto& pair : chart_ids) {
        const std::string& chart_id = pair.first;
        const std::vector<size_t>& sample_indices = pair.second;
        std::cout << "BEGIN " << chart_id << std::endl;
        for (size_t i : sample_indices) {
          if (i < data_array.size()) {
            std::cout << "SET col_" << i << " = " << data_array[i] << std::endl;
          }
        }
        std::cout << "END" << std::endl;
      }

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
