// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Debug)]
pub struct LeakyBucket {
    fullness: u64,
    bit_capacity: u64,
    frame_capacity: u64,
    target_bits_per_frame: u64,
    min_frame_size: u64,
}

impl LeakyBucket {
    pub fn new(
        frame_capacity: u64,
        target_bitrate: u64,
        target_framerate: u32,
        min_frame_size: u64,
    ) -> LeakyBucket {
        let target_bits_per_frame = target_bitrate / (target_framerate as u64);
        let bit_capacity = target_bits_per_frame * frame_capacity * 2;
        LeakyBucket {
            fullness: bit_capacity / 2,
            bit_capacity: bit_capacity,
            frame_capacity: frame_capacity,
            target_bits_per_frame: target_bits_per_frame,
            min_frame_size: min_frame_size,
        }
    }

    pub fn get_frame_budget(&self) -> u64 {
        (self.bit_capacity - self.fullness) / self.frame_capacity
    }

    pub fn process_frame(&mut self, frame_len: u64) {
        self.fullness += frame_len as u64 * 8;
        if self.fullness > self.target_bits_per_frame {
            self.fullness -= self.target_bits_per_frame;
        } else {
            self.fullness = 0;
        }
        self.fullness =
            self.fullness.min(self.bit_capacity - (self.min_frame_size * self.frame_capacity));
    }
}
