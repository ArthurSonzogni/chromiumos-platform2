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
constexpr char kBaguetteVersion[] = "2026-04-02-000107_f4dd9bbb601afcc292532b6b76aa9bcca9f8bba8";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "bb66f246fab8ceca9f3553b9c29a7fb909499d2d953c82ac69e88a8218b53176";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "fc69b816eda40eb1ef9c27a1483e4446ab377b7c8d0d2c232adfc921a81f3900";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
