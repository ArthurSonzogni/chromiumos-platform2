// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;
use std::collections::VecDeque;
use std::rc::Rc;

use rand::Rng;

// See go/universal-bitrate-control for architecture details.

// TODO: Tensorflow is definitely overkill for a network this size, but we could leverage a linear
// algebra crate if we have one available.

#[derive(Debug)]
pub struct NeuralNetwork {
    layer_sizes: Vec<usize>,
    weights: Vec<Vec<Vec<f64>>>,
    biases: Vec<Vec<f64>>,
    linear_outputs: Vec<Vec<f64>>,
    outputs: Vec<Vec<f64>>,
}

impl NeuralNetwork {
    pub fn new(layer_sizes: Vec<usize>, mut init_params: Option<VecDeque<f64>>) -> NeuralNetwork {
        let mut rng = rand::thread_rng();

        let mut all_biases: Vec<Vec<f64>> = vec![];
        let mut all_weights: Vec<Vec<Vec<f64>>> = vec![];
        let mut all_linear_outputs: Vec<Vec<f64>> = vec![];
        let mut all_outputs: Vec<Vec<f64>> = vec![];
        let mut layer_sizes_iter = layer_sizes.iter();
        let mut prev_layer_size = *layer_sizes_iter.next().unwrap();
        let mut outputs: Vec<f64> = vec![];
        outputs.resize(prev_layer_size, 0.0);
        all_outputs.push(outputs);
        for layer_size in layer_sizes_iter {
            let mut weights: Vec<Vec<f64>> = vec![];
            for _i in 0..*layer_size {
                let mut weight_row: Vec<f64> = vec![];
                for _j in 0..prev_layer_size {
                    if let Some(init_params) = init_params.as_mut() {
                        weight_row.push(init_params.pop_front().expect("Length of initialization parameters does not match neural architecture!"));
                    } else {
                        weight_row.push(2.0 * rng.gen::<f64>() - 1.0);
                    }
                }
                weights.push(weight_row);
            }
            all_weights.push(weights);

            let mut biases: Vec<f64> = vec![];
            for _i in 0..*layer_size {
                if let Some(init_params) = init_params.as_mut() {
                    biases.push(init_params.pop_front().expect(
                        "Length of initialization parameters does not match neural architecture!",
                    ));
                } else {
                    biases.push(2.0 * rng.gen::<f64>() - 1.0);
                }
            }
            all_biases.push(biases);

            let mut outputs: Vec<f64> = vec![];
            outputs.resize(*layer_size, 0.0);
            all_outputs.push(outputs.clone());
            all_linear_outputs.push(outputs);

            prev_layer_size = *layer_size;
        }

        NeuralNetwork {
            layer_sizes: layer_sizes,
            weights: all_weights,
            biases: all_biases,
            linear_outputs: all_linear_outputs,
            outputs: all_outputs,
        }
    }

    pub fn forward(&mut self, mut input_vec: Vec<f64>) -> Vec<f64> {
        self.outputs[0].as_mut_slice().copy_from_slice(input_vec.as_slice());
        for layer in 0..(self.layer_sizes.len() - 1) {
            for output_idx in 0..self.outputs[layer + 1].len() {
                self.linear_outputs[layer][output_idx] = 0.0;
                for input_idx in 0..input_vec.len() {
                    self.linear_outputs[layer][output_idx] +=
                        input_vec[input_idx] * self.weights[layer][output_idx][input_idx];
                }
                self.linear_outputs[layer][output_idx] += self.biases[layer][output_idx];

                // Hyperbolic tangent activation function
                self.outputs[layer + 1][output_idx] = self.linear_outputs[layer][output_idx].tanh();
            }
            input_vec = self.outputs[layer + 1].clone();
        }

        input_vec
    }

    pub fn backward(&mut self, mut output_grad: Vec<f64>, learning_rate: f64) {
        for layer in (0..self.biases.len()).rev() {
            let mut bias_grad: Vec<f64> = vec![];
            bias_grad.resize(output_grad.len(), 0.0);
            let mut input_grad: Vec<f64> = vec![];
            input_grad.resize(self.weights[layer][0].len(), 0.0);

            for output_idx in 0..bias_grad.len() {
                // Derivative of hyperbolic tangent is hyperbolic secant squared
                bias_grad[output_idx] = 1.0 / self.linear_outputs[layer][output_idx].cosh().powi(2)
                    * output_grad[output_idx];
                self.biases[layer][output_idx] += bias_grad[output_idx] * learning_rate;

                for input_idx in 0..input_grad.len() {
                    input_grad[input_idx] +=
                        bias_grad[output_idx] * self.weights[layer][output_idx][input_idx];
                    self.weights[layer][output_idx][input_idx] +=
                        bias_grad[output_idx] * self.outputs[layer][input_idx] * learning_rate;
                }
            }

            output_grad = input_grad;
        }
    }
}
