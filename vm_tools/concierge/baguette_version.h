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
constexpr char kBaguetteVersion[] = "2026-09-18-000110_093db79314ff0a54b7ed2b454b9036e3107e38f4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c3515b627623ec81ea203b2dfc02e63f5a426bcd1110079bc702f812d7d5ad6a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "98c9d91b9cad2e04e7297af58398b51e64ba1bcc6730a80ef3f9edc6a6636a76";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
