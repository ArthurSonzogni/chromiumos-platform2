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
constexpr char kBaguetteVersion[] = "2025-09-09-000059_13d9506e97ba33c2fa850837c80cbcd952e3b8dc";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "5f0f769f0abc7f67ec2f4c2dd1a85a9584c7d6bbba007d960ac969337a4171ce";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "79951dccdacac034caef13f92fd62ab0f3838d375188ee1d49283fe5ecb8a05a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
