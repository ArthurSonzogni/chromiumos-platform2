// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_METRICS_LIBRARY_MOCK_H_
#define METRICS_METRICS_LIBRARY_MOCK_H_

#include <string>

#include "metrics/metrics_library.h"

#include <base/time/time.h>
#include <gmock/gmock.h>

class MetricsLibraryMock : public MetricsLibraryInterface {
 public:
  MOCK_METHOD(bool,
              SendToUMA,
              (const std::string&, int, int, int, int),
              (override));
  MOCK_METHOD(bool,
              SendRepeatedToUMA,
              (const std::string&, int, int, int, int, int),
              (override));

  MOCK_METHOD(bool, SendEnumToUMA, (const std::string&, int, int), (override));
  MOCK_METHOD(bool,
              SendRepeatedEnumToUMA,
              (const std::string&, int, int, int),
              (override));

  MOCK_METHOD(bool,
              SendLinearToUMA,
              (const std::string&, int, int),
              (override));
  MOCK_METHOD(bool,
              SendRepeatedLinearToUMA,
              (const std::string&, int, int, int),
              (override));

  MOCK_METHOD(bool, SendPercentageToUMA, (const std::string&, int), (override));
  MOCK_METHOD(bool,
              SendRepeatedPercentageToUMA,
              (const std::string&, int, int),
              (override));
  MOCK_METHOD(bool, SendBoolToUMA, (const std::string&, bool), (override));
  MOCK_METHOD(bool,
              SendRepeatedBoolToUMA,
              (const std::string&, bool, int),
              (override));
  MOCK_METHOD(bool, SendSparseToUMA, (const std::string&, int), (override));
  MOCK_METHOD(bool,
              SendRepeatedSparseToUMA,
              (const std::string&, int, int),
              (override));
  MOCK_METHOD(bool, SendUserActionToUMA, (const std::string&), (override));
  MOCK_METHOD(bool,
              SendRepeatedUserActionToUMA,
              (const std::string&, int),
              (override));
  MOCK_METHOD(bool, SendCrashToUMA, (const char* crash_kind), (override));
  MOCK_METHOD(bool,
              SendRepeatedCrashToUMA,
              (const char* crash_kind, int),
              (override));
  MOCK_METHOD(bool, SendCrosEventToUMA, (const std::string& event), (override));
  MOCK_METHOD(bool,
              SendRepeatedCrosEventToUMA,
              (const std::string& event, int),
              (override));
  MOCK_METHOD(bool,
              SendTimeToUMA,
              (std::string_view,
               base::TimeDelta,
               base::TimeDelta,
               base::TimeDelta,
               size_t),
              (override));
  MOCK_METHOD(bool,
              SendRepeatedTimeToUMA,
              (std::string_view,
               base::TimeDelta,
               base::TimeDelta,
               base::TimeDelta,
               size_t,
               int),
              (override));
  MOCK_METHOD(void, SetOutputFile, (const std::string&), (override));
  bool AreMetricsEnabled() override { return metrics_enabled_; }
  bool IsAppSyncEnabled() override { return appsync_enabled_; }
  bool IsGuestMode() override { return guest_mode_; }

  void set_metrics_enabled(bool value) { metrics_enabled_ = value; }
  void set_appsync_enabled(bool value) { appsync_enabled_ = value; }
  void set_guest_mode(bool value) { guest_mode_ = value; }

 private:
  bool metrics_enabled_ = true;
  bool appsync_enabled_ = true;
  bool guest_mode_ = false;
};

#endif  // METRICS_METRICS_LIBRARY_MOCK_H_
