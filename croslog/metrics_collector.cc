// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <deque>
#include <limits>
#include <string>
#include <vector>

#include <base/command_line.h>
#include <base/logging.h>
#include "base/files/file_path.h"
#include <base/files/file_util.h>
#include <base/files/file_enumerator.h>

#include <brillo/syslog_logging.h>
#include <metrics/metrics_library.h>

#include "croslog/constants.h"
#include "croslog/log_parser_audit.h"
#include "croslog/log_parser_syslog.h"
#include "croslog/metrics_collector_util.h"

namespace {

///////////////////////////////////////////////////////////////////////////////
// Min, max, bucket size of metricses:
//
// NOTE: these value can't be changed. If we want, we need to delete the
// existing metrics and create new one (eg. SystemLogTotalFileSize2).

// We expect the log file size is between 1MB to 4GB.
// Note these values can't be changed
const int kFileSizeMegabyteMetricsMin = 1;
const int kFileSizeMegabyteMetricsMax = 4 * 1024;
// 24 = log2(kFileSizeMegabyteMetricsMax) * 2
const int kFileSizeMegabyteMetricsNumberOfBuckets = 24;

///////////////////////////////////////////////////////////////////////////////
// Log path constants:

// Patterns to match with the specific logs:
const char kChromeLogFileNamePattern[] = "chrome_\?\?\?\?\?\?\?\?-\?\?\?\?\?\?";
const char kChromeUiLogFileNamePattern[] = "ui.\?\?\?\?\?\?\?\?-\?\?\?\?\?\?";

// Log files:
const base::FilePath kSystemLogDirectoryPath("/var/log/");
const base::FilePath kSystemChromeLogDirectoryPath("/var/log/chrome/");
const base::FilePath kSystemChromeUiLogDirectoryPath("/var/log/ui/");
const base::FilePath kSystemMessagesLogPath("/var/log/messages");
const base::FilePath kSystemNetLogPath("/var/log/net.log");
const base::FilePath kSystemAuditLogPath("/var/log/audit/audit.log");
const base::FilePath kSystemArcLogPath("/var/log/arc.log");
const base::FilePath kUserLogDirectoryPath("/home/chronos/user/log/");
const base::FilePath kUserChromeLogDirectoryPath("/home/chronos/user/log/");

///////////////////////////////////////////////////////////////////////////////
// Utility methods

int ByteToMB(int64_t bytes) {
  return bytes / 1024 / 1024;
}

void CalculateSysLogFileSizePerDayWithinDay(const base::FilePath& path,
                                            int64_t* byte_count_out) {
  base::Time count_after = base::Time::Now() - base::Days(1);
  croslog::CalculateLogMetrics(path, count_after,
                               std::make_unique<croslog::LogParserSyslog>(),
                               byte_count_out, nullptr, nullptr);
}

void CalculateAuditLogFileSizePerDayWithinDay(const base::FilePath& path,
                                              int64_t* byte_count_out) {
  base::Time count_after = base::Time::Now() - base::Days(1);
  croslog::CalculateLogMetrics(path, count_after,
                               std::make_unique<croslog::LogParserAudit>(),
                               byte_count_out, nullptr, nullptr);
}

void CalculateSystemChromeLogsByteCountWithinDay(int64_t* byte_count_out) {
  base::Time count_after = base::Time::Now() - base::Days(1);
  croslog::CalculateChromeLogMetrics(kSystemChromeLogDirectoryPath,
                                     kChromeLogFileNamePattern, count_after,
                                     byte_count_out, nullptr, nullptr);
  croslog::CalculateChromeLogMetrics(kSystemChromeUiLogDirectoryPath,
                                     kChromeUiLogFileNamePattern, count_after,
                                     byte_count_out, nullptr, nullptr);
}

}  // anonymous namespace

namespace metrics {

///////////////////////////////////////////////////////////////////////////////
// Metrics names:

const char kSystemNetLogFileSizePerDay[] = "Logging.SystemNetLogFileSizePerDay";

}  // namespace metrics

class MetricsCollector {
 public:
  MetricsCollector() { metrics_library_.Init(); }

  void Run() {
    // [Entire system log directory] Total file size.
    {
      int64_t system_log_total_size =
          ComputeDirectorySize(kSystemLogDirectoryPath);
      LOG(INFO) << "Total system log size: " << system_log_total_size
                << " bytes";
    }

    // [Major system log files] Number of entries per day and the maximum
    // throughput per minute.
    {
      int64_t max_throughput;
      int64_t entry_count;

      croslog::Multiplexer multiplexer;
      for (const auto& log_path_str : croslog::kLogSources) {
        base::FilePath path(log_path_str.data());
        if (!base::PathExists(path))
          continue;
        multiplexer.AddSource(
            path, std::make_unique<croslog::LogParserSyslog>(), false);
      }

      for (const auto& log_path_str : croslog::kAuditLogSources) {
        base::FilePath path(log_path_str.data());
        if (!base::PathExists(path))
          continue;
        multiplexer.AddSource(path, std::make_unique<croslog::LogParserAudit>(),
                              false);
      }

      base::Time count_after = base::Time::Now() - base::Days(1);
      croslog::CalculateMultipleLogMetrics(&multiplexer, count_after,
                                           &entry_count, &max_throughput);

      LOG(INFO) << "Total system log: " << entry_count << " entries per day.";

      LOG(INFO) << "Maximum throughput of system logs: " << max_throughput
                << " entries per minute.";
    }

    // [System "message" log] Byte count of logs per day.
    {
      int64_t byte_count_message;
      CalculateSysLogFileSizePerDayWithinDay(kSystemMessagesLogPath,
                                             &byte_count_message);
      if (byte_count_message != -1) {
        LOG(INFO) << "Total message (system) log: " << byte_count_message
                  << " bytes per day.";
      }
    }

    // [System "net" log] Byte count of logs per day.
    {
      int64_t byte_count_net;
      CalculateSysLogFileSizePerDayWithinDay(kSystemNetLogPath,
                                             &byte_count_net);
      if (byte_count_net != -1) {
        SendLogFileSizeToUMA(metrics::kSystemNetLogFileSizePerDay,
                             byte_count_net);
        LOG(INFO) << "Total net (system) log: " << byte_count_net
                  << " bytes per day.";
      }
    }

    // [System "audit" log] Byte count of logs per day.
    {
      int64_t byte_count_audit;
      CalculateAuditLogFileSizePerDayWithinDay(kSystemAuditLogPath,
                                               &byte_count_audit);
      if (byte_count_audit != -1) {
        LOG(INFO) << "Total audit (system) log: " << byte_count_audit
                  << " bytes per day.";
      }
    }

    // [System "ARC" log] Byte count of logs per day.
    {
      int64_t byte_count_arc;
      CalculateSysLogFileSizePerDayWithinDay(kSystemArcLogPath,
                                             &byte_count_arc);
      if (byte_count_arc != -1) {
        LOG(INFO) << "Total arc (system) log: " << byte_count_arc
                  << " bytes per day.";
      }
    }

    // [System chrome logs] Byte count of logs per day.
    {
      int64_t byte_count_chrome;
      CalculateSystemChromeLogsByteCountWithinDay(&byte_count_chrome);
      LOG(INFO) << "Total chrome (system) log: " << byte_count_chrome
                << " bytes per day.";
    }

    // [Entire user log directory] Total file size.
    {
      int64_t user_log_total_size = ComputeDirectorySize(kUserLogDirectoryPath);
      LOG(INFO) << "Total user log size: " << user_log_total_size << " bytes";
    }

    // [User chrome logs] Byte count of logs per day, and the maximum throughput
    // per minute.
    {
      int64_t max_throughput;
      int64_t entry_count;
      int64_t byte_count_chrome;

      base::Time count_after = base::Time::Now() - base::Days(1);
      croslog::CalculateChromeLogMetrics(
          kUserChromeLogDirectoryPath, kChromeLogFileNamePattern, count_after,
          &byte_count_chrome, &entry_count, &max_throughput);

      if (byte_count_chrome > 0) {
        LOG(INFO) << "Maximum throughput of user logs: " << max_throughput
                  << " entries per minute.";
        LOG(INFO) << "Total user log: " << entry_count << " entries per day.";
        LOG(INFO) << "Total chrome (user) log: " << byte_count_chrome
                  << " bytes per day.";
      }
    }
  }

 private:
  void SendLogFileSizeToUMA(const std::string& name, int64_t value_in_bytes) {
    metrics_library_.SendToUMA(
        name, ByteToMB(value_in_bytes), kFileSizeMegabyteMetricsMin,
        kFileSizeMegabyteMetricsMax, kFileSizeMegabyteMetricsNumberOfBuckets);
  }

  MetricsLibrary metrics_library_;
};

int main(int argc, char* argv[]) {
  // Configure the log destination.
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  std::unique_ptr<MetricsCollector> collector =
      std::make_unique<MetricsCollector>();
  collector->Run();
}
