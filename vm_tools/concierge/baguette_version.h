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
constexpr char kBaguetteVersion[] = "2026-01-14-000123_089773b2012f37ff79e656e5a2f64ffbb16ad970";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "7be5d01ed6938f7321312dde6990d8f9596165c676cab7710a3c1e8e0a8df320";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5f4f8889608a99b3af32c3445d0f5f09b888945fad0ba7f78b0bdddd4d5b5520";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
