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
constexpr char kBaguetteVersion[] = "2025-08-25-000133_924a53b596f7d057798db53d913d3a54f938c7f3";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9cd2b772c04af8618cbc6e8edad0d2ff243535c57b5930e95d9c94115c2bf6cb";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "334a23f25e24eb85db96981da4d9b869b8a5a46c6d2613d907769672b368b956";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
