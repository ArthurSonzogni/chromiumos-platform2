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
constexpr char kBaguetteVersion[] = "2025-05-21-000128_6e4023e742b346d76a798d8790ae4f6f81b115fc";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e7525cf232010e71b74a09be7f771e3a65410796f7344dbe821271560da378c0";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4f41a1c77214564dee00451328b9ea55ba22836fb3b4c4735107b956ba3c80bd";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
