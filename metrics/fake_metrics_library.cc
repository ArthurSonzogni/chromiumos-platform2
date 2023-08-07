// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/fake_metrics_library.h"

#include "base/time/time.h"

void FakeMetricsLibrary::Init() {}

bool FakeMetricsLibrary::AreMetricsEnabled() {
  return true;
}

bool FakeMetricsLibrary::IsAppSyncEnabled() {
  return true;
}

bool FakeMetricsLibrary::IsGuestMode() {
  return false;
}

bool FakeMetricsLibrary::SendToUMA(
    const std::string& name, int sample, int min, int max, int nbuckets) {
  return SendRepeatedToUMA(name, sample, min, max, nbuckets, 1);
}

bool FakeMetricsLibrary::SendRepeatedToUMA(const std::string& name,
                                           int sample,
                                           int min,
                                           int max,
                                           int nbuckets,
                                           int num_samples) {
  metrics_[name].insert(metrics_[name].end(), num_samples, sample);
  return true;
}

bool FakeMetricsLibrary::SendEnumToUMA(const std::string& name,
                                       int sample,
                                       int exclusive_max) {
  return SendRepeatedEnumToUMA(name, sample, exclusive_max, 1);
}

bool FakeMetricsLibrary::SendRepeatedEnumToUMA(const std::string& name,
                                               int sample,
                                               int exclusive_max,
                                               int num_samples) {
  metrics_[name].insert(metrics_[name].end(), num_samples, sample);
  return true;
}

bool FakeMetricsLibrary::SendLinearToUMA(const std::string& name,
                                         int sample,
                                         int max) {
  return SendRepeatedLinearToUMA(name, sample, max, 1);
}

bool FakeMetricsLibrary::SendRepeatedLinearToUMA(const std::string& name,
                                                 int sample,
                                                 int max,
                                                 int num_samples) {
  metrics_[name].insert(metrics_[name].end(), num_samples, sample);
  return true;
}

bool FakeMetricsLibrary::SendPercentageToUMA(const std::string& name,
                                             int sample) {
  return SendRepeatedPercentageToUMA(name, sample, /*num_samples=*/1);
}

bool FakeMetricsLibrary::SendRepeatedPercentageToUMA(const std::string& name,
                                                     int sample,
                                                     int num_samples) {
  metrics_[name].insert(metrics_[name].end(), num_samples, sample);
  return true;
}

bool FakeMetricsLibrary::SendBoolToUMA(const std::string& name, bool sample) {
  return SendRepeatedBoolToUMA(name, sample, /*num_samples=*/1);
}

bool FakeMetricsLibrary::SendRepeatedBoolToUMA(const std::string& name,
                                               bool sample,
                                               int num_samples) {
  metrics_[name].insert(metrics_[name].end(), num_samples, sample ? 1 : 0);
  return true;
}

bool FakeMetricsLibrary::SendSparseToUMA(const std::string& name, int sample) {
  return SendRepeatedSparseToUMA(name, sample, /*num_samples=*/1);
}

bool FakeMetricsLibrary::SendRepeatedSparseToUMA(const std::string& name,
                                                 int sample,
                                                 int num_samples) {
  metrics_[name].insert(metrics_[name].end(), num_samples, sample);
  return true;
}

bool FakeMetricsLibrary::SendUserActionToUMA(const std::string& action) {
  return false;
}

bool FakeMetricsLibrary::SendRepeatedUserActionToUMA(const std::string& action,
                                                     int num_samples) {
  return false;
}

bool FakeMetricsLibrary::SendCrashToUMA(const char* crash_kind) {
  return false;
}

bool FakeMetricsLibrary::SendRepeatedCrashToUMA(const char* crash_kind,
                                                int num_samples) {
  return false;
}

bool FakeMetricsLibrary::SendCrosEventToUMA(const std::string& event) {
  return false;
}

bool FakeMetricsLibrary::SendRepeatedCrosEventToUMA(const std::string& event,
                                                    int num_samples) {
  return false;
}

bool FakeMetricsLibrary::SendTimeToUMA(std::string_view name,
                                       base::TimeDelta sample,
                                       base::TimeDelta min,
                                       base::TimeDelta max,
                                       size_t num_buckets) {
  return SendRepeatedTimeToUMA(name, sample, min, max, num_buckets,
                               /*num_samples=*/1);
}

bool FakeMetricsLibrary::SendRepeatedTimeToUMA(std::string_view name,
                                               base::TimeDelta sample,
                                               base::TimeDelta min,
                                               base::TimeDelta max,
                                               size_t num_buckets,
                                               int num_samples) {
  return SendRepeatedToUMA(std::string(name), sample.InMilliseconds(),
                           min.InMilliseconds(), max.InMilliseconds(),
                           num_buckets, num_samples);
}

void FakeMetricsLibrary::SetOutputFile(const std::string& output_file) {}

// Test Getters

std::vector<int> FakeMetricsLibrary::GetCalls(const std::string& name) {
  return metrics_[name];
}

size_t FakeMetricsLibrary::NumCalls(const std::string& name) {
  return GetCalls(name).size();
}

int FakeMetricsLibrary::GetLast(const std::string& name) {
  std::vector<int> calls = GetCalls(name);
  if (calls.empty()) {
    return kInvalid;
  }
  return calls.back();
}

void FakeMetricsLibrary::Clear() {
  metrics_.clear();
}
