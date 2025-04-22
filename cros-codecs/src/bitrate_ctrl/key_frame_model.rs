// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Debug)]
pub struct KeyFrameModel {
    bias: f64,
    coefficient: f64,
    ln_output: f64,
}

impl KeyFrameModel {
    pub fn new() -> KeyFrameModel {
        // Values chosen by encoding an H264 bitstream using CQP values of 20, 25, 30, 35, and 40
        // and using the resulting bitrate to fit a logarithmic curve.
        Self { bias: -1.76, coefficient: -0.34, ln_output: 0.0 }
    }

    pub fn forward(&mut self, input_vec: Vec<f64>) -> f64 {
        self.ln_output = input_vec.last().expect("Invalid input vec!").ln();
        (self.bias + self.coefficient * self.ln_output).clamp(-0.9, 1.0)
    }

    pub fn backward(&mut self, grad: Vec<f64>, learning_rate: f64) {
        self.bias += grad[0] * learning_rate;
        self.coefficient += grad[0] * self.ln_output * learning_rate;
    }
}
