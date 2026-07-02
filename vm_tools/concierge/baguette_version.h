// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
#define VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

// This constant points to the image downloaded for new installations of
// Baguette.
// TODO(crbug.com/393151776): Point to luci recipe and builders that update this
// URL when new images are available.

// clang-format off
constexpr char kBaguetteVersion[] = "2026-07-02-000128_f6e2157ce3acba0329aeeaaec4aeb30bd242d33d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6491de7cb6ada3dcb1fd492b44ea1daa09be8d329039c2bed92669fca0c540ee";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "3203d627b17f26c965a4bb62e0214b2209cde58e8c0265adb433c0265441384a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
