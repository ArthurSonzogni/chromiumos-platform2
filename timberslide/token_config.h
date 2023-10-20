// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TIMBERSLIDE_TOKEN_CONFIG_H_
#define TIMBERSLIDE_TOKEN_CONFIG_H_

// Use a rarely used char in EC logging as tokenizer prefix.
// A printable char is preferred to aid in debugging efforts when
// using pigweeds tokenization python tool.
#define PW_TOKENIZER_NESTED_PREFIX_STR "`"

#endif  // TIMBERSLIDE_TOKEN_CONFIG_H_
