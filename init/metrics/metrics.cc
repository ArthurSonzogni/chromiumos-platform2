// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/metrics/metrics.h"

#include <base/check_op.h>
#include <base/time/time.h>

namespace init_metrics {

namespace {
static InitMetrics* g_metrics = nullptr;

// UMA stats supported by the metrics subsystem
constexpr char kSystemKeyStatus[] = "Platform.MountEncrypted.SystemKeyStatus";
constexpr char kEncryptionKeyStatus[] =
    "Platform.MountEncrypted.EncryptionKeyStatus";

}  // namespace

InitMetrics::InitMetrics(const std::string& output_file) {
  metrics_library_.SetOutputFile(output_file);
}

// static
void InitMetrics::Initialize(const std::string& output_file) {
  CHECK(!g_metrics);
  g_metrics = new InitMetrics(output_file);
}

// static
InitMetrics* InitMetrics::Get() {
  CHECK(g_metrics);
  return g_metrics;
}

MetricsLibrary* InitMetrics::GetInternal() {
  CHECK(g_metrics);
  return &g_metrics->metrics_library_;
}

// static
void InitMetrics::Reset() {
  CHECK(g_metrics);
  delete g_metrics;
  g_metrics = nullptr;
}

void InitMetrics::ReportSystemKeyStatus(
    mount_encrypted::EncryptionKey::SystemKeyStatus status) {
  metrics_library_.SendEnumToUMA(
      kSystemKeyStatus, static_cast<int>(status),
      static_cast<int>(
          mount_encrypted::EncryptionKey::SystemKeyStatus::kCount));
}

void InitMetrics::ReportEncryptionKeyStatus(
    mount_encrypted::EncryptionKey::EncryptionKeyStatus status) {
  metrics_library_.SendEnumToUMA(
      kEncryptionKeyStatus, static_cast<int>(status),
      static_cast<int>(
          mount_encrypted::EncryptionKey::EncryptionKeyStatus::kCount));
}

}  // namespace init_metrics
