/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Paul Sokolovsky
 * Copyright (c) 2016 Damien P. George
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

#include "api_event.h"
#include "py/obj.h"

extern const mp_obj_type_t mp_type_CellularError;
extern const mp_obj_type_t mp_type_CellularRegistrationError;
extern const mp_obj_type_t mp_type_SMSError;
extern const mp_obj_type_t mp_type_NoSIMError;
extern const mp_obj_type_t mp_type_CellularAttachmentError;
extern const mp_obj_type_t mp_type_CellularActivationError;

void modcellular_init0(void);

void modcellular_notify_no_sim(API_Event_t* event);
void modcellular_notify_sim_drop(API_Event_t* event);

void modcellular_notify_reg_home(API_Event_t* event);
void modcellular_notify_reg_roaming(API_Event_t* event);
void modcellular_notify_reg_searching(API_Event_t* event);
void modcellular_notify_reg_denied(API_Event_t* event);
void modcellular_notify_dereg(API_Event_t* event);

void modcellular_notify_det(API_Event_t* event);
void modcellular_notify_att_failed(API_Event_t* event);
void modcellular_notify_att(API_Event_t* event);

void modcellular_notify_deact(API_Event_t* event);
void modcellular_notify_act_failed(API_Event_t* event);
void modcellular_notify_act(API_Event_t* event);

void modcellular_notify_sms_list(API_Event_t* event);
void modcellular_notify_sms_sent(API_Event_t* event);
void modcellular_notify_sms_error(API_Event_t* event);
void modcellular_notify_sms_receipt(API_Event_t* event);
void modcellular_notify_signal(API_Event_t* event);

#define MAX_SMS_LIST_TIMEOUT 1000
#define MAX_SMS_SEND_TIMEOUT 10000
#define MAX_ATT_TIMEOUT 10000
#define MAX_ACT_TIMEOUT 10000

#define REQUIRES_NETWORK_REGISTRATION do {if (!network_status) {mp_raise_CellularRegistrationError("Network is not available: is SIM card inserted?"); return mp_const_none;}} while(0)
#define REQUIRES_VALID_SMS_STATUS(bits) do {if (bitsum(bits) != 1) {return mp_const_none;}} while(0)

