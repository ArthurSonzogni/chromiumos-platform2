// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview This file will be dynamic imported under CCA page with access
 * to cca_test.js to interact with the app.
 */

/**
 * @param message {string}
 */
export function log(message) {
  console.log('[CCA CLI]', message);
}
