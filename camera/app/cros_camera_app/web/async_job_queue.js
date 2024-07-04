// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @typedef {'drop'|'enqueue'|'keepLatest'} AsyncJobQueueMode
 */

/**
 * @template T
 * @typedef {Promise<T>|T} Awaitable
 */

/**
 * @typedef {Object} PendingJob
 * @property {function(): Promise<void>|void} job
 * @property {function(): void} resolve
 * @property {function(unknown): void} reject
 */

/**
 * @typedef {Object} AsyncJobInfo
 * @property {Promise<void>} result
 */

/**
 * Asynchronous job queue that supports different queuing behavior, and clearing
 * all pending jobs.
 */
class AsyncJobQueue {
  /**
   * @param {AsyncJobQueueMode} mode
   */
  constructor(mode = "enqueue") {
    /** @private {AsyncJobQueueMode} */
    this.mode = mode;

    /** @private {Promise<void>|null} */
    this.runningPromise = null;

    /** @private {PendingJob[]} */
    this.pendingJobs = [];
  }

  /**
   * Handles all job in `queuedJobs`.
   *
   * This should only be called in the promise chain of `runningPromise`.
   * @private
   * @returns {Promise<void>}
   */
  async handlePendingJobs() {
    while (true) {
      const pendingJob = this.pendingJobs.shift();
      if (pendingJob === undefined) {
        break;
      }
      try {
        await pendingJob.job();
        pendingJob.resolve();
      } catch (e) {
        pendingJob.reject(e);
      }
    }
    this.runningPromise = null;
  }

  /**
   * Pushes the given job into queue.
   *
   * @param {function(): Awaitable<void>} job
   * @returns {AsyncJobInfo}
   */
  push(job) {
    if (this.runningPromise === null) {
      const result = Promise.resolve().then(() => job());
      this.runningPromise = result
        .catch((error) => {
          console.error(error);
        })
        .then(() => this.handlePendingJobs());
      return { result };
    }

    if (this.mode === "drop") {
      return { result: Promise.resolve() };
    }
    if (this.mode === "keepLatest") {
      this.clearInternal();
    }
    const result = new Promise((resolve, reject) => {
      this.pendingJobs.push({ job, resolve, reject });
    });
    return { result };
  }

  /**
   * Flushes the job queue.
   *
   * @returns {Promise<void>}
   */
  flush() {
    if (this.runningPromise === null) {
      return Promise.resolve();
    }
    return this.runningPromise;
  }

  /**
   * @private
   */
  clearInternal() {
    for (const job of this.pendingJobs) {
      job.resolve();
    }
    this.pendingJobs = [];
  }

  /**
   * Clears all not-yet-scheduled jobs and waits for current job finished.
   *
   * @returns {Promise<void>}
   */
  clear() {
    this.clearInternal();
    return this.flush();
  }
}

export { AsyncJobQueue };
