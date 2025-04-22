// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::VecDeque;
use std::fs::File;
use std::io::Read;
use std::path::Path;
use std::str::FromStr;

use crate::bitrate_ctrl::leaky_bucket::LeakyBucket;
use crate::bitrate_ctrl::neural_network::NeuralNetwork;
use crate::Resolution;

pub mod leaky_bucket;
pub mod neural_network;

#[derive(Debug)]
struct BitrateControlHistory {
    qp_model_output: f64,
    target_size: f64,
    actual_size: f64,
}

pub struct BitrateController {
    qp_model: NeuralNetwork,
    leaky_bucket: LeakyBucket,
    resolution: Resolution,
    history: VecDeque<BitrateControlHistory>,
    in_flight: VecDeque<(BitrateControlHistory, Vec<f64>)>,
}

// TODO: Tweak these numbers
const HISTORY_SIZE: usize = 5;
const HIDDEN_LAYER_SIZE: usize = 5;
const LEARNING_RATE: f64 = 0.01;
const FRAME_CAPACITY: u64 = 60;
const MIN_FRAME_BITS_PER_PIXEL: f64 = 0.01;

impl BitrateController {
    pub fn new(
        resolution: Resolution,
        target_bitrate: u64,
        target_framerate: u32,
    ) -> BitrateController {
        let mut initial_history: VecDeque<BitrateControlHistory> = VecDeque::new();
        for _i in 0..HISTORY_SIZE {
            let target_size = (target_bitrate as f64) / (resolution.get_area() as f64);
            initial_history.push_back(BitrateControlHistory {
                qp_model_output: 0.0,
                target_size: target_size,
                actual_size: target_size,
            });
        }

        // Parameters pretrained using the platform encoding 360P test video (tulip3-640x360).
        let mut qp_model = NeuralNetwork::new(
            vec![(2 * HISTORY_SIZE + 1), HIDDEN_LAYER_SIZE, 1],
            Some(VecDeque::from([
                0.425684582786736,
                -0.36667331662066827,
                0.5019712699213323,
                0.21644038484698708,
                -0.06458135817009378,
                0.6064852268753187,
                0.8543065183073643,
                -0.915291079032271,
                -0.4333725522265774,
                0.3645490430250808,
                -0.15560624111490803,
                -0.3199786274408347,
                -0.45440880169768894,
                -0.06738883753940754,
                0.6296130281937793,
                -0.9286643185904727,
                -0.39776583932869053,
                0.627800319486201,
                0.27693707375579935,
                0.6651590049383689,
                -0.9724136534017053,
                -0.8087770659011951,
                -0.7546455244473126,
                0.8410107721482897,
                -0.04200974475081515,
                0.5387322470240627,
                0.8646503050364456,
                0.2779357707703823,
                -0.8508681604937401,
                -0.4260598041376381,
                -0.4485563644990533,
                -1.0322817945053895,
                0.6039304611682356,
                0.30542575498584035,
                0.443675866468031,
                -0.6401103757098691,
                0.3137309735338676,
                -0.9533555381658919,
                -0.5700425370686214,
                0.8794601231207578,
                0.40941717933351035,
                -0.3984068118311618,
                -0.45922122293065704,
                0.09243349757184359,
                0.17319666053546287,
                0.7892502749682055,
                0.3096278696075423,
                0.6869701787008016,
                -0.9075346566405105,
                -0.4692641952710672,
                0.1660173908739917,
                0.6121365411346972,
                -0.5446873146768768,
                -0.1525327192193162,
                -0.08757761095478364,
                0.9844879411470974,
                0.7591588811118342,
                -0.26878326837585836,
                -0.07220115557041284,
                -0.5836283812415518,
                -0.1992275737626039,
                -0.1504212118205487,
                -0.30921748462999293,
                0.11440050628361297,
                0.47909397434872153,
                0.722482877168,
            ])),
        );

        let leaky_bucket = LeakyBucket::new(
            FRAME_CAPACITY,
            target_bitrate,
            target_framerate,
            (MIN_FRAME_BITS_PER_PIXEL * (resolution.get_area() as f64)) as u64,
        );

        BitrateController {
            qp_model: qp_model,
            leaky_bucket: leaky_bucket,
            resolution: resolution,
            history: initial_history,
            in_flight: VecDeque::new(),
        }
    }

    pub fn tune(&mut self, target_bitrate: u64, target_framerate: u32) {
        self.leaky_bucket = LeakyBucket::new(
            FRAME_CAPACITY,
            target_bitrate,
            target_framerate,
            (MIN_FRAME_BITS_PER_PIXEL * (self.resolution.get_area() as f64)) as u64,
        );
    }

    pub fn get_qp(&mut self, min_qp: u32, max_qp: u32) -> u32 {
        let mut target_size = self.leaky_bucket.get_frame_budget() as f64;

        // The idea here is that having historical QP values and the actual frame sizes they
        // correspond to will give us a sense of the entropy of the scene.
        let mut input_vec: Vec<f64> = vec![];
        for history_entry in self.history.iter() {
            input_vec.push(history_entry.qp_model_output);
            input_vec.push(history_entry.actual_size);
        }

        target_size /= self.resolution.get_area() as f64;
        input_vec.push(target_size);

        let qp = self.qp_model.forward(input_vec.clone())[0];

        self.in_flight.push_back((
            BitrateControlHistory {
                qp_model_output: qp,
                target_size: target_size,
                actual_size: 0.0,
            },
            input_vec,
        ));

        // QP output from model is -1.0 to 1.0, so interpolate to the correct QP range
        (((qp + 1.0) / 2.0) * ((max_qp - min_qp) as f64)) as u32 + min_qp
    }

    pub fn process_frame(&mut self, frame_len: u64) {
        // Update leaky bucket
        self.leaky_bucket.process_frame(frame_len);

        let (mut history_entry, input_vec) = self.in_flight.pop_front().unwrap();
        let actual_size = (frame_len as f64) * 8.0 / (self.resolution.get_area() as f64);
        history_entry.actual_size = actual_size;

        // Learn QP model.
        let grad_loss = ((actual_size / history_entry.target_size) - 1.0).clamp(-1.0, 1.0);
        self.qp_model.forward(input_vec);
        self.qp_model.backward(vec![grad_loss], LEARNING_RATE);

        // Manage history
        let _ = self.history.pop_front();
        self.history.push_back(history_entry);
    }
}
