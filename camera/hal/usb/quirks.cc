/*
 * Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb/quirks.h"

#include <map>
#include <utility>

#include <base/no_destructor.h>

namespace cros {

namespace {

using VidPidPair = std::pair<std::string, std::string>;
using QuirksMap = std::map<VidPidPair, uint32_t>;

const QuirksMap& GetQuirksMap() {
  static const base::NoDestructor<QuirksMap> kQuirksMap({
      // Logitech Webcam Pro 9000 (b/138159048)
      {{"046d", "0809"}, kQuirkPreferMjpeg},
      // Huddly GO (crbug.com/1010557)
      {{"2bd9", "0011"}, kQuirkRestartOnTimeout},
      // Liteon 5M AF 6BA502N2 (b/147397859)
      {{"0bda", "5646"}, kQuirkReportLeastFpsRanges},
      // Liteon AR CCD 8BA842N2A (b/147397859)
      {{"0bda", "5647"}, kQuirkReportLeastFpsRanges},
      // Genesys Logic, Inc. (b/160544169)
      {{"05e3", "f11a"}, kQuirkReportLeastFpsRanges},
      // Logitech Tap HDMI Capture (b/146590270)
      {{"046d", "0876"}, kQuirkRestartOnTimeout},
      // Magewell USB Capture HDMI (b/262885305)
      {{"2935", "0006"}, kQuirkRestartOnTimeout},
      // IPEVO Ziggi-HD Plus
      {{"1778", "0225"}, kQuirkDisableFrameRateSetting},
      // Chicony CNFFH37 (b/158957477)
      {{"0c45", "6a05"}, kQuirkUserSpaceTimestamp},
      // HoverCam Solo 8 Plus document camera (b/171609393)
      {{"2894", "0029"}, kQuirkReportLeastFpsRanges},
      // LVI Camera MagniLink S (crbug.com/1197426)
      {{"1904", "0001"}, kQuirkReportLeastFpsRanges},
      // Chicony/CNFKH7521003210LH (b/185993364)
      {{"04f2", "b72f"}, kQuirkReportLeastFpsRanges},
      // Chicony Integrated IR Camera (b/223587315)
      {{"04f2", "b615"}, kQuirkInfrared},
      // Sunplus Innovation Technology Inc. USB2.0 UVC HD Webcam (b/269094788)
      {{"1bcf", "2cb5"}, kQuirkRestartOnTimeout},
      // Kingcome KPNB752 (b/326004301)
      {{"2b7e", "b752"}, kQuirkUserSpaceTimestamp},
      // Foxlink FO10FF-863H-5 (b/359087839)
      {{"05c8", "0b10"}, kQuirkUserSpaceTimestamp},
      // Google Inc. Lattice USB 3.0 Video Bridge (b/354766714)
      {{"18d1", "800a"}, kQuirkExpectTimeout},
      // Google Inc. Plankton Captured HDMI Video (b/354766714)
      {{"18d1", "501e"}, kQuirkExpectTimeout | kQuirkExpectHotplugWhileOpen},
      // Series One Video Input (Endeavour) (b/354766714)
      {{"18d1", "8006"}, kQuirkExpectHotplugWhileOpen},
      // TFC 1YHIZZZ0009 (YHIG) (b/374232012)
      {{"0408", "548f"}, kQuirkUserSpaceTimestamp},
      // Shinetech ASUS FHD webcam (b/381010970)
      {{"3277", "0094"}, kQuirkUserSpaceTimestamp},
  });
  return *kQuirksMap;
}

}  // namespace

uint32_t GetQuirks(const std::string& vid, const std::string& pid) {
  const QuirksMap& quirks_map = GetQuirksMap();
  auto it = quirks_map.find({vid, pid});
  if (it != quirks_map.end()) {
    return it->second;
  }
  return 0;
}

}  // namespace cros
