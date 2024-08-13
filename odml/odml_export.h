// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ODML_EXPORT_H_
#define ODML_ODML_EXPORT_H_

// Use this for any class or function that needs to be exported from
// odml. E.g. ODML_EXPORT void foo();
#define ODML_EXPORT __attribute__((__visibility__("default")))

#endif  // ODML_ODML_EXPORT_H_
