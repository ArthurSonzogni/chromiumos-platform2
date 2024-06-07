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

    /** @type {number|null} */
    this.callbackId = null;
  }

  /**
   * @param {DOMHighResTimeStamp} now
   */
  onVideoFrame = (now) => {
    this.timestamps.push(now);
    if (this.timestamps.length >= this.len * 2) {
      this.timestamps = this.timestamps.slice(-this.len);
    }

    const count = Math.min(this.len, this.timestamps.length);
    const start = this.timestamps.at(-count);
    const fps = count === 0 ? 0 : ((count - 1) * 1000) / (now - start);
    this.el.textContent = `FPS: ${fps.toFixed(0)}`;

    this.callbackId = this.video.requestVideoFrameCallback(this.onVideoFrame);
  };

  start() {
    this.callbackId = this.video.requestVideoFrameCallback(this.onVideoFrame);
  }

  stop() {
    if (this.callbackId !== null) {
      this.video.cancelVideoFrameCallback(this.callbackId);
      this.callbackId = null;
    }

    this.timestamps = [];
    this.el.textContent = "FPS: ?";
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

  reset() {
    this.el.textContent = `Resolution: ?`;
  }
}

class PlayPauseButton {
  /**
   * @param {CameraApp} app
   */
  constructor(app) {
    /** @type {CameraApp} */
    this.app = app;

    /** @type {boolean} */
    this.isPlaying = false;

    /** @type {HTMLButtonElement} */
    this.el = document.getElementById("play-pause");

    this.el.addEventListener("click", this.onClick);
  }

  /**
   * @param {boolean} value
   */
  setPlaying(value) {
    this.isPlaying = value;
    this.el.textContent = value ? "Pause" : "Play";
  }

  onClick = async () => {
    this.el.disabled = true;
    if (this.isPlaying) {
      this.app.stop();
    } else {
      await this.app.start();
    }
    this.el.disabled = false;
  };
}

class CameraApp {
  constructor() {
    this.video = document.querySelector("video");
    this.fpsCounter = new FpsCounter(this.video, 100);
    this.resolutionStat = new ResolutionStat();
    this.playPauseButton = new PlayPauseButton(this);
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
    this.resolutionStat.update(stream);
    this.playPauseButton.setPlaying(true);
  }

  stop() {
    /** @type {MediaStream} stream */
    const stream = this.video.srcObject;
    if (stream === null) {
      return;
    }

    this.playPauseButton.setPlaying(false);
    this.resolutionStat.reset();
    this.fpsCounter.stop();
    for (const track of stream.getTracks()) {
      track.stop();
    }
    this.video.srcObject = null;
  }
}

async function init() {
  const app = new CameraApp();
  await app.start();
}

init();
