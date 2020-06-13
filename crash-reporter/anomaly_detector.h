// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// anomaly_detector examines the log files, namely /var/log/messages,
// /var/log/upstart.log, and /var/log/audit/audit.log, using
// anomaly::LogReader and looks for messages matching particular patterns. When
// it finds one, it invokes crash_reporter appropriately to report the issue.

// This file (and the associated .cc file) contains logic to parse log
// entries and determine whether to invoke crash_reporter (or how to invoke it).
// The logic to read from plaintext files lives in
// anomaly_detector_text_file_reader.h and anomaly_detector_log_reader.h. The
// logic to setup LogReader, pass entries to corresponding parser and execute
// crash_reporter lives in anomaly_detector_main.cc.

#ifndef CRASH_REPORTER_ANOMALY_DETECTOR_H_
#define CRASH_REPORTER_ANOMALY_DETECTOR_H_

#include <base/optional.h>
#include <base/time/clock.h>
#include <base/time/time.h>
#include <dbus/bus.h>
#include <metrics/metrics_library.h>

#include <memory>
#include <string>
#include <vector>

#include <inttypes.h>

namespace anomaly {

struct CrashReport {
  std::string text;
  std::string flag;
};

using MaybeCrashReport = base::Optional<CrashReport>;

constexpr size_t HASH_BITMAP_SIZE(1 << 15);

class Parser {
 public:
  virtual ~Parser() = 0;

  virtual MaybeCrashReport ParseLogEntry(const std::string& line) = 0;

  virtual bool WasAlreadySeen(uint32_t hash);

  // Called once every 10-20 seconds to allow Parser to update state in ways
  // that aren't always tied to receiving a message.
  virtual void PeriodicUpdate();

 protected:
  enum class LineType {
    None,
    Header,
    Start,
    Body,
  };

 private:
  std::bitset<HASH_BITMAP_SIZE> hash_bitmap_;
};

class ServiceParser : public Parser {
 public:
  MaybeCrashReport ParseLogEntry(const std::string& line) override;
};

class SELinuxParser : public Parser {
 public:
  MaybeCrashReport ParseLogEntry(const std::string& line) override;
};

class KernelParser : public Parser {
 public:
  MaybeCrashReport ParseLogEntry(const std::string& line) override;

 private:
  LineType last_line_ = LineType::None;
  std::string text_;
  std::string flag_;

  // Timestamp of last time crash_reporter failed.
  base::TimeTicks crash_reporter_last_crashed_ = base::TimeTicks();
};

class SuspendParser : public Parser {
 public:
  MaybeCrashReport ParseLogEntry(const std::string& line) override;

 private:
  LineType last_line_ = LineType::None;
  std::string dev_str_;
  std::string errno_str_;
  std::string step_str_;
};

// Collector for journal entries from our own crash_reporter. Unlike other
// collectors, this doesn't actually ever create crash reports -- ParseLogEntry
// always returns nullopt. Instead, it produces UMA metrics that track how well
// Chrome's crash handlers (breakpad or crashpad) are working. If Chrome gets
// a segfault or such, its internal crash handler should invoke crash_reporter
// directly. Once the internal crash handler is done, the kernel should also
// invoke crash_reporter via the normal core pattern file. Both of these produce
// distinct log entries. By matching these up, we can detect how often the
// internal crash handler is failing to invoke crash_reporter. In particular, if
// we see an invoked-by-kernel message without a corresponding invoking-directly
// message, Chrome's crash handler failed. We record the number of unmatched
// invoked-by-kernel messages, and, for a denominator, we record the total
// number of invoked-by-kernel messages.
//
// (There are some cases -- "dump without crashing" -- in which Chrome will
// invoke crash_reporter but will not actually crash, and so will not produce
// an invoked-by-kernel message. This is why we go to the trouble of actually
// matching up messages from the log, instead of just counting the number of
// invoked-directly and invoked-from-kernel events. The "dump without crashing"
// events will overcount the number of successes and hide the true number of
// failures. Therefore, we ignore "dump without crashing" crashes by not
// counting the number of invoked-by-Chrome messages we see, and not reporting
// the number of unmatched invoked-by-Chrome messages.)
class CrashReporterParser : public Parser {
 public:
  // We hold on to unmatched messages for at least this long before reporting
  // them as unmatched.
  static constexpr base::TimeDelta kTimeout = base::TimeDelta::FromSeconds(30);

  explicit CrashReporterParser(
      std::unique_ptr<base::Clock> clock,
      std::unique_ptr<MetricsLibraryInterface> metrics_lib);
  MaybeCrashReport ParseLogEntry(const std::string& line) override;
  void PeriodicUpdate() override;

 private:
  enum class Collector {
    // Log entry was from ChromeCollector.
    CHROME,

    // Log entry was from UserCollector.
    USER
  };

  struct UnmatchedCrash {
    int pid;
    base::Time timestamp;
    Collector collector;
  };

  std::unique_ptr<base::Clock> clock_;
  std::unique_ptr<MetricsLibraryInterface> metrics_lib_;
  std::vector<UnmatchedCrash> unmatched_crashes_;
};

class TerminaParser {
 public:
  explicit TerminaParser(scoped_refptr<dbus::Bus> dbus);
  MaybeCrashReport ParseLogEntry(const std::string& tag,
                                 const std::string& line);

 private:
  scoped_refptr<dbus::Bus> dbus_;
};

}  // namespace anomaly

#endif  // CRASH_REPORTER_ANOMALY_DETECTOR_H_
