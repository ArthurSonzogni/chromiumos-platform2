// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_AUDIO_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_AUDIO_FETCHER_H_

#include <base/optional.h>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The AudioFetcher class is responsible for gathering audio info reported
// by cros_healthd. Info is fetched via cras.
class AudioFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  // Returns a structure with either the device's audio info or the error that
  // occurred fetching the information.
  chromeos::cros_healthd::mojom::AudioResultPtr FetchAudioInfo();

 private:
  using OptionalProbeErrorPtr =
      base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr>;

  OptionalProbeErrorPtr PopulateMuteInfo(
      chromeos::cros_healthd::mojom::AudioInfo* info);
  OptionalProbeErrorPtr PopulateActiveNodeInfo(
      chromeos::cros_healthd::mojom::AudioInfo* info);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_AUDIO_FETCHER_H_
