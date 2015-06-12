/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file was copied from https://github.com/devttys0/libmpsse.git (sha1
 * f1a6744b), and modified to suite the Chromium OS project.
 */

#ifndef TRUNKS_FTDI_SUPPORT_H_
#define TRUNKS_FTDI_SUPPORT_H_

#include "trunks/ftdi/mpsse.h"

int raw_write(struct mpsse_context* mpsse, unsigned char* buf, int size);
int raw_read(struct mpsse_context* mpsse, unsigned char* buf, int size);
void set_timeouts(struct mpsse_context* mpsse, int timeout);
uint16_t freq2div(uint32_t system_clock, uint32_t freq);
uint32_t div2freq(uint32_t system_clock, uint16_t div);
unsigned char* build_block_buffer(struct mpsse_context* mpsse,
                                  uint8_t cmd,
                                  unsigned char* data,
                                  int size,
                                  int* buf_size);
int set_bits_high(struct mpsse_context* mpsse, int port);
int set_bits_low(struct mpsse_context* mpsse, int port);
int gpio_write(struct mpsse_context* mpsse, int pin, int direction);
int is_valid_context(struct mpsse_context* mpsse);

#endif  /*  TRUNKS_FTDI_SUPPORT_H_ */
