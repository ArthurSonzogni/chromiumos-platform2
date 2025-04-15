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
constexpr char kBaguetteVersion[] = "2025-04-15-000102_87443eaa7670f591ec154a847249d7274b472590";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "bf8917d21144ddc20eaea2aa62216e28b4313137ef03c273005cb91a1c9c48d4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "3bfd2598faa698869a6b23e11a3094eafdf4f8ed4310aee0873c06d152d8781e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
