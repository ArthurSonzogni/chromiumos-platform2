// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/vmlog_writer.h"

#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <brillo/daemons/daemon.h>

namespace chromeos_metrics {
namespace {

constexpr char kVmlogHeader[] =
    "time pgmajfault pgmajfault_f pgmajfault_a pswpin pswpout cpuusage";

// We limit the size of vmlog log files to keep frequent logging from wasting
// disk space.
constexpr int kMaxVmlogFileSize = 256 * 1024;

}  // namespace

bool VmStatsParseStats(std::istream* input_stream,
                       struct VmstatRecord* record) {
  // a mapping of string name to field in VmstatRecord and whether we found it
  struct Mapping {
    const std::string name;
    uint64_t* value_p;
    bool found;
    bool optional;
  } map[] = {
      {.name = "pgmajfault",
       .value_p = &record->page_faults_,
       .found = false,
       .optional = false},
      // pgmajfault_f and pgmajfault_a may not be present in all kernels.
      // Don't fuss if they are not.
      {.name = "pgmajfault_f",
       .value_p = &record->file_page_faults_,
       .found = false,
       .optional = true},
      {.name = "pgmajfault_a",
       .value_p = &record->anon_page_faults_,
       .found = false,
       .optional = true},
      {.name = "pswpin",
       .value_p = &record->swap_in_,
       .found = false,
       .optional = false},
      {.name = "pswpout",
       .value_p = &record->swap_out_,
       .found = false,
       .optional = false},
  };

  // Each line in the file has the form
  // <ID> <VALUE>
  // for instance:
  // nr_free_pages 213427
  std::string line;
  while (std::getline(*input_stream, line)) {
    std::vector<std::string> tokens = base::SplitString(
        line, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    if (tokens.size() != 2u) {
      LOG(WARNING) << "Unexpected vmstat format in line: " << line;
      continue;
    }
    for (auto& mapping : map) {
      if (!tokens[0].compare(mapping.name)) {
        if (!base::StringToUint64(tokens[1], mapping.value_p))
          return false;
        mapping.found = true;
      }
    }
  }
  // Make sure we got all the stats, except the optional ones.
  for (const auto& mapping : map) {
    if (!mapping.found) {
      if (mapping.optional) {
        *mapping.value_p = 0;
      } else {
        LOG(WARNING) << "vmstat missing " << mapping.name;
        return false;
      }
    }
  }
  return true;
}

bool ParseCpuTime(std::istream* input, CpuTimeRecord* record) {
  std::string buf;
  if (!std::getline(*input, buf)) {
    PLOG(ERROR) << "Unable to read cpu time";
    return false;
  }
  // Expect the first line to be like
  // cpu  20126642 15102603 12415348 2330408305 11759657 0 355204 0 0 0
  // The number corresponds to cpu time for
  // #cpu user nice system idle iowait irq softirq  steal guest guest_nice
  std::vector<std::string> tokens = base::SplitString(
      buf, " \t\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (tokens[0] != "cpu") {
    LOG(WARNING) << "Expect the first line of /proc/stat to be \"cpu ...\"";
    return false;
  }
  uint64_t value;
  for (int i = 1; i < tokens.size(); ++i) {
    if (!base::StringToUint64(tokens[i], &value)) {
      LOG(WARNING) << "Unable to convert " << tokens[i] << " to int64";
      return false;
    }
    record->total_time_ += value;
    // Exclude idle or iowait.
    if (i != 4 && i != 5) {
      record->non_idle_time_ += value;
    }
  }
  return true;
}

VmlogFile::VmlogFile(const base::FilePath& live_path,
                     const base::FilePath& rotated_path,
                     const uint64_t max_size,
                     const std::string& header)
    : live_path_(live_path),
      rotated_path_(rotated_path),
      max_size_(max_size),
      header_(header) {
  fd_ = open(live_path_.value().c_str(), O_CREAT | O_RDWR | O_EXCL, 0644);
  if (fd_ != -1) {
    Write(header_);
  } else {
    PLOG(ERROR) << "Failed to open file: " << live_path_.value();
  }
}

VmlogFile::~VmlogFile() = default;

bool VmlogFile::Write(const std::string& data) {
  if (fd_ == -1)
    return false;

  if (cur_size_ + data.size() > max_size_) {
    if (!base::CopyFile(live_path_, rotated_path_)) {
      PLOG(ERROR) << "Could not copy vmlog to: " << rotated_path_.value();
    }
    base::FilePath rotated_path_dir = rotated_path_.DirName();
    base::FilePath rotated_symlink = rotated_path_dir.Append("vmlog.1.LATEST");
    if (!base::PathExists(rotated_symlink)) {
      if (!base::CreateSymbolicLink(rotated_path_, rotated_symlink)) {
        PLOG(ERROR) << "Unable to create symbolic link from "
                    << rotated_symlink.value() << " to "
                    << rotated_path_.value();
      }
    }

    if (HANDLE_EINTR(ftruncate(fd_, 0)) != 0) {
      PLOG(ERROR) << "Could not ftruncate() file";
      return false;
    }
    if (HANDLE_EINTR(lseek(fd_, 0, SEEK_SET)) != 0) {
      PLOG(ERROR) << "Could not lseek() file";
      return false;
    }
    cur_size_ = 0;
    if (!Write(header_)) {
      return false;
    }
  }

  if (!base::WriteFileDescriptor(fd_, data.c_str(), data.size())) {
    return false;
  }
  cur_size_ += data.size();
  return true;
}

VmlogWriter::VmlogWriter(const base::FilePath& vmlog_dir,
                         const base::TimeDelta& log_interval) {
  if (!base::DirectoryExists(vmlog_dir)) {
    if (!base::CreateDirectory(vmlog_dir)) {
      PLOG(ERROR) << "Couldn't create " << vmlog_dir.value();
      return;
    }
  }
  if (!base::SetPosixFilePermissions(vmlog_dir, 0755)) {
    PLOG(ERROR) << "Couldn't set permissions for " << vmlog_dir.value();
  }
  Init(vmlog_dir, log_interval);
}

void VmlogWriter::Init(const base::FilePath& vmlog_dir,
                       const base::TimeDelta& log_interval) {
  base::Time now = base::Time::Now();

  // If the current time is within a day of the epoch, we probably don't have a
  // good time set for naming files. Wait 5 minutes.
  //
  // See crbug.com/724175 for details.
  if (now - base::Time::UnixEpoch() < base::TimeDelta::FromDays(1)) {
    LOG(WARNING) << "Time seems incorrect, too close to epoch: " << now;
    valid_time_delay_timer_.Start(
        FROM_HERE, base::TimeDelta::FromMinutes(5),
        base::Bind(&VmlogWriter::Init, base::Unretained(this), vmlog_dir,
                   log_interval));
    return;
  }

  base::FilePath vmlog_current_path =
      vmlog_dir.Append("vmlog." + brillo::GetTimeAsLogString(now));
  base::FilePath vmlog_rotated_path =
      vmlog_dir.Append("vmlog.1." + brillo::GetTimeAsLogString(now));

  brillo::UpdateLogSymlinks(vmlog_dir.Append("vmlog.LATEST"),
                            vmlog_dir.Append("vmlog.PREVIOUS"),
                            vmlog_current_path);

  base::DeleteFile(vmlog_dir.Append("vmlog.1.PREVIOUS"), false);
  if (base::PathExists(vmlog_dir.Append("vmlog.1.LATEST"))) {
    base::Move(vmlog_dir.Append("vmlog.1.LATEST"),
               vmlog_dir.Append("vmlog.1.PREVIOUS"));
  }

  vmstat_stream_.open("/proc/vmstat", std::ifstream::in);
  if (vmstat_stream_.fail()) {
    PLOG(ERROR) << "Couldn't open /proc/vmstat";
    return;
  }

  proc_stat_stream_.open("/proc/stat", std::ifstream::in);
  if (proc_stat_stream_.fail()) {
    PLOG(ERROR) << "Couldn't open /proc/stat";
    return;
  }

  if (!log_interval.is_zero()) {
    timer_.Start(FROM_HERE, log_interval, this, &VmlogWriter::WriteCallback);
  }

  const int n_cpus = sysconf(_SC_NPROCESSORS_ONLN);
  for (int cpu = 0; cpu != n_cpus; ++cpu) {
    std::ostringstream path;
    path << "/sys/devices/system/cpu/cpufreq/policy" << cpu
         << "/scaling_cur_freq";
    std::ifstream cpufreq_stream(path.str());
    if (cpufreq_stream) {
      cpufreq_streams_.push_back(std::move(cpufreq_stream));
    } else {
      PLOG(WARNING) << "Failed to open scaling_cur_freq for logical core "
                    << cpu;
    }
  }

  amdgpu_sclk_stream_.open("/sys/class/drm/card0/device/pp_dpm_sclk");

  std::ostringstream header(kVmlogHeader, std::ios_base::ate);
  if (amdgpu_sclk_stream_)
    header << " gpufreq";

  for (int cpu = 0; cpu != cpufreq_streams_.size(); ++cpu) {
    header << " cpufreq" << cpu;
  }
  header << "\n";

  vmlog_.reset(new VmlogFile(vmlog_current_path, vmlog_rotated_path,
                             kMaxVmlogFileSize, header.str()));
}

VmlogWriter::~VmlogWriter() = default;

bool VmlogWriter::GetCpuUsage(double* cpu_usage_out) {
  proc_stat_stream_.clear();
  if (!proc_stat_stream_.seekg(0, std::ios_base::beg)) {
    PLOG(ERROR) << "Unable to seekg() /proc/stat";
    return false;
  }
  CpuTimeRecord cur;
  ParseCpuTime(&proc_stat_stream_, &cur);
  if (cur.total_time_ == prev_cputime_record_.total_time_) {
    LOG(WARNING) << "Same total time for two consecutive calls to GetCpuUsage";
    return false;
  }
  *cpu_usage_out =
      (cur.non_idle_time_ - prev_cputime_record_.non_idle_time_) /
      static_cast<double>(cur.total_time_ - prev_cputime_record_.total_time_);
  prev_cputime_record_ = cur;
  return true;
}

bool VmlogWriter::GetDeltaVmStat(VmstatRecord* delta_out) {
  // Reset the Vmstat stream.
  vmstat_stream_.clear();
  if (!vmstat_stream_.seekg(0, std::ios_base::beg)) {
    PLOG(ERROR) << "Unable to seekg() /proc/vmstat";
    return false;
  }

  // Get current Vmstat.
  VmstatRecord r;
  if (!VmStatsParseStats(&vmstat_stream_, &r)) {
    LOG(ERROR) << "Unable to parse vmstat data";
    return false;
  }

  delta_out->page_faults_ = r.page_faults_ - prev_vmstat_record_.page_faults_;
  delta_out->file_page_faults_ =
      r.file_page_faults_ - prev_vmstat_record_.file_page_faults_;
  delta_out->anon_page_faults_ =
      r.anon_page_faults_ - prev_vmstat_record_.anon_page_faults_;
  delta_out->swap_in_ = r.swap_in_ - prev_vmstat_record_.swap_in_;
  delta_out->swap_out_ = r.swap_out_ - prev_vmstat_record_.swap_out_;
  prev_vmstat_record_ = r;
  return true;
}

bool ParseAmdgpuFrequency(std::ostream& out, std::istream& sclk_stream) {
  // Note: pcrecpp::RE is thread-safe, and FullMatch() is re-entrant.
  static const pcrecpp::RE* amdgpu_sclk_expression =
      new pcrecpp::RE(R"(^\d: (\d{2,4})Mhz \*$)");

  std::string line;
  while (std::getline(sclk_stream, line)) {
    std::string frequency_mhz;
    if (amdgpu_sclk_expression->FullMatch(line, &frequency_mhz)) {
      out << " " << frequency_mhz;
      return true;
    }
  }

  PLOG(ERROR) << "Unable to recognize GPU frequency";
  return false;
}

bool VmlogWriter::GetAmdgpuFrequency(std::ostream& out) {
  if (!amdgpu_sclk_stream_) {
    // Nothing to do if the sysfs entry is not present.
    return true;
  }
  if (!amdgpu_sclk_stream_.seekg(0, std::ios_base::beg)) {
    PLOG(ERROR) << "Unable to seek pp_dpm_sclk";
    return false;
  }

  return ParseAmdgpuFrequency(out, amdgpu_sclk_stream_);
}

bool VmlogWriter::GetCpuFrequencies(std::ostream& out) {
  for (std::ifstream& cpufreq_stream : cpufreq_streams_) {
    if (!cpufreq_stream.seekg(0, std::ios_base::beg)) {
      PLOG(ERROR) << "Unable to seek scaling_cur_freq";
      return false;
    }

    std::string result;
    cpufreq_stream >> result;
    out << " " << result;
  }
  return true;
}

void VmlogWriter::WriteCallback() {
  VmstatRecord delta_vmstat;
  double cpu_usage;
  if (!GetDeltaVmStat(&delta_vmstat) || !GetCpuUsage(&cpu_usage)) {
    LOG(ERROR) << "Stop timer because of error reading system info";
    timer_.Stop();
    return;
  }

  timeval tv;
  gettimeofday(&tv, nullptr);
  struct tm tm_time;
  localtime_r(&tv.tv_sec, &tm_time);
  std::ostringstream out_line;
  out_line << base::StringPrintf(
      "[%02d%02d/%02d%02d%02d]"
      " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %.2f",
      tm_time.tm_mon + 1, tm_time.tm_mday,              //
      tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,  //
      delta_vmstat.page_faults_, delta_vmstat.file_page_faults_,
      delta_vmstat.anon_page_faults_, delta_vmstat.swap_in_,
      delta_vmstat.swap_out_, cpu_usage);

  if (!GetAmdgpuFrequency(out_line) || !GetCpuFrequencies(out_line)) {
    LOG(ERROR) << "Stop timer because of error reading system info";
    timer_.Stop();
  }
  out_line << "\n";

  if (!vmlog_->Write(out_line.str())) {
    LOG(ERROR) << "Writing to vmlog failed.";
    timer_.Stop();
    return;
  }
}

}  // namespace chromeos_metrics
