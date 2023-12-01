// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_ZRAM_RECOMPRESSION_H_
#define SWAP_MANAGEMENT_ZRAM_RECOMPRESSION_H_

#include "swap_management/utils.h"

#include <string>

#include <absl/status/status.h>
#include <base/memory/weak_ptr.h>
#include <base/timer/timer.h>
#include <chromeos/dbus/swap_management/dbus-constants.h>

namespace swap_management {

class ZramRecompression {
 public:
  static ZramRecompression* Get();

  absl::Status EnableRecompression();
  absl::Status SetZramRecompressionConfigIfOverriden(const std::string& key,
                                                     const std::string& value);
  void Start();
  void Stop();

 private:
  ZramRecompression() = default;
  ZramRecompression& operator=(const ZramRecompression&) = delete;
  ZramRecompression(const ZramRecompression&) = delete;

  ~ZramRecompression();

  friend class MockZramRecompression;

  // There are only one zram recompression instance in current setup.
  friend ZramRecompression** GetSingleton<ZramRecompression>();

  absl::Status InitiateRecompression(ZramRecompressionMode mode);
  void PeriodicRecompress();

  struct ZramRecompressionParams {
    std::string recomp_algorithm = "zstd";
    base::TimeDelta periodic_time = base::Minutes(5);
    base::TimeDelta backoff_time = base::Minutes(5);
    uint64_t threshold_mib = 1024;
    bool recompression_huge_idle = true;
    bool recompression_idle = true;
    bool recompression_huge = true;
    base::TimeDelta idle_min_time = base::Hours(1);
    base::TimeDelta idle_max_time = base::Hours(6);

    friend std::ostream& operator<<(std::ostream& out,
                                    const ZramRecompressionParams& p) {
      out << "[";
      out << "periodic_time=" << p.periodic_time << " ";
      out << "backoff_time=" << p.backoff_time << " ";
      out << "threshold_mib=" << p.threshold_mib << " ";
      out << "recompression_huge_idle=" << p.recompression_huge_idle << " ";
      out << "recompression_idle=" << p.recompression_idle << " ";
      out << "recompression_huge=" << p.recompression_huge << " ";
      out << "idle_min_time=" << p.idle_min_time << " ";
      out << "idle_max_time=" << p.idle_max_time << " ";
      out << "]";

      return out;
    }
  } params_;

  bool is_currently_recompressing_ = false;
  base::Time last_recompression_ = base::Time::Min();
  base::WeakPtrFactory<ZramRecompression> weak_factory_{this};
};

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_ZRAM_RECOMPRESSION_H_
