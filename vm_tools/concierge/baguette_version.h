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
constexpr char kBaguetteVersion[] = "2025-12-10-000152_64cf64be564fb6bf5365aaa7f31d08abed860528";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "af01dc17d45893ff2abd82d6c21232d3599994c81c89b6198e3bb862420da84c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "cb766fab93acbcd2ba53a823f02a691e513f8637dac61e7219303924ba28238b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
