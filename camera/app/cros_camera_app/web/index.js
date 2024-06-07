// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class FpsCounter {
  /**
   * @param {HTMLVideoElement} video
   * @param {number} len
   */
  constructor(video, len) {
    this.video = video;
    this.len = len;

    /** @type {number[]} */
    this.timestamps = [];

    /** @type {HTMLDivElement} */
    this.el = document.getElementById("fps-counter");
  }

  /**
   * @param {DOMHighResTimeStamp} now
   */
  update(now) {
    this.timestamps.push(now);
    if (this.timestamps.length >= this.len * 2) {
      this.timestamps = this.timestamps.slice(-this.len);
    }

    const count = Math.min(this.len, this.timestamps.length);
    const start = this.timestamps.at(-count);
    const fps = count === 0 ? 0 : ((count - 1) * 1000) / (now - start);
    this.el.textContent = `FPS: ${fps.toFixed(0)}`;

    this.video.requestVideoFrameCallback(this.update.bind(this));
  }

  start() {
    this.video.requestVideoFrameCallback(this.update.bind(this));
  }
}

class ResolutionStat {
  constructor() {
    this.el = document.getElementById("resolution-stat");
  }

  /**
   * @param {MediaStream} stream
   */
  update(stream) {
    const track = stream.getVideoTracks()[0];
    const { width = 0, height = 0 } = track.getSettings();
    this.el.textContent = `Resolution: ${width}x${height}`;
  }
}

class CameraApp {
  constructor() {
    this.video = document.querySelector("video");
    this.fpsCounter = new FpsCounter(this.video, 100);
    this.resoltuionStat = new ResolutionStat();
  }

  async start() {
    const stream = await navigator.mediaDevices.getUserMedia({
      video: {
        width: { ideal: 1280 },
        frameRate: { ideal: 30 },
      },
    });
    this.video.srcObject = stream;
    this.fpsCounter.start();
    this.resoltuionStat.update(stream);
  }
}

async function init() {
  const app = new CameraApp();
  await app.start();
}

init();
