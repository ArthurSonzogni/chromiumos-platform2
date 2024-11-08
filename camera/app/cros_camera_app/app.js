// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview This file will be dynamic imported under CCA page with access
 * to cca_test.js to interact with the app.
 */

import {CcaTest} from 'chrome://camera-app/js/test/cca_test.js';

/**
 * @param message {string}
 */
export function log(message) {
  console.log('[CCA CLI]', message);
}

/**
 * Asserts the given condition.
 * @param {boolean} cond The condition.
 * @param {string=} message The message to show on failure.
 */
function assert(cond, message = 'Assertion failed') {
  if (!cond) {
    throw new Error(message);
  }
}

/**
 * Sleeps for the given duration.
 * @param {number} duration How long to sleep in milliseconds.
 * @return {!Promise<void>} Resolved when `duration` milliseconds is passed.
 */
async function sleep(duration) {
  return new Promise((resolve) => {
    setTimeout(resolve, duration);
  });
}

/**
 * Polls until the given function return a value instead of throwing an error.
 * If it failed to poll within the timeout, the last error would be thrown.
 * @template T
 * @param {function(): T} fn Returns an awaitable value or throws an error.
 * @param {{timeout?: number, interval?: number}=} opts Options in milliseconds.
 * @return {!Promise<Awaited<T>>} The return value of the given function.
 */
async function poll(fn, {timeout = 10000, interval = 10} = {}) {
  let lastError = null;
  const deadline = Date.now() + timeout;

  while (Date.now() < deadline) {
    try {
      const value = await fn();
      return value;
    } catch (e) {
      lastError = e;
    }
    await sleep(interval);
  }

  throw new Error(
      'Timed out polling',
      lastError !== null ? {cause: lastError} : {},
  );
}

/**
 * @param timeout {number}
 * @return {!Promise<void>} Resolved when video is active and not configuring.
 */
async function waitUntilVideoReady(timeout = 10000) {
  await poll(
      async () => {
        assert(await CcaTest.isVideoActive(), 'video is not active');
        assert(
            !CcaTest.getState('camera-configuring'),
            'camera is still configuring',
        );
      },
      {timeout},
  );
}

/**
 * @param resolution {{width: number, height: number}}
 */
export async function setVideoResolution(resolution) {
  await waitUntilVideoReady();
  const facing = await CcaTest.getFacing();
  assert(facing !== 'unknown');
  CcaTest.chooseVideoResolution(facing, resolution);
  await waitUntilVideoReady();
}
