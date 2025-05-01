// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cmp::min;

use crate::utils::align_down;
use crate::utils::align_up;
use crate::video_frame::{VideoFrame, ARGB_PLANE, UV_PLANE, U_PLANE, V_PLANE, Y_PLANE};
use crate::DecodedFormat;

/// TODO(greenjustin): This entire file should be replaced with LibYUV.
use byteorder::ByteOrder;
use byteorder::LittleEndian;

#[cfg(feature = "v4l2")]
use std::arch::aarch64::*;

#[cfg(all(target_arch = "x86_64", target_feature = "avx2"))]
use std::arch::x86_64::*;

pub const MM21_TILE_WIDTH: usize = 16;
pub const MM21_TILE_HEIGHT: usize = 32;

// Constants taken from LibYUV.
// 8.8 fixed point conversions between ARGB and YUV
const K_ARGB_TO_Y: [u8; 32] = [
    25, 129, 66, 0, 25, 129, 66, 0, 25, 129, 66, 0, 25, 129, 66, 0, 25, 129, 66, 0, 25, 129, 66, 0,
    25, 129, 66, 0, 25, 129, 66, 0,
];
const K_ARGB_TO_UV: [i8; 32] = [
    -112, 74, 38, 0, 18, 94, -112, 0, -112, 74, 38, 0, 18, 94, -112, 0, -112, 74, 38, 0, 18, 94,
    -112, 0, -112, 74, 38, 0, 18, 94, -112, 0,
];

/// Converts an ARGB row into an NV12 Y row. This algorithm was ported directly from LibYUV.
// Safety: The caller, argb_to_nv12_y_row, must ensure that src_argb.len() == dst_y.len() * 4.
// Note that _mm256_loadu_si256 has no alignment requirements, so the caller does not need to make
// guarantees about pointer alignment.
#[cfg(all(target_arch = "x86_64", target_feature = "avx2"))]
unsafe fn argb_to_nv12_y_row_simd(src_argb: &[u8], dst_y: &mut [u8]) -> usize {
    const kSimdWidth: usize = 32;
    // maddubs takes one unsigned and one signed vector, and unfortunately one of our coefficients
    // is 129, so those have to go into the unsigned vector. This means that our input data needs
    // to be put into the signed vector. To prevent overflow, we center all of our input pixels
    // around 0 by subtracting 128. Then, when we would normally add 16 << 8 at the end of the dot
    // product, we instead add 126.5 << 8 to correct the range. Note the dot product calculation
    // should give us results ranging from -109 << 8 to 110 << 8 for studio swing video, and we
    // want to output a range between 16 and 235. The extra 0.5 is for rounding, since the final
    // bitshift from 8.8 fixed point to regular 8-bit video would otherwise simply truncate our
    // signal.
    //
    // TODO: We'll probably have to fix this if we need to support full swing ARGB video?
    const kSub128: [i8; 32] = [128; 32];
    const kAddY16: [u16; 16] = [0x7e80; 16];
    const kUnpermute: [u32; 8] = [0, 4, 1, 5, 2, 6, 3, 7];

    debug_assert!(dst_y.len() * 4 == src_argb.len());
    let width = align_down(dst_y.len(), kSimdWidth);
    let src_argb = src_argb.as_ptr();
    let dst_y = dst_y.as_mut_ptr();

    unsafe {
        // SAFETY: K_ARGB_TO_Y, must be an array with 32 u8 elements. kSub128 must be an array with
        // 32 i8 elements. kAddY16 must be an array with 16 u16 elements. kUnpermute must be an
        // array with 8 u32 elements.
        debug_assert!(K_ARGB_TO_Y.len() == 32);
        debug_assert!(kAddY16.len() == 16);
        debug_assert!(kSub128.len() == 32);
        debug_assert!(kUnpermute.len() == 8);
        let argb_to_y = _mm256_loadu_si256(K_ARGB_TO_Y.as_ptr() as *const __m256i);
        let sub128 = _mm256_loadu_si256(kSub128.as_ptr() as *const __m256i);
        let offset = _mm256_loadu_si256(kAddY16.as_ptr() as *const __m256i);
        let unpermute = _mm256_loadu_si256(kUnpermute.as_ptr() as *const __m256i);
        for i in (0..(width as isize)).step_by(32) {
            // Load input pixels.
            // SAFETY: src_argb must be a valid pointer, and i * 4 through i * 4 + 128 must be
            // valid indices into the underlying slice.
            let mut input1 = _mm256_loadu_si256(src_argb.offset(i * 4) as *const __m256i);
            let mut input2 = _mm256_loadu_si256(src_argb.offset(i * 4 + 32) as *const __m256i);
            let mut input3 = _mm256_loadu_si256(src_argb.offset(i * 4 + 64) as *const __m256i);
            let mut input4 = _mm256_loadu_si256(src_argb.offset(i * 4 + 96) as *const __m256i);

            // Center data around 0.
            // SAFETY: These should actually always be safe if the CPU supports AVX2.
            input1 = _mm256_sub_epi8(input1, sub128);
            input2 = _mm256_sub_epi8(input2, sub128);
            input3 = _mm256_sub_epi8(input3, sub128);
            input4 = _mm256_sub_epi8(input4, sub128);

            // Compute dot products.
            // SAFETY: These should actually always be safe if the CPU supports AVX2.
            let mut output1 = _mm256_hadd_epi16(
                _mm256_maddubs_epi16(argb_to_y, input1),
                _mm256_maddubs_epi16(argb_to_y, input2),
            );
            let mut output2 = _mm256_hadd_epi16(
                _mm256_maddubs_epi16(argb_to_y, input3),
                _mm256_maddubs_epi16(argb_to_y, input4),
            );

            // Add offset and then bitshift back down to 8-bit.
            // SAFETY: These should actually always be safe if the CPU supports AVX2.
            output1 = _mm256_srli_epi16(_mm256_add_epi16(output1, offset), 8);
            output2 = _mm256_srli_epi16(_mm256_add_epi16(output2, offset), 8);

            // Re-pack everything into one vector.
            // SAFETY: These should actually always be safe if the CPU supports AVX2.
            let output =
                _mm256_permutevar8x32_epi32(_mm256_packus_epi16(output1, output2), unpermute);

            // Store output data.
            // SAFETY: dst_y must be a valid pointer, and i through i + 32 must be valid offsets
            // into the underlying slice.
            _mm256_storeu_si256(dst_y.offset(i) as *mut __m256i, output);
        }
    }

    width
}

// Safety: The caller, argb_to_nv12_uv_row, must ensure that src_argb.len() == dst_uv.len() * 4.
#[cfg(all(target_arch = "aarch64", target_feature = "neon"))]
unsafe fn argb_to_nv12_y_row_simd(src_argb: &[u8], dst_y: &mut [u8]) -> usize {
    const kSimdWidth: usize = 16;
    const kZero: [u16; 8] = [0; 8];
    const kAddY16: [u16; 8] = [0x1080; 8];

    debug_assert!(dst_y.len() * 4 == src_argb.len());
    let width = align_down(dst_y.len(), kSimdWidth);
    let src_argb = src_argb.as_ptr();
    let dst_y = dst_y.as_mut_ptr();

    unsafe {
        // SAFETY: kZero must be an array of 8 u16 elements. K_ARGB_TO_Y must be an array with at
        // least 8 u8 elements. kAddY16 must be an array of 8 u16 elements.
        debug_assert!(K_ARGB_TO_Y.len() >= 8);
        debug_assert!(kAddY16.len() == 8);
        debug_assert!(kZero.len() == 8);
        let zero = vld1q_u16(kZero.as_ptr());
        let argb_to_y = vld1_u8(K_ARGB_TO_Y.as_ptr());
        let offset = vld1q_u16(kAddY16.as_ptr());
        for i in (0..(width as isize)).step_by(16) {
            // SAFETY: src_argb must be a valid pointer, and i * 4 through i * 4 + 64 must be
            // valid indices into the underlying slice.
            let input1 = vld1_u8_x4(src_argb.offset(i * 4));
            let input2 = vld1_u8_x4(src_argb.offset(i * 4 + 32));

            // Compute the dot products.
            // SAFETY: These should actually always be safe if the CPU supports neon.
            let output1 = vpaddq_u16(
                vpaddq_u16(
                    vmlal_u8(zero, input1.0, argb_to_y),
                    vmlal_u8(zero, input1.1, argb_to_y),
                ),
                vpaddq_u16(
                    vmlal_u8(zero, input1.2, argb_to_y),
                    vmlal_u8(zero, input1.3, argb_to_y),
                ),
            );
            let output2 = vpaddq_u16(
                vpaddq_u16(
                    vmlal_u8(zero, input2.0, argb_to_y),
                    vmlal_u8(zero, input2.1, argb_to_y),
                ),
                vpaddq_u16(
                    vmlal_u8(zero, input2.2, argb_to_y),
                    vmlal_u8(zero, input2.3, argb_to_y),
                ),
            );

            // Add the offset and shift back down to 8 bit.
            // SAFETY: These should actually always be safe if the CPU supports neon.
            let output = vcombine_u8(
                vqshrn_n_u16(vaddq_u16(output1, offset), 8),
                vqshrn_n_u16(vaddq_u16(output2, offset), 8),
            );

            // Store output data.
            // SAFETY: dst_y must be a valid pointer, and i through i + 16 must be valid offsets
            // into the underlying slice.
            vst1q_u8(dst_y.offset(i), output);
        }
    }

    width
}

// Safety: The caller must ensure that src_argb.len() == dst_y.len() * 4.
unsafe fn argb_to_nv12_y_row(mut src_argb: &[u8], mut dst_y: &mut [u8]) {
    debug_assert!(src_argb.len() == dst_y.len() * 4);

    #[cfg(any(
        all(target_arch = "x86_64", target_feature = "avx2"),
        all(target_arch = "aarch64", target_feature = "neon")
    ))]
    {
        // SAFETY: The caller, argb_to_nv12, validates the stride and width parameters.
        let aligned_width = unsafe { argb_to_nv12_y_row_simd(src_argb, dst_y) };
        src_argb = &src_argb[(aligned_width * 4)..];
        dst_y = &mut dst_y[aligned_width..];
    }

    for i in 0..(dst_y.len()) {
        let b = src_argb[i * 4] as u16;
        let g = src_argb[i * 4 + 1] as u16;
        let r = src_argb[i * 4 + 2] as u16;
        let a = src_argb[i * 4 + 3] as u16;
        dst_y[i] = (((K_ARGB_TO_Y[0] as u16 * b
            + K_ARGB_TO_Y[1] as u16 * g
            + K_ARGB_TO_Y[2] as u16 * r
            + K_ARGB_TO_Y[3] as u16 * a)
            >> 8)
            + 16) as u8;
    }
}

/// Converts an ARGB row into an NV12 UV row.
// Safety: The caller, argb_to_nv12_y_row, must ensure that src_argb.len() == dst_y.len() * 4.
// Note that _mm256_loadu_si256 has no alignment requirements, so the caller does not need to make
// guarantees about pointer alignment.
#[cfg(all(target_arch = "x86_64", target_feature = "avx2"))]
unsafe fn argb_to_nv12_uv_row_simd(src_argb: &[u8], dst_uv: &mut [u8]) -> usize {
    let kSimdWidth: usize = 32;

    const kGatherIndices1: [u32; 8] = [0, 0, 8, 8, 64, 64, 72, 72];
    const kGatherIndices2: [u32; 8] = [16, 16, 24, 24, 80, 80, 88, 88];
    const kGatherIndices3: [u32; 8] = [32, 32, 40, 40, 96, 96, 104, 104];
    const kGatherIndices4: [u32; 8] = [48, 48, 56, 56, 112, 112, 120, 120];
    const kAdd128: [i16; 16] = [0x8080; 16];

    debug_assert!(dst_uv.len() * 4 == src_argb.len());
    let width = align_down(dst_uv.len(), kSimdWidth);
    let src_argb = src_argb.as_ptr();
    let dst_uv = dst_uv.as_mut_ptr();

    unsafe {
        // SAFETY: K_ARGB_TO_UV must be an array of 32 i8 elements. kGatherIndices* must be arrays
        // of 8 u32 elements. kAdd128 must be an array of 16 i16 elements.
        debug_assert!(K_ARGB_TO_UV.len() == 32);
        debug_assert!(kAdd128.len() == 16);
        debug_assert!(kGatherIndices1.len() == 8);
        debug_assert!(kGatherIndices2.len() == 8);
        debug_assert!(kGatherIndices3.len() == 8);
        debug_assert!(kGatherIndices4.len() == 8);
        let argb_to_uv = _mm256_loadu_si256(K_ARGB_TO_UV.as_ptr() as *const __m256i);
        let gather1 = _mm256_loadu_si256(kGatherIndices1.as_ptr() as *const __m256i);
        let gather2 = _mm256_loadu_si256(kGatherIndices2.as_ptr() as *const __m256i);
        let gather3 = _mm256_loadu_si256(kGatherIndices3.as_ptr() as *const __m256i);
        let gather4 = _mm256_loadu_si256(kGatherIndices4.as_ptr() as *const __m256i);
        let add128 = _mm256_loadu_si256(kAdd128.as_ptr() as *const __m256i);
        for i in (0..(width as isize)).step_by(32) {
            // Load input pixels. Rust doesn't have load with stride intrinsics, so we have to use
            // "gather" in order to subsample our ARGB plane. Since we're already using gather, we
            // can manipulate the indices in such a way that the downstream swizzling instructions
            // like hadd and pack actually result in the output vector bytes being in the correct
            // order.
            // SAFETY: src_argb must be a valid pointer, and i * 4 through i * 4 + 124 must be
            // valid indices into the underlying slice. No index in the gather* indices must exceed
            // 120.
            let input1 = _mm256_i32gather_epi32(src_argb.offset(i * 4) as *const i32, gather1, 1);
            let input2 = _mm256_i32gather_epi32(src_argb.offset(i * 4) as *const i32, gather2, 1);
            let input3 = _mm256_i32gather_epi32(src_argb.offset(i * 4) as *const i32, gather3, 1);
            let input4 = _mm256_i32gather_epi32(src_argb.offset(i * 4) as *const i32, gather4, 1);

            // Compute dot products.
            // SAFETY: These should actually always be safe if the CPU supports AVX2.
            let mut output1 = _mm256_hadd_epi16(
                _mm256_maddubs_epi16(input1, argb_to_uv),
                _mm256_maddubs_epi16(input2, argb_to_uv),
            );
            let mut output2 = _mm256_hadd_epi16(
                _mm256_maddubs_epi16(input3, argb_to_uv),
                _mm256_maddubs_epi16(input4, argb_to_uv),
            );

            // Add offset and then bitshift back down to 8-bit.
            // SAFETY: These should actually always be safe if the CPU supports AVX2.
            output1 = _mm256_srli_epi16(_mm256_sub_epi16(add128, output1), 8);
            output2 = _mm256_srli_epi16(_mm256_sub_epi16(add128, output2), 8);

            // Re-pack everything into one vector.
            // SAFETY: This should actually always be safe if the CPU supports AVX2.
            let output = _mm256_packus_epi16(output1, output2);

            // Store output data.
            // SAFETY: dst_uv must be a valid pointer, and i through i + 32 must be valid offsets
            // into the underlying slice.
            _mm256_storeu_si256(dst_uv.offset(i) as *mut __m256i, output);
        }
    }

    width
}

// Safety: The caller, argb_to_nv12_uv_row, must ensure that src_argb.len() == dst_y.len() * 4.
#[cfg(all(target_arch = "aarch64", target_feature = "neon"))]
unsafe fn argb_to_nv12_uv_row_simd(src_argb: &[u8], dst_uv: &mut [u8]) -> usize {
    let kSimdWidth: usize = 16;

    const kZero: [u16; 8] = [0; 8];
    const kAdd128: [i16; 8] = [0x8080; 8];

    debug_assert!(dst_uv.len() * 4 == src_argb.len());
    let width = align_down(dst_uv.len(), kSimdWidth);
    let src_argb = src_argb.as_ptr();
    let dst_uv = dst_uv.as_mut_ptr();

    unsafe {
        // SAFETY: kZero must be an array of 8 u16 elements. K_ARGB_TO_UV must be an array with
        // at least 8 u8 elements. kAdd128 must be an array of 8 i16 elements.
        debug_assert!(K_ARGB_TO_UV.len() >= 8);
        debug_assert!(kAdd128.len() == 8);
        debug_assert!(kZero.len() == 8);
        let zero = vld1q_u16(kZero.as_ptr());
        let argb_to_u =
            vmovl_s8(vreinterpret_s8_u32(vld1_dup_u32(K_ARGB_TO_UV.as_ptr() as *const u32)));
        let argb_to_v = vmovl_s8(vreinterpret_s8_u32(vld1_dup_u32(
            K_ARGB_TO_UV.as_ptr().offset(4) as *const u32,
        )));
        let offset = vld1q_s16(kAdd128.as_ptr());
        for i in (0..(width as isize)).step_by(16) {
            // SAFETY: src_argb must be a valid pointer, and i * 4 through i * 4 + 64 must be valid
            // indices into the underlying slice.
            let input1 = vreinterpretq_s16_u16(vmovl_u8(vreinterpret_u8_u32(
                vld2_u32(src_argb.offset(i * 4) as *const u32).0,
            )));
            let input2 = vreinterpretq_s16_u16(vmovl_u8(vreinterpret_u8_u32(
                vld2_u32(src_argb.offset(i * 4 + 16) as *const u32).0,
            )));
            let input3 = vreinterpretq_s16_u16(vmovl_u8(vreinterpret_u8_u32(
                vld2_u32(src_argb.offset(i * 4 + 32) as *const u32).0,
            )));
            let input4 = vreinterpretq_s16_u16(vmovl_u8(vreinterpret_u8_u32(
                vld2_u32(src_argb.offset(i * 4 + 48) as *const u32).0,
            )));

            // Compute dot products.
            // SAFETY: These should actually always be safe if the CPU supports neon.
            let output_u = vpaddq_s16(
                vpaddq_s16(vmulq_s16(input1, argb_to_u), vmulq_s16(input2, argb_to_u)),
                vpaddq_s16(vmulq_s16(input3, argb_to_u), vmulq_s16(input4, argb_to_u)),
            );
            let output_v = vpaddq_s16(
                vpaddq_s16(vmulq_s16(input1, argb_to_v), vmulq_s16(input2, argb_to_v)),
                vpaddq_s16(vmulq_s16(input3, argb_to_v), vmulq_s16(input4, argb_to_v)),
            );

            // Add the offset and shift back down to 8 bit.
            // SAFETY: These should actually always be safe if the CPU supports neon.
            let output_u = vqshrn_n_u16(vreinterpretq_u16_s16(vsubq_s16(offset, output_u)), 8);
            let output_v = vqshrn_n_u16(vreinterpretq_u16_s16(vsubq_s16(offset, output_v)), 8);

            // Store output data.
            // SAFETY: dst_y must be a valid pointer, and i through i + 16 must be valid offsets
            // into the underlying slice.
            vst2_u8(dst_uv.offset(i), uint8x8x2_t(output_u, output_v));
        }
    }

    width
}

// Safety: The caller must ensure that src_argb.len() == dst_uv.len() * 4. Note that even though UV
// elements are interleaved, the horizontal subsampling means the UV and Y planes have the same
// stride.
unsafe fn argb_to_nv12_uv_row(mut src_argb: &[u8], mut dst_uv: &mut [u8]) {
    debug_assert!(src_argb.len() == dst_uv.len() * 4);

    #[cfg(any(
        all(target_arch = "x86_64", target_feature = "avx2"),
        all(target_arch = "aarch64", target_feature = "neon")
    ))]
    {
        // SAFETY: The caller, argb_to_nv12, validates the stride and width parameters.
        let aligned_width = unsafe { argb_to_nv12_uv_row_simd(src_argb, dst_uv) };
        src_argb = &src_argb[(aligned_width * 4)..];
        dst_uv = &mut dst_uv[aligned_width..];
    }

    for i in (0..dst_uv.len()).step_by(2) {
        let b = src_argb[i * 4] as i16;
        let g = src_argb[i * 4 + 1] as i16;
        let r = src_argb[i * 4 + 2] as i16;
        let a = src_argb[i * 4 + 3] as i16;
        dst_uv[i] = (-1
            * ((K_ARGB_TO_UV[0] as i16 * b
                + K_ARGB_TO_UV[1] as i16 * g
                + K_ARGB_TO_UV[2] as i16 * r
                + K_ARGB_TO_UV[3] as i16 * a)
                >> 8)
            + 128) as u8;
        dst_uv[i + 1] = (-1
            * ((K_ARGB_TO_UV[4] as i16 * b
                + K_ARGB_TO_UV[5] as i16 * g
                + K_ARGB_TO_UV[6] as i16 * r
                + K_ARGB_TO_UV[7] as i16 * a)
                >> 8)
            + 128) as u8;
    }
}

/// Convert an ARGB frame into an NV12 frame.
pub fn argb_to_nv12(
    src_argb: &[u8],
    src_stride: usize,
    dst_y: &mut [u8],
    dst_y_stride: usize,
    dst_uv: &mut [u8],
    dst_uv_stride: usize,
    width: usize,
    height: usize,
) {
    assert!(width <= 4 * src_stride);
    assert!(width <= dst_y_stride);
    assert!(width <= dst_uv_stride);
    assert!(height * src_stride <= src_argb.len());
    assert!(height * dst_y_stride <= dst_y.len());
    assert!((align_up(height, 2) / 2) * dst_uv_stride <= dst_uv.len());

    for y in 0..(height / 2) {
        // SAFETY: These range calculations guarantee that the slices we pass to argb_to_nv12_y_row
        // and argb_to_nv12_uv_row match the invariant requirements documented above those
        // functions. Specifically, we need to make sure the length of the src_argb slice is
        // exactly equal to 4 * width and that the length of the dst_y and dst_uv slices is equal
        // to width.
        unsafe {
            argb_to_nv12_y_row(
                &src_argb[(y * 2 * src_stride)..(y * 2 * src_stride + width * 4)],
                &mut dst_y[(y * 2 * dst_y_stride)..(y * 2 * dst_y_stride + width)],
            );
            argb_to_nv12_y_row(
                &src_argb[((y * 2 + 1) * src_stride)..((y * 2 + 1) * src_stride + width * 4)],
                &mut dst_y[((y * 2 + 1) * dst_y_stride)..((y * 2 + 1) * dst_y_stride + width)],
            );
            argb_to_nv12_uv_row(
                &src_argb[(y * 2 * src_stride)..(y * 2 * src_stride + width * 4)],
                &mut dst_uv[(y * dst_uv_stride)..(y * dst_uv_stride + width)],
            );
        }
    }

    if height % 2 == 1 {
        let y = height - 1;
        // SAFETY: These range calculations guarantee that the slices we pass to argb_to_nv12_y_row
        // and argb_to_nv12_uv_row match the invariant requirements documented above those
        // functions. Specifically, we need to make sure that the length of the src_argb slice is
        // exactly equal to 4 * width, and that the length of the dst_y and the dst_uv slices is equal to
        // width.
        unsafe {
            argb_to_nv12_y_row(
                &src_argb[(y * src_stride)..(y * src_stride + width * 4)],
                &mut dst_y[(y * dst_y_stride)..(y * dst_y_stride + width)],
            );
            argb_to_nv12_uv_row(
                &src_argb[(y * src_stride)..(y * src_stride + width * 4)],
                &mut dst_uv[((y + 1) / 2 * dst_uv_stride)..((y + 1) / 2 * dst_uv_stride + width)],
            );
        }
    }
}

/// Copies `src` into `dst` as NV12, handling padding.
pub fn nv12_copy(
    src_y: &[u8],
    src_y_stride: usize,
    dst_y: &mut [u8],
    dst_y_stride: usize,
    src_uv: &[u8],
    src_uv_stride: usize,
    dst_uv: &mut [u8],
    dst_uv_stride: usize,
    width: usize,
    height: usize,
) {
    for y in 0..height {
        dst_y[(y * dst_y_stride)..(y * dst_y_stride + width)]
            .copy_from_slice(&src_y[(y * src_y_stride)..(y * src_y_stride + width)]);
    }
    for y in 0..(height / 2) {
        dst_uv[(y * dst_uv_stride)..(y * dst_uv_stride + width)]
            .copy_from_slice(&src_uv[(y * src_uv_stride)..(y * src_uv_stride + width)]);
    }
}

/// Replace 0 padding with the last pixels of the real image. This helps reduce compression
/// artifacts caused by the sharp transition between real image data and 0.
pub fn extend_border_nv12(
    y_plane: &mut [u8],
    uv_plane: &mut [u8],
    visible_width: usize,
    visible_height: usize,
    coded_width: usize,
    coded_height: usize,
) {
    assert!(visible_width > 1);
    assert!(visible_height > 1);
    for y in 0..visible_height {
        let row_start = y * coded_width;
        for x in visible_width..coded_width {
            y_plane[row_start + x] = y_plane[row_start + x - 1]
        }
    }
    for y in visible_height..coded_height {
        let (src, dst) = y_plane.split_at_mut(y * coded_width);
        dst[0..coded_width].copy_from_slice(&src[((y - 1) * coded_width)..(y * coded_width)]);
    }
    for y in 0..(visible_height / 2) {
        let row_start = y * coded_width;
        for x in visible_width..coded_width {
            // We use minus 2 here because we want to actually repeat the last 2 UV values.
            uv_plane[row_start + x] = uv_plane[row_start + x - 2]
        }
    }
    for y in (visible_height / 2)..(coded_height / 2) {
        let (src, dst) = uv_plane.split_at_mut(y * coded_width);
        dst[0..coded_width].copy_from_slice(&src[((y - 1) * coded_width)..(y * coded_width)]);
    }
}

pub fn copy_plane(
    src: &[u8],
    src_stride: usize,
    dst: &mut [u8],
    dst_stride: usize,
    width: usize,
    height: usize,
) {
    for y in 0..height {
        dst[(y * dst_stride)..(y * dst_stride + width)]
            .copy_from_slice(&src[(y * src_stride)..(y * src_stride + width)]);
    }
}

/// Copies `src` into `dst` as I4xx (YUV tri-planar).
///
/// `sub_h` and `sub_v` enable horizontal and vertical sub-sampling, respectively. E.g, if both
/// `sub_h` and `sub_v` are `true` the data will be `4:2:0`, if only `sub_v` is `true` then it will be
/// `4:2:2`, and if both are `false` then we have `4:4:4`.
pub fn i4xx_copy(
    src_y: &[u8],
    src_y_stride: usize,
    dst_y: &mut [u8],
    dst_y_stride: usize,
    src_u: &[u8],
    src_u_stride: usize,
    dst_u: &mut [u8],
    dst_u_stride: usize,
    src_v: &[u8],
    src_v_stride: usize,
    dst_v: &mut [u8],
    dst_v_stride: usize,
    width: usize,
    height: usize,
    (sub_h, sub_v): (bool, bool),
) {
    copy_plane(src_y, src_y_stride, dst_y, dst_y_stride, width, height);

    // Align width and height of UV planes to 2 if sub-sampling is used.
    let uv_width = if sub_h { (width + 1) / 2 } else { width };
    let uv_height = if sub_v { (height + 1) / 2 } else { height };

    copy_plane(src_u, src_u_stride, dst_u, dst_u_stride, uv_width, uv_height);
    copy_plane(src_v, src_v_stride, dst_v, dst_v_stride, uv_width, uv_height);
}

/// Copies `src` into `dst` as I410, removing all padding and changing the layout from packed to
/// triplanar. Also drops the alpha channel.
pub fn y410_to_i410(
    src: &[u8],
    dst: &mut [u8],
    width: usize,
    height: usize,
    strides: [usize; 3],
    offsets: [usize; 3],
) {
    let src_lines = src[offsets[0]..].chunks(strides[0]).map(|line| &line[..width * 4]);

    let dst_y_size = width * 2 * height;
    let dst_u_size = width * 2 * height;

    let (dst_y_plane, dst_uv_planes) = dst.split_at_mut(dst_y_size);
    let (dst_u_plane, dst_v_plane) = dst_uv_planes.split_at_mut(dst_u_size);
    let dst_y_lines = dst_y_plane.chunks_mut(width * 2);
    let dst_u_lines = dst_u_plane.chunks_mut(width * 2);
    let dst_v_lines = dst_v_plane.chunks_mut(width * 2);

    for (src_line, (dst_y_line, (dst_u_line, dst_v_line))) in
        src_lines.zip(dst_y_lines.zip(dst_u_lines.zip(dst_v_lines))).take(height)
    {
        for (src, (dst_y, (dst_u, dst_v))) in src_line.chunks(4).zip(
            dst_y_line.chunks_mut(2).zip(dst_u_line.chunks_mut(2).zip(dst_v_line.chunks_mut(2))),
        ) {
            let y = LittleEndian::read_u16(&[src[1] >> 2 | src[2] << 6, src[2] >> 2 & 0b11]);
            let u = LittleEndian::read_u16(&[src[0], src[1] & 0b11]);
            let v = LittleEndian::read_u16(&[src[2] >> 4 | src[3] << 4, src[3] >> 4 & 0b11]);
            LittleEndian::write_u16(dst_y, y);
            LittleEndian::write_u16(dst_u, u);
            LittleEndian::write_u16(dst_v, v);
        }
    }
}

#[cfg(feature = "v4l2")]
// SAFETY: Verified by caller that |src| and |dst| is valid and not
// a NULL-pointer or invalid memory.
pub unsafe fn align_detile(src: *const u8, src_tile_stride: isize, dst: *mut u8, width: usize) {
    let mut vin = [0u8; MM21_TILE_WIDTH];
    let mut vout = [0u8; MM21_TILE_WIDTH];

    let bytes_per_pixel = 1;
    let mask = MM21_TILE_WIDTH - 1;

    let remainder = width & mask;
    let width_aligned_down = width & !mask;
    if width_aligned_down > 0 {
        detile_row(src, src_tile_stride, dst, width_aligned_down);
    }

    let index = (width_aligned_down / MM21_TILE_WIDTH * (src_tile_stride as usize)) as usize;
    let input_slice =
        std::slice::from_raw_parts(src.offset(index as isize), remainder * bytes_per_pixel);
    (&mut vin[0..remainder * bytes_per_pixel])
        .copy_from_slice(&input_slice[0..remainder * bytes_per_pixel]);

    detile_row(vin.as_ptr(), src_tile_stride, vout.as_mut_ptr(), MM21_TILE_WIDTH);

    let output_slice = std::slice::from_raw_parts_mut(
        dst.offset(width_aligned_down as isize),
        remainder * bytes_per_pixel,
    );
    output_slice[0..remainder * bytes_per_pixel]
        .copy_from_slice(&vout[0..remainder * bytes_per_pixel]);
}

#[cfg(feature = "v4l2")]
// SAFETY: Verified by caller that |src| and |dst| is valid and not
// a NULL-pointer or invalid memory.
pub unsafe fn detile_row(
    mut src: *const u8,
    src_tile_stride: isize,
    mut dst: *mut u8,
    width: usize,
) {
    let mut w = width;
    while w > 0 {
        let v0: uint8x16_t = vld1q_u8(src);
        src = src.offset(src_tile_stride as isize);
        w = w - MM21_TILE_WIDTH;
        vst1q_u8(dst, v0);
        dst = dst.offset(MM21_TILE_WIDTH as isize);
    }
}

// TODO(bchoobineh): Use a fuzzer to verify the correctness of this SIMD
// code compared to its Rust equivalent
// Detiles a plane of data using implementation from LibYUV::DetilePlane.
#[cfg(feature = "v4l2")]
pub fn detile_plane(
    src: &[u8],
    src_stride: usize,
    dst: &mut [u8],
    mut dst_stride: isize,
    width: usize,
    mut height: isize,
    tile_height: usize,
) -> Result<(), String> {
    let src_tile_stride = (16 * tile_height) as isize;

    if width == 0 || height == 0 || (((tile_height) & ((tile_height) - 1)) > 0) {
        return Err("Invalid width, height, or tile height is not a power of 2.".to_owned());
    }

    let mut aligned = true;
    if (width & (MM21_TILE_WIDTH - 1)) > 0 {
        aligned = false;
    }

    if src.len() < (src_stride * (height.abs() as usize)) {
        return Err("Src buffer not big enough.".to_owned());
    }

    if dst.len() < (dst_stride * height.abs()) as usize {
        return Err("Dst buffer not big enough.".to_owned());
    }

    let mut src_ptr = src.as_ptr();
    let mut dst_ptr = dst.as_mut_ptr();

    // Image inversion
    if height < 0 {
        height = -height;
        // SAFETY: Verified the validity of src buffer and height.
        unsafe {
            src_ptr = src_ptr.offset(((height - 1) * dst_stride) as isize);
        }
        dst_stride = -dst_stride;
    }

    // Detile Plane
    for y in 0..height {
        // SAFETY: Verified validity of src and dst pointers.
        unsafe {
            if aligned {
                detile_row(src_ptr, src_tile_stride, dst_ptr, width);
            } else {
                align_detile(src_ptr, src_tile_stride, dst_ptr, width);
            }
        }

        // SAFETY: Verified the validity of the src and dst buffers.
        unsafe {
            dst_ptr = dst_ptr.offset(dst_stride as isize);
            src_ptr = src_ptr.offset(MM21_TILE_WIDTH as isize);
        }
        // Advance to next row of tiles.
        if (y & (tile_height - 1) as isize) == ((tile_height - 1) as isize) {
            // SAFETY: Verified validity of the src buffers.
            unsafe {
                src_ptr = src_ptr.offset(-src_tile_stride + (src_stride * tile_height) as isize);
            }
        }
    }

    Ok(())
}

// Converts MM21 to NV12 using the implementation from LibYUV::MM21ToNV12.
#[cfg(feature = "v4l2")]
pub fn mm21_to_nv12(
    src_y: &[u8],
    src_stride_y: usize,
    dst_y: &mut [u8],
    dst_stride_y: usize,
    src_uv: &[u8],
    src_stride_uv: usize,
    dst_uv: &mut [u8],
    dst_stride_uv: usize,
    width: usize,
    height: isize,
) -> Result<(), String> {
    if width <= 0 {
        return Err("Width must be greater than 0.".to_owned());
    }

    let sign = if height < 0 { -1 } else { 1 };

    // Detile Plane Y
    detile_plane(
        src_y,
        src_stride_y,
        dst_y,
        dst_stride_y as isize,
        width,
        height,
        MM21_TILE_HEIGHT,
    )?;

    // Detile Plane UV
    detile_plane(
        src_uv,
        src_stride_uv,
        dst_uv,
        dst_stride_uv as isize,
        (width + 1) & !1,
        (height + sign) / 2,
        MM21_TILE_HEIGHT / 2,
    )
}

pub fn nv12_to_i420(
    src_y: &[u8],
    src_y_stride: usize,
    dst_y: &mut [u8],
    dst_y_stride: usize,
    src_uv: &[u8],
    src_uv_stride: usize,
    dst_u: &mut [u8],
    dst_u_stride: usize,
    dst_v: &mut [u8],
    dst_v_stride: usize,
    width: usize,
    height: usize,
) {
    copy_plane(src_y, src_y_stride, dst_y, dst_y_stride, width, height);

    // We can just assume 4:2:0 subsampling
    let aligned_width = align_up(width, 2);
    for y in 0..(align_up(height, 2) / 2) {
        let src_row = &src_uv[(y * src_uv_stride)..(y * src_uv_stride + aligned_width)];
        let dst_u_row = &mut dst_u[(y * dst_u_stride)..(y * dst_u_stride + aligned_width / 2)];
        let dst_v_row = &mut dst_v[(y * dst_v_stride)..(y * dst_v_stride + aligned_width / 2)];
        for x in 0..aligned_width {
            if x % 2 == 0 {
                dst_u_row[x / 2] = src_row[x];
            } else {
                dst_v_row[x / 2] = src_row[x];
            }
        }
    }
}

pub fn i420_to_nv12(
    src_y: &[u8],
    src_y_stride: usize,
    dst_y: &mut [u8],
    dst_y_stride: usize,
    src_u: &[u8],
    src_u_stride: usize,
    src_v: &[u8],
    src_v_stride: usize,
    dst_uv: &mut [u8],
    dst_uv_stride: usize,
    width: usize,
    height: usize,
) {
    assert!(width <= src_y_stride);
    assert!(src_y_stride * height <= src_y.len());
    assert!(align_up(width, 2) / 2 <= src_u_stride);
    assert!(src_u_stride * align_up(height, 2) / 2 <= src_u.len());
    assert!(align_up(width, 2) / 2 <= src_v_stride);
    assert!(src_v_stride * align_up(height, 2) / 2 <= src_u.len());
    assert!(align_up(width, 2) <= dst_y_stride);
    assert!(dst_y_stride * height <= dst_y.len());
    assert!(align_up(width, 2) <= dst_uv_stride);
    assert!(dst_uv_stride * align_up(height, 2) / 2 <= dst_uv.len());

    copy_plane(src_y, src_y_stride, dst_y, dst_y_stride, width, height);

    for y in 0..(align_up(height, 2) / 2) {
        let mut aligned_width = align_up(width, 2);
        let mut src_u_row = &src_u[(y * src_u_stride)..(y * src_u_stride + aligned_width / 2)];
        let mut src_v_row = &src_v[(y * src_v_stride)..(y * src_v_stride + aligned_width / 2)];
        let mut dst_uv_row = &mut dst_uv[(y * dst_uv_stride)..(y * dst_uv_stride + aligned_width)];

        #[cfg(all(target_arch = "x86_64", target_feature = "avx2"))]
        {
            let simd_aligned_width = align_down(aligned_width / 2, 32);
            for x in (0..simd_aligned_width).step_by(32) {
                // SAFETY: The above logic guarantees that src_u_row, src_v_row, and dst_uv_row are
                // valid slices, and thus the pointers to them are valid. We are also guaranteed
                // that simd_aligned_width will not exceed the length of src_u_row and src_v_row,
                // and that 2 * simd_aligned_width will not exceed the length of dst_uv_row. Note
                // that loadu and storeu have no memory alignment requirements.
                unsafe {
                    let input_u =
                        _mm256_loadu_si256(src_u_row[x..(x + 32)].as_ptr() as *const __m256i);
                    let input_v =
                        _mm256_loadu_si256(src_v_row[x..(x + 32)].as_ptr() as *const __m256i);
                    let output1 = _mm256_unpacklo_epi8(input_u, input_v);
                    let output2 = _mm256_unpackhi_epi8(input_u, input_v);
                    _mm256_storeu_si256(
                        dst_uv_row[(2 * x)..(2 * x + 32)].as_mut_ptr() as *mut __m256i,
                        _mm256_permute2x128_si256(output1, output2, 0x20),
                    );
                    _mm256_storeu_si256(
                        dst_uv_row[(2 * x + 32)..(2 * x + 64)].as_mut_ptr() as *mut __m256i,
                        _mm256_permute2x128_si256(output1, output2, 0x31),
                    );
                }
            }

            src_u_row = &src_u_row[simd_aligned_width..];
            src_v_row = &src_v_row[simd_aligned_width..];
            dst_uv_row = &mut dst_uv_row[(2 * simd_aligned_width)..];
            aligned_width -= 2 * simd_aligned_width;
        }
        #[cfg(all(target_arch = "aarch64", target_feature = "neon"))]
        {
            let simd_aligned_width = align_down(aligned_width / 2, 16);
            for x in (0..simd_aligned_width).step_by(16) {
                // SAFETY: The above logic guarantees that src_u_row, src_v_row, and dst_uv_row are
                // valid slices, and thus the pointers to them are valid. We are also guaranteed
                // that simd_aligned_width will not exceed the length of src_u_row and src_v_row,
                // and that 2 * simd_aligned_width will not exceed the length of dst_uv_row. Note
                // that vld1q_u8 and vst2q_u8 have no memory alignment requirements.
                unsafe {
                    vst2q_u8(
                        dst_uv_row[(2 * x)..(2 * x + 32)].as_mut_ptr(),
                        uint8x16x2_t(
                            vld1q_u8(src_u_row[x..(x + 16)].as_ptr()),
                            vld1q_u8(src_v_row[x..(x + 16)].as_ptr()),
                        ),
                    );
                }
            }

            src_u_row = &src_u_row[simd_aligned_width..];
            src_v_row = &src_v_row[simd_aligned_width..];
            dst_uv_row = &mut dst_uv_row[(2 * simd_aligned_width)..];
            aligned_width -= 2 * simd_aligned_width;
        }

        for x in 0..aligned_width {
            if x % 2 == 0 {
                dst_uv_row[x] = src_u_row[x / 2];
            } else {
                dst_uv_row[x] = src_v_row[x / 2];
            }
        }
    }
}

// TODO: Add more conversions. All supported conversion functions need to take stride parameters.
pub const SUPPORTED_CONVERSION: &'static [(DecodedFormat, DecodedFormat)] = &[
    #[cfg(feature = "v4l2")]
    (DecodedFormat::MM21, DecodedFormat::NV12),
    (DecodedFormat::MM21, DecodedFormat::I420),
    (DecodedFormat::NV12, DecodedFormat::NV12),
    (DecodedFormat::NV12, DecodedFormat::I420),
    (DecodedFormat::I420, DecodedFormat::I420),
    (DecodedFormat::I420, DecodedFormat::NV12),
    (DecodedFormat::I422, DecodedFormat::I422),
    (DecodedFormat::I444, DecodedFormat::I444),
    (DecodedFormat::AR24, DecodedFormat::NV12),
];

pub fn convert_video_frame(src: &impl VideoFrame, dst: &mut impl VideoFrame) -> Result<(), String> {
    let width = min(dst.resolution().width, src.resolution().width) as usize;
    let height = min(dst.resolution().height, src.resolution().height) as usize;

    let conversion = (src.decoded_format()?, dst.decoded_format()?);
    let src_pitches = src.get_plane_pitch();
    let src_mapping = src.map().expect("Image processor src mapping failed!");
    let src_planes = src_mapping.get();
    let dst_pitches = dst.get_plane_pitch();
    let dst_mapping = dst.map_mut().expect("Image processor dst mapping failed!");
    let dst_planes = dst_mapping.get();
    match conversion {
        #[cfg(feature = "v4l2")]
        (DecodedFormat::MM21, DecodedFormat::NV12) => mm21_to_nv12(
            src_planes[Y_PLANE],
            src_pitches[Y_PLANE],
            *dst_planes[Y_PLANE].borrow_mut(),
            dst_pitches[Y_PLANE],
            src_planes[UV_PLANE],
            src_pitches[UV_PLANE],
            *dst_planes[UV_PLANE].borrow_mut(),
            dst_pitches[UV_PLANE],
            width,
            height as isize,
        ),
        #[cfg(feature = "v4l2")]
        (DecodedFormat::MM21, DecodedFormat::I420) => {
            let mut tmp_y: Vec<u8> = vec![0; src_planes[Y_PLANE].len()];
            let mut tmp_uv: Vec<u8> = vec![0; src_planes[UV_PLANE].len()];
            mm21_to_nv12(
                src_planes[Y_PLANE],
                src_pitches[Y_PLANE],
                &mut tmp_y[..],
                src_pitches[Y_PLANE],
                src_planes[UV_PLANE],
                src_pitches[UV_PLANE],
                &mut tmp_uv[..],
                src_pitches[UV_PLANE],
                width,
                height as isize,
            )?;
            nv12_to_i420(
                &mut tmp_y[..],
                src_pitches[Y_PLANE],
                *dst_planes[Y_PLANE].borrow_mut(),
                dst_pitches[Y_PLANE],
                &mut tmp_uv[..],
                src_pitches[UV_PLANE],
                *dst_planes[U_PLANE].borrow_mut(),
                dst_pitches[U_PLANE],
                *dst_planes[V_PLANE].borrow_mut(),
                dst_pitches[V_PLANE],
                width,
                height,
            );
            Ok(())
        }
        (DecodedFormat::NV12, DecodedFormat::NV12) => {
            nv12_copy(
                src_planes[Y_PLANE],
                src_pitches[Y_PLANE],
                *dst_planes[Y_PLANE].borrow_mut(),
                dst_pitches[Y_PLANE],
                src_planes[UV_PLANE],
                src_pitches[UV_PLANE],
                *dst_planes[UV_PLANE].borrow_mut(),
                dst_pitches[UV_PLANE],
                width,
                height,
            );
            Ok(())
        }
        (DecodedFormat::NV12, DecodedFormat::I420) => {
            nv12_to_i420(
                src_planes[Y_PLANE],
                src_pitches[Y_PLANE],
                *dst_planes[Y_PLANE].borrow_mut(),
                dst_pitches[Y_PLANE],
                src_planes[UV_PLANE],
                src_pitches[UV_PLANE],
                *dst_planes[U_PLANE].borrow_mut(),
                dst_pitches[U_PLANE],
                *dst_planes[V_PLANE].borrow_mut(),
                dst_pitches[V_PLANE],
                width,
                height,
            );
            Ok(())
        }
        (DecodedFormat::I420, DecodedFormat::I420) => {
            i4xx_copy(
                src_planes[Y_PLANE],
                src_pitches[Y_PLANE],
                *dst_planes[Y_PLANE].borrow_mut(),
                dst_pitches[Y_PLANE],
                src_planes[U_PLANE],
                src_pitches[U_PLANE],
                *dst_planes[U_PLANE].borrow_mut(),
                dst_pitches[U_PLANE],
                src_planes[V_PLANE],
                src_pitches[V_PLANE],
                *dst_planes[V_PLANE].borrow_mut(),
                dst_pitches[V_PLANE],
                width,
                height,
                (true, true),
            );
            Ok(())
        }
        (DecodedFormat::I420, DecodedFormat::NV12) => {
            i420_to_nv12(
                src_planes[Y_PLANE],
                src_pitches[Y_PLANE],
                *dst_planes[Y_PLANE].borrow_mut(),
                dst_pitches[Y_PLANE],
                src_planes[U_PLANE],
                src_pitches[U_PLANE],
                src_planes[V_PLANE],
                src_pitches[V_PLANE],
                *dst_planes[UV_PLANE].borrow_mut(),
                dst_pitches[UV_PLANE],
                width,
                height,
            );
            Ok(())
        }
        (DecodedFormat::I422, DecodedFormat::I422) => {
            i4xx_copy(
                src_planes[Y_PLANE],
                src_pitches[Y_PLANE],
                *dst_planes[Y_PLANE].borrow_mut(),
                dst_pitches[Y_PLANE],
                src_planes[U_PLANE],
                src_pitches[U_PLANE],
                *dst_planes[U_PLANE].borrow_mut(),
                dst_pitches[U_PLANE],
                src_planes[V_PLANE],
                src_pitches[V_PLANE],
                *dst_planes[V_PLANE].borrow_mut(),
                dst_pitches[V_PLANE],
                width,
                height,
                (true, false),
            );
            Ok(())
        }
        (DecodedFormat::I444, DecodedFormat::I444) => {
            i4xx_copy(
                src_planes[Y_PLANE],
                src_pitches[Y_PLANE],
                *dst_planes[Y_PLANE].borrow_mut(),
                dst_pitches[Y_PLANE],
                src_planes[U_PLANE],
                src_pitches[U_PLANE],
                *dst_planes[U_PLANE].borrow_mut(),
                dst_pitches[U_PLANE],
                src_planes[V_PLANE],
                src_pitches[V_PLANE],
                *dst_planes[V_PLANE].borrow_mut(),
                dst_pitches[V_PLANE],
                width,
                height,
                (false, false),
            );
            Ok(())
        }
        (DecodedFormat::AR24, DecodedFormat::NV12) => {
            argb_to_nv12(
                src_planes[ARGB_PLANE],
                src_pitches[ARGB_PLANE],
                *dst_planes[Y_PLANE].borrow_mut(),
                dst_pitches[Y_PLANE],
                *dst_planes[UV_PLANE].borrow_mut(),
                dst_pitches[UV_PLANE],
                width,
                height,
            );
            Ok(())
        }
        _ => Err(format!("Unsupported conversion {:?} -> {:?}", conversion.0, conversion.1)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    #[cfg(feature = "v4l2")]
    fn test_mm21_to_nv12() {
        let test_input = include_bytes!("test_data/puppets-480x270_20230825.mm21.yuv");
        let test_expected_output = include_bytes!("test_data/puppets-480x270_20230825.nv12.yuv");

        let mut test_output = [0u8; 480 * 288 * 3 / 2];
        let (test_y_output, test_uv_output) = test_output.split_at_mut(480 * 288);
        mm21_to_nv12(
            &test_input[0..480 * 288],
            480,
            test_y_output,
            480,
            &test_input[480 * 288..480 * 288 * 3 / 2],
            480,
            test_uv_output,
            480,
            480,
            288,
        )
        .expect("Failed to detile!");
        assert_eq!(test_output, *test_expected_output);
    }

    #[test]
    fn test_nv12_to_i420() {
        let test_input = include_bytes!("test_data/puppets-480x270_20230825.nv12.yuv");
        let test_expected_output = include_bytes!("test_data/puppets-480x270_20230825.i420.yuv");

        let mut test_output = [0u8; 480 * 288 * 3 / 2];
        let (test_y_output, test_uv_output) = test_output.split_at_mut(480 * 288);
        let (test_u_output, test_v_output) = test_uv_output.split_at_mut(480 * 288 / 4);
        nv12_to_i420(
            &test_input[0..480 * 288],
            test_y_output,
            &test_input[480 * 288..480 * 288 * 3 / 2],
            test_u_output,
            test_v_output,
        );
        assert_eq!(test_output, *test_expected_output);
    }

    #[test]
    fn test_i420_to_nv12() {
        let test_input = include_bytes!("test_data/puppets-480x270_20230825.i420.yuv");
        let test_expected_output = include_bytes!("test_data/puppets-480x270_20230825.nv12.yuv");

        let mut test_output = [0u8; 480 * 288 * 3 / 2];
        let (test_y_output, test_uv_output) = test_output.split_at_mut(480 * 288);
        i420_to_nv12(
            &test_input[0..(480 * 288)],
            480,
            test_y_output,
            &test_input[(480 * 288)..(480 * 288 * 5 / 4)],
            240,
            &test_input[(480 * 288 * 5 / 4)..(480 * 288 * 3 / 2)],
            240,
            test_uv_output,
            480,
            480,
            280,
        );
        assert_eq!(test_output, *test_expected_output);
    }
}
