// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_FLEX_HWIS_SERVER_INFO_H_
#define FLEX_HWIS_FLEX_HWIS_SERVER_INFO_H_

#if USE_FLEX_INTERNAL
#include "flex_hwis_private/server_info.h"
#endif

#include <string_view>

namespace flex_hwis {

// kServerUrl points to the hwis server, which can be used to register,
// update, and delete device hardware data.
// kApiKey is used to do authorization of the client application that
// is making requests to the hwis server.
#if USE_FLEX_INTERNAL
inline constexpr std::string_view kServerUrl = flex_hwis_private::kServerUrl;
inline constexpr std::string_view kApiKey = flex_hwis_private::kApiKey;
#else
inline constexpr std::string_view kServerUrl = "";
inline constexpr std::string_view kApiKey = "";
#endif  // USE_FLEX_INTERNAL

}  // namespace flex_hwis

#endif  // FLEX_HWIS_FLEX_HWIS_SERVER_INFO_H_
