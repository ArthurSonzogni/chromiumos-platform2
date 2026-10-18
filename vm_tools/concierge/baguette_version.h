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
constexpr char kBaguetteVersion[] = "2026-10-18-000107_c260ac4dfb4031857d3fea5c0d85d1dd756e58c7";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c2338b6db83e95541c5ad62cfb928cb40cbbd0d14ff8ea03f8c79d7b8a1f2472";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "9a7a77b7b066f57a047fb0fa3cc51ff99b59b8a5770c87dd2bdb2ae2aa8219ea";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
