// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! perf_test is a binary that contains a number of performance tests for our image processing
//! functions.

use cros_codecs::image_processing::*;
#[cfg(feature = "v4l2")]
use std::ptr;
use std::time::Instant;

const K_NUMBER_OF_TEST_CYCLES: u64 = 2000;
const K_WIDTH: usize = 480;
const K_HEIGHT: usize = 288;
const K_VISIBLE_HEIGHT: usize = 270;
#[cfg(feature = "v4l2")]
const K_ADDR_ALIGN: usize = 16;

#[cfg(feature = "v4l2")]
fn test_mm21_to_nv12_perf() {
    let test_input = include_bytes!("../src/test_data/puppets-480x270_20230825.mm21.yuv");
    let test_expected_output = include_bytes!("../src/test_data/puppets-480x270_20230825.nv12.yuv");

    // Byte align test input.
    let mut aligned_input = vec![0u8; test_input.len() + K_ADDR_ALIGN];
    let mut index = K_ADDR_ALIGN - (ptr::addr_of!(aligned_input[0]) as usize % K_ADDR_ALIGN);
    let input_data = aligned_input.get_mut(index..(index + test_input.len())).unwrap();
    input_data.copy_from_slice(test_input);

    // Byte align test output.
    let mut aligned_output = vec![0u8; test_input.len() + K_ADDR_ALIGN];
    index = K_ADDR_ALIGN - (ptr::addr_of!(aligned_output[0]) as usize % K_ADDR_ALIGN);
    let output_data = aligned_output.get_mut(index..(index + test_input.len())).unwrap();
    let (test_y_output, test_uv_output) = output_data.split_at_mut(output_data.len() * 2 / 3);

    let start_time = Instant::now();
    for _cycle in 0..K_NUMBER_OF_TEST_CYCLES {
        let _ = mm21_to_nv12(
            &input_data[..(test_input.len() * 2 / 3)],
            K_WIDTH,
            test_y_output,
            K_WIDTH,
            &input_data[(test_input.len() * 2 / 3)..],
            K_WIDTH,
            test_uv_output,
            K_WIDTH,
            K_WIDTH,
            K_HEIGHT as isize,
        );
    }

    let duration_us = ((Instant::now() - start_time).as_micros()) as f64;

    let fps = K_NUMBER_OF_TEST_CYCLES as f64 / (duration_us / 1000000f64);

    println!("MM21 to NV12 Perf Test Results");
    println!("-----------------------------------");
    println!("Frames Decoded: {}", K_NUMBER_OF_TEST_CYCLES);
    println!("TotalDurationMs: {}", duration_us);
    println!("FramesPerSecond: {}\n\n", fps);

    assert_eq!(output_data, *test_expected_output);
}

fn test_nv12_to_i420_perf() {
    let test_input = include_bytes!("../src/test_data/puppets-480x270_20230825.nv12.yuv");
    let test_expected_output = include_bytes!("../src/test_data/puppets-480x270_20230825.i420.yuv");

    let mut test_output = vec![0u8; test_input.len()];
    let (test_y_output, test_uv_output) = test_output.split_at_mut(test_input.len() * 2 / 3);
    let (test_u_output, test_v_output) = test_uv_output.split_at_mut(test_uv_output.len() / 2);

    let start_time = Instant::now();
    for _cycle in 0..K_NUMBER_OF_TEST_CYCLES {
        nv12_to_i420(
            &test_input[..test_input.len() * 2 / 3],
            K_WIDTH,
            test_y_output,
            K_WIDTH,
            &test_input[test_input.len() * 2 / 3..],
            K_WIDTH,
            test_u_output,
            K_WIDTH / 2,
            test_v_output,
            K_WIDTH / 2,
            K_WIDTH,
            K_HEIGHT,
        );
    }

    let duration_us = ((Instant::now() - start_time).as_micros()) as f64;

    let fps = K_NUMBER_OF_TEST_CYCLES as f64 / (duration_us / 1000000f64);

    println!("NV12 to I420 Perf Test Results");
    println!("-----------------------------------");
    println!("Frames Decoded: {}", K_NUMBER_OF_TEST_CYCLES);
    println!("TotalDurationMs: {}", duration_us);
    println!("FramesPerSecond: {}\n\n", fps);

    assert_eq!(test_output, *test_expected_output);
}

fn test_i420_to_nv12_perf() {
    let test_input = include_bytes!("../src/test_data/puppets-480x270_20230825.i420.yuv");
    let test_expected_output = include_bytes!("../src/test_data/puppets-480x270_20230825.nv12.yuv");

    let mut test_output = vec![0u8; test_input.len()];
    let (test_y_output, test_uv_output) = test_output.split_at_mut(test_input.len() * 2 / 3);

    let start_time = Instant::now();
    for _cycle in 0..K_NUMBER_OF_TEST_CYCLES {
        i420_to_nv12(
            &test_input[..test_input.len() * 2 / 3],
            K_WIDTH,
            test_y_output,
            K_WIDTH,
            &test_input[(test_input.len() * 2 / 3)..(test_input.len() * 5 / 6)],
            K_WIDTH / 2,
            &test_input[(test_input.len() * 5 / 6)..],
            K_WIDTH / 2,
            test_uv_output,
            K_WIDTH,
            K_WIDTH,
            K_HEIGHT,
        );
    }

    let duration_us = ((Instant::now() - start_time).as_micros()) as f64;

    let fps = K_NUMBER_OF_TEST_CYCLES as f64 / (duration_us / 1000000f64);

    println!("I420 to NV12 Perf Test Results");
    println!("-----------------------------------");
    println!("Frames Decoded: {}", K_NUMBER_OF_TEST_CYCLES);
    println!("TotalDurationMs: {}", duration_us);
    println!("FramesPerSecond: {}\n\n", fps);

    assert_eq!(test_output, *test_expected_output);
}

fn test_argb_to_nv12_perf() {
    let test_input = include_bytes!("../src/test_data/puppets-480x270_20230825.argb");
    let test_expected_output = include_bytes!("../src/test_data/puppets-480x270_20230825.nv12.yuv");

    let mut test_output = vec![0u8; test_input.len() / 4 * 3 / 2];
    let (test_y_output, test_uv_output) = test_output.split_at_mut(test_input.len() / 4);

    let start_time = Instant::now();
    for _cycle in 0..K_NUMBER_OF_TEST_CYCLES {
        argb_to_nv12(
            test_input.as_slice(),
            K_WIDTH * 4,
            test_y_output,
            K_WIDTH,
            test_uv_output,
            K_WIDTH,
            K_WIDTH,
            K_HEIGHT,
        );
    }

    let duration_us = ((Instant::now() - start_time).as_micros()) as f64;

    let fps = K_NUMBER_OF_TEST_CYCLES as f64 / (duration_us / 1000000f64);

    println!("ARGB to NV12 Perf Test Results");
    println!("-----------------------------------");
    println!("Frames Decoded: {}", K_NUMBER_OF_TEST_CYCLES);
    println!("TotalDurationMs: {}", duration_us);
    println!("FramesPerSecond: {}\n\n", fps);

    // There's a tiny amount of error with our 8-bit fixed point conversion versus true floating
    // point conversion, so we have a tolerance in our assert.
    for y in 0..K_VISIBLE_HEIGHT {
        for x in 0..K_WIDTH {
            let i = y * K_WIDTH + x;
            let expected = test_expected_output[i] as i32;
            let actual = test_output[i] as i32;
            assert!(
                (actual - expected).abs() < 2,
                "idx = {} actual = {} expected = {}",
                i,
                actual,
                expected
            );
        }
    }
    for y in 0..(K_VISIBLE_HEIGHT / 2) {
        for x in 0..K_WIDTH {
            let i = y * K_WIDTH + x + K_WIDTH * K_HEIGHT;
            let expected = test_expected_output[i] as i32;
            let actual = test_output[i] as i32;
            assert!(
                (actual - expected).abs() < 2,
                "idx = {} actual = {} expected = {}",
                i,
                actual,
                expected
            );
        }
    }
}

fn main() {
    #[cfg(feature = "v4l2")]
    test_mm21_to_nv12_perf();

    test_nv12_to_i420_perf();
    test_i420_to_nv12_perf();
    test_argb_to_nv12_perf();
}
