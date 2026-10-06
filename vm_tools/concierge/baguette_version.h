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
constexpr char kBaguetteVersion[] = "2026-10-06-000107_d6717d4db347c31e3621d8eaa8617b16d8595e38";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "041ca57a88028a21b5246c04b4c52d5abcc9d512768425cadb9bc04efcf47d91";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a8420516acc48c24237369832e46cf74a24cf8984e9624c24f137f7e9e5f59c3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
