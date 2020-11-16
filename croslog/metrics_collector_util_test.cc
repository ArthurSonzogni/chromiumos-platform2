// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/metrics_collector_util.h"

#include <string>

#include "base/files/file_path.h"
#include "gtest/gtest.h"

#include "croslog/log_parser_audit.h"
#include "croslog/log_parser_syslog.h"

namespace croslog {

class MetricsCollectorUtilTest : public ::testing::Test {
 public:
  MetricsCollectorUtilTest() = default;
  MetricsCollectorUtilTest(const MetricsCollectorUtilTest&) = delete;
  MetricsCollectorUtilTest& operator=(const MetricsCollectorUtilTest&) = delete;

  static base::Time TimeFromExploded(int year,
                                     int month,
                                     int day_of_month,
                                     int hour,
                                     int minute,
                                     int second,
                                     int microsec,
                                     int timezone_hour) {
    base::Time time;
    EXPECT_TRUE(base::Time::FromUTCExploded(
        base::Time::Exploded{year, month, 0, day_of_month, hour, minute, second,
                             0},
        &time));
    time += base::TimeDelta::FromMicroseconds(microsec);
    time -= base::TimeDelta::FromHours(timezone_hour);
    return time;
  }
};

TEST_F(MetricsCollectorUtilTest, CalculateLogMetrics) {
  {
    base::FilePath log_path("./testdata/TEST_AUDIT_LOG");

    int64_t max_throughput = 0;
    int64_t entry_count = 0;
    int64_t byte_count = 0;

    // base::Time count_after = base::Time::Now() -
    // base::TimeDelta::FromDays(1);
    CalculateLogMetrics(log_path, base::Time(),
                        std::make_unique<LogParserAudit>(), &byte_count,
                        &entry_count, &max_throughput);
    EXPECT_EQ(1561, byte_count);
    EXPECT_EQ(7, entry_count);
    EXPECT_EQ(3, max_throughput);
  }

  {
    base::FilePath log_path("./testdata/TEST_NORMAL_LOG1");

    int64_t max_throughput = 0;
    int64_t entry_count = 0;
    int64_t byte_count = 0;

    // base::Time count_after = base::Time::Now() -
    // base::TimeDelta::FromDays(1);
    CalculateLogMetrics(log_path, base::Time(),
                        std::make_unique<LogParserSyslog>(), &byte_count,
                        &entry_count, &max_throughput);
    EXPECT_EQ(330, byte_count);
    EXPECT_EQ(2, entry_count);
    EXPECT_EQ(2, max_throughput);
  }

  {
    base::FilePath log_path("./testdata/TEST_BOOT_ID_LOG");

    int64_t max_throughput = 0;
    int64_t entry_count = 0;
    int64_t byte_count = 0;

    // base::Time count_after = base::Time::Now() -
    // base::TimeDelta::FromDays(1);
    CalculateLogMetrics(log_path, base::Time(),
                        std::make_unique<LogParserSyslog>(), &byte_count,
                        &entry_count, &max_throughput);
    EXPECT_EQ(240, byte_count);
    EXPECT_EQ(3, entry_count);
    EXPECT_EQ(1, max_throughput);
  }

  {
    base::FilePath log_path("./testdata/TEST_BOOT_ID_LOG");

    int64_t max_throughput = 0;
    int64_t entry_count = 0;
    int64_t byte_count = 0;

    base::Time count_after = TimeFromExploded(2020, 7, 3, 16, 23, 24, 0, 9);
    CalculateLogMetrics(log_path, count_after,
                        std::make_unique<LogParserSyslog>(), &byte_count,
                        &entry_count, &max_throughput);
    EXPECT_EQ(80, byte_count);
    EXPECT_EQ(1, entry_count);
    EXPECT_EQ(1, max_throughput);
  }
}

TEST_F(MetricsCollectorUtilTest, CalculateMultipleLogMetrics) {
  {
    Multiplexer multiplexer;
    multiplexer.AddSource(base::FilePath("./testdata/TEST_BOOT_ID_LOG"),
                          std::make_unique<LogParserSyslog>(), false);

    int64_t max_throughput = 0;
    int64_t entry_count = 0;

    CalculateMultipleLogMetrics(&multiplexer, base::Time(), &entry_count,
                                &max_throughput);
    EXPECT_EQ(3, entry_count);
    EXPECT_EQ(1, max_throughput);
  }

  {
    Multiplexer multiplexer;
    multiplexer.AddSource(base::FilePath("./testdata/TEST_BOOT_ID_LOG"),
                          std::make_unique<LogParserSyslog>(), false);

    int64_t max_throughput = 0;
    int64_t entry_count = 0;

    base::Time count_after = TimeFromExploded(2020, 7, 3, 16, 23, 24, 0, 9);
    CalculateMultipleLogMetrics(&multiplexer, count_after, &entry_count,
                                &max_throughput);
    EXPECT_EQ(1, entry_count);
    EXPECT_EQ(1, max_throughput);
  }
}

TEST_F(MetricsCollectorUtilTest, CalculateChromeLogMetrics) {
  {
    int64_t byte_count = 0;
    int64_t max_throughput = 0;
    int64_t entry_count = 0;

    CalculateChromeLogMetrics(base::FilePath("./testdata/"),
                              "TEST_SEQUENTIAL_LOG?", base::Time(), &byte_count,
                              &entry_count, &max_throughput);
    EXPECT_EQ(444, byte_count);
    EXPECT_EQ(6, entry_count);
    EXPECT_EQ(2, max_throughput);
  }

  {
    int64_t byte_count = 0;
    int64_t max_throughput = 0;
    int64_t entry_count = 0;

    base::Time count_after = TimeFromExploded(2020, 5, 25, 14, 16, 0, 0, 9);
    CalculateChromeLogMetrics(base::FilePath("./testdata/"),
                              "TEST_SEQUENTIAL_LOG?", count_after, &byte_count,
                              &entry_count, &max_throughput);
    EXPECT_EQ(222, byte_count);
    EXPECT_EQ(3, entry_count);
    EXPECT_EQ(1, max_throughput);
  }
}

}  // namespace croslog
