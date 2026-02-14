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
constexpr char kBaguetteVersion[] = "2026-02-14-000116_4cb98470df2ec4e88b36838a7e7450e2397019cc";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "2a4c01ab10e557aaaab46c5fff8e48e69ccfe21b9cd7041efe628cb2eb12fe25";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b5e1d1409019042920f943abed9040f3722939a20921a885b01164e1bc9d0acc";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
