/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 pulkin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef __MPHALPORT_H
#define __MPHALPORT_H

#include "stdint.h"
#include "stdbool.h"
#include "shared/runtime/interrupt_char.h"
#include "time.h"
#include "py/obj.h"
#include "uart.h"

#define MP_HAL_PIN_FMT "%u"
#define mp_hal_pin_name(p) (p)
#define mp_hal_pin_obj_t uint32_t

extern int uart_attached_to_dupterm[UART_NPORTS];

void mp_hal_set_interrupt_char(int c);
void mp_hal_pyrepl_uart_init();

uint32_t mp_hal_ticks_ms(void);
uint32_t mp_hal_ticks_us(void);
void mp_hal_delay_ms(uint32_t ms);
void mp_hal_delay_us(uint32_t us);
void mp_hal_delay_us_fast(uint32_t us);
__attribute__((always_inline)) static inline uint32_t mp_hal_ticks_cpu(void) {
  return clock();
}

mp_hal_pin_obj_t mp_hal_get_pin_obj(mp_obj_t pin_in);
void mp_hal_pin_input(mp_hal_pin_obj_t pin_id);
void mp_hal_pin_output(mp_hal_pin_obj_t pin_id);
int mp_hal_pin_read(mp_hal_pin_obj_t pin_id);
void mp_hal_pin_write(mp_hal_pin_obj_t pin_id, int value);

#endif

