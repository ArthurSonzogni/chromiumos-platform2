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
constexpr char kBaguetteVersion[] = "2026-02-10-000103_954c2979f4fa50b77f7f9eec9da459e100303497";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6fd2407a428c6dac7eae745543437880961ae4f547d91136829d98c18f9018a1";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b1065735860d960bc7f5d34ec26ed0e8fb809b4746820d21d5690cd19e911508";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
