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
constexpr char kBaguetteVersion[] = "2026-02-08-000136_2a8df146d461582cbcc58f2b354ae07271b7da1a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "8a57abe352243aaf1fd93d071d0ed23278b7da6e6ab8f0aa7aae2ca1e44ab38a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b8074536c4b5d026f215900395db17c0c28bb9f4d3d48e620e027998590093b6";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
