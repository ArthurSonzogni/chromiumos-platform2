// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREEN_TYPES_H_
#define MINIOS_SCREEN_TYPES_H_

namespace minios {

// All the different screens in the MiniOs Flow. `kDownloadError` is shown when
// there is an Update Engine failure, `kNetworkError` is shown when there is an
// issue getting the networks. `kPasswordError` and `kConnectionError` are shown
// upon failures connecting to a chosen network.
enum class ScreenType {
  kWelcomeScreen = 0,
  kNetworkDropDownScreen = 1,
  // TODO(vyshu): Remove `kExpandedNetworkDropDownScreen` and `kPasswordScreen`
  // after screens.h is removed. These are internal states of
  // `kNetworkDropDownScreen`.
  kExpandedNetworkDropDownScreen = 2,
  kPasswordScreen = 3,
  kLanguageDropDownScreen = 4,
  // TODO(vyshu): Remove `kWaitForConnection` after screens.h is removed. These
  // are internal states of `kNetworkDropDownScreen`.
  kWaitForConnection = 5,
  kUserPermissionScreen = 6,
  kStartDownload = 7,
  kDownloadError = 8,
  kNetworkError = 9,
  kPasswordError = 10,
  kConnectionError = 11,
  kGeneralError = 12,
  kDebugOptionsScreen = 13,
  kLogScreen = 14,
};

}  // namespace minios

#endif  // MINIOS_SCREEN_TYPES_H_
