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

#include "modcellular.h"
#include "mphalport.h"

#include "py/nlr.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/binary.h"
#include "py/objexcept.h"

#include "api_info.h"
#include "api_sim.h"
#include "api_sms.h"
#include "api_os.h"
#include "api_network.h"

#include "buffer.h"
#include "time.h"

#define NTW_REG_BIT 0x01
#define NTW_ROAM_BIT 0x02
#define NTW_REG_PROGRESS_BIT 0x04
#define NTW_ATT_BIT 0x08
#define NTW_ACT_BIT 0x10

#define NTW_NO_EXC 0
#define NTW_EXC_NOSIM 0x01
#define NTW_EXC_REG_DENIED 0x02
#define NTW_EXC_SMS_SEND 0x03
#define NTW_EXC_SIM_DROP 0x04
#define NTW_EXC_ATT_FAILED 0x05
#define NTW_EXC_ACT_FAILED 0x06
#define NTW_EXC_SMS_DROP 0x07

// --------------
// Vars: statuses
// --------------

// Tracks the status on the network
uint8_t network_status = 0;
uint8_t network_exception = NTW_NO_EXC;
uint8_t network_status_updated = 0;
uint8_t network_signal_quality = 0;
uint8_t network_signal_rx_level = 0;

// Count SMS received
uint8_t sms_received_count = 0;

// SMS send flag
uint8_t sms_send_flag = 0;

// -------------------
// Vars: SMS retrieval
// -------------------

// A buffer used for listing messages
mp_obj_list_t *sms_list_buffer = NULL;
uint8_t sms_list_buffer_count = 0;

// ---------------------------
// Vars: Network operator list
// ---------------------------

uint8_t network_list_buffer_len = 0;
Network_Operator_Info_t *network_list_buffer = NULL;

// SMS parsing
STATIC mp_obj_t modcellular_sms_from_record(SMS_Message_Info_t* record);
STATIC mp_obj_t modcellular_sms_from_raw(uint8_t* header, uint32_t header_length, uint8_t* content, uint32_t content_length);

void modcellular_init0(void) {
    network_status_updated = 0;
    network_exception = 0;
    sms_received_count = 0;
}

// ----------
// Exceptions
// ----------

MP_DEFINE_EXCEPTION(CellularError, OSError)

NORETURN void mp_raise_CellularError(const char *msg) {
    mp_raise_msg(&mp_type_CellularError, msg);
}

// ------
// Notify
// ------

void modcellular_notify_no_sim(API_Event_t* event) {
    network_status = 0;
    network_status_updated = 1;
    network_exception = NTW_EXC_NOSIM;
}

void modcellular_notify_sim_drop(API_Event_t* event) {
    network_status = 0;
    network_status_updated = 1;
    network_exception = NTW_EXC_SIM_DROP;
}

// Register

void modcellular_notify_reg_home(API_Event_t* event) {
    network_status = NTW_REG_BIT;
    network_status_updated = 1;
}

void modcellular_notify_reg_roaming(API_Event_t* event) {
    network_status = NTW_REG_BIT | NTW_ROAM_BIT;
    network_status_updated = 1;
}

void modcellular_notify_reg_searching(API_Event_t* event) {
    network_status = NTW_REG_PROGRESS_BIT;
    network_status_updated = 1;
}

void modcellular_notify_reg_denied(API_Event_t* event) {
    network_status = 0;
    network_status_updated = 1;
    network_exception = NTW_EXC_REG_DENIED;
}

void modcellular_notify_dereg(API_Event_t* event) {
    network_status = 0;
    network_status_updated = 1;
}

// Attach

void modcellular_notify_det(API_Event_t* event) {
    network_status &= ~NTW_ATT_BIT;
    network_status_updated = 1;
}

void modcellular_notify_att_failed(API_Event_t* event) {
    network_status &= ~NTW_ATT_BIT;
    network_status_updated = 1;
    network_exception = NTW_EXC_ATT_FAILED;
}

void modcellular_notify_att(API_Event_t* event) {
    network_status |= NTW_ATT_BIT;
    network_status_updated = 1;
}

// Activate

void modcellular_notify_deact(API_Event_t* event) {
    network_status &= ~NTW_ACT_BIT;
    network_status_updated = 1;
}

void modcellular_notify_act_failed(API_Event_t* event) {
    network_status &= ~NTW_ACT_BIT;
    network_status_updated = 1;
    network_exception = NTW_EXC_ACT_FAILED;
}

void modcellular_notify_act(API_Event_t* event) {
    network_status |= NTW_ACT_BIT;
    network_status_updated = 1;
}

// Networks

void modcellular_notify_ntwlist(API_Event_t* event) {
    network_list_buffer_len = event->param1;

    if (network_list_buffer != NULL)
        free(network_list_buffer);
    network_list_buffer = malloc(sizeof(Network_Operator_Info_t) * (network_list_buffer_len + 1));  // One more item added for empty outputs
    if (network_list_buffer != NULL)
        memcpy(network_list_buffer, event->pParam1, sizeof(Network_Operator_Info_t) * network_list_buffer_len);
}

// SMS

void modcellular_notify_sms_list(API_Event_t* event) {
    SMS_Message_Info_t* messageInfo = (SMS_Message_Info_t*)event->pParam1;

    if (sms_list_buffer && (sms_list_buffer->len > sms_list_buffer_count)) {
        sms_list_buffer->items[sms_list_buffer_count] = modcellular_sms_from_record(messageInfo);
        sms_list_buffer_count ++;
    } else {
        network_exception = NTW_EXC_SMS_DROP;
    }
    OS_Free(messageInfo->data);
}

void modcellular_notify_sms_sent(API_Event_t* event) {
    sms_send_flag = 1;
}

void modcellular_notify_sms_error(API_Event_t* event) {
    network_exception = NTW_EXC_SMS_SEND;
}

void modcellular_notify_sms_receipt(API_Event_t* event) {
    sms_received_count ++;
}

void modcellular_notify_signal(API_Event_t* event) {
    network_signal_quality = event->param1;
    network_signal_rx_level = event->param2;
}

// -------
// Classes
// -------

typedef struct _sms_obj_t {
    mp_obj_base_t base;
    uint8_t index;
    uint8_t status;
    uint8_t phone_number_type;
    mp_obj_t phone_number;
    mp_obj_t message;
} sms_obj_t;

mp_obj_t modcellular_sms_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {

    enum { ARG_phone_number, ARG_message };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_phone_number, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_message, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    sms_obj_t *self = m_new_obj(sms_obj_t);
    self->base.type = type;
    self->index = 0;
    self->status = 0;
    // TODO: fix the following
    self->phone_number_type = 0;
    self->phone_number = args[ARG_phone_number].u_obj;
    self->message = args[ARG_message].u_obj;
    return MP_OBJ_FROM_PTR(self);
}

uint8_t bitsum(uint32_t i) {
     i = i - ((i >> 1) & 0x55555555);
     i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
     return (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

STATIC mp_obj_t modcellular_sms_inbox(mp_obj_t self_in) {
    // ========================================
    // Determines if SMS is inbox or outbox.
    // Returns:
    //     True if inbox.
    // ========================================
    sms_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t s = self->status;
    REQUIRES_VALID_SMS_STATUS(s);
    return mp_obj_new_bool(s & (SMS_STATUS_READ | SMS_STATUS_UNREAD));
}

STATIC mp_obj_t modcellular_sms_unread(mp_obj_t self_in) {
    // ========================================
    // Determines if SMS is unread.
    // Returns:
    //     True if unread.
    // ========================================
    sms_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t s = self->status;
    REQUIRES_VALID_SMS_STATUS(s);
    return mp_obj_new_bool(s & SMS_STATUS_UNREAD);
}

STATIC mp_obj_t modcellular_sms_sent(mp_obj_t self_in) {
    // ========================================
    // Determines if SMS was sent.
    // Returns:
    //     True if it was sent.
    // ========================================
    sms_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t s = self->status;
    REQUIRES_VALID_SMS_STATUS(s);
    return mp_obj_new_bool(!(s | SMS_STATUS_UNSENT));
}

STATIC mp_obj_t modcellular_sms_send(mp_obj_t self_in) {
    // ========================================
    // Sends an SMS messsage.
    // ========================================
    REQUIRES_NETWORK_REGISTRATION;

    sms_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->status != 0) {
        mp_raise_CellularError("A message with non-zero status cannot be sent");
        return mp_const_none;
    }

    const char* destination_c = mp_obj_str_get_str(self->phone_number);
    const char* message_c = mp_obj_str_get_str(self->message);

    if (!SMS_SetFormat(SMS_FORMAT_TEXT, SIM0)) {
        mp_raise_CellularError("Failed to set SMS format");
        return mp_const_none;
    } 

    SMS_Parameter_t smsParam = {
        .fo = 17 , // stadard values
        .vp = 167,
        .pid= 0  ,
        .dcs= 8  , // 0:English 7bit, 4:English 8 bit, 8:Unicode 2 Bytes
    };

    if (!SMS_SetParameter(&smsParam, SIM0)) {
        mp_raise_CellularError("Failed to set SMS parameters");
        return mp_const_none;
    }

    if (!SMS_SetNewMessageStorage(SMS_STORAGE_SIM_CARD)) {
        mp_raise_CellularError("Failed to set SMS storage in the SIM card");
        return mp_const_none;
    }

    uint8_t* unicode = NULL;
    uint32_t unicodeLen;

    if (!SMS_LocalLanguage2Unicode((uint8_t*)message_c, strlen(message_c), CHARSET_UTF_8, &unicode, &unicodeLen)) {
        mp_raise_CellularError("Failed to convert to Unicode before sending SMS");
        return mp_const_none;
    }

    sms_send_flag = 0;
    if (!SMS_SendMessage(destination_c, unicode, unicodeLen, SIM0)) {
        OS_Free(unicode);
        mp_raise_CellularError("Failed to submit SMS message for sending");
        return mp_const_none;
    }
    OS_Free(unicode);
    WAIT_UNTIL(sms_send_flag, TIMEOUT_SMS_SEND, 100, mp_warning(NULL, "Failed to send SMS. The module will continue attempting sending it"));

    sms_send_flag = 0;
    return mp_const_none;
}

MP_DEFINE_CONST_FUN_OBJ_1(modcellular_sms_send_obj, &modcellular_sms_send);

STATIC mp_obj_t modcellular_sms_withdraw(mp_obj_t self_in) {
    // ========================================
    // Withdraws an SMS message from the SIM card.
    // ========================================

    sms_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->index == 0 || self->status == 0) {
        mp_raise_CellularError("Cannot withdraw SMS with zero index/status");
        return mp_const_none;
    }

    if (!SMS_DeleteMessage(self->index, SMS_STATUS_ALL, SMS_STORAGE_SIM_CARD)) {
        mp_raise_CellularError("Failed to withdraw SMS");
        return mp_const_none;
    }

    self->status = 0;
    self->index = 0;
    return mp_const_none;
}

MP_DEFINE_CONST_FUN_OBJ_1(modcellular_sms_withdraw_obj, &modcellular_sms_withdraw);

STATIC mp_obj_t modcellular_sms_list(void) {
    // ========================================
    // Lists SMS messages.
    // Returns:
    //     A list of SMS messages.
    // ========================================
    REQUIRES_NETWORK_REGISTRATION;

    SMS_Storage_Info_t storage;
    SMS_GetStorageInfo(&storage, SMS_STORAGE_SIM_CARD);
    
    sms_list_buffer = mp_obj_new_list(storage.used, NULL);
    sms_list_buffer_count = 0;

    SMS_ListMessageRequst(SMS_STATUS_ALL, SMS_STORAGE_SIM_CARD);
    WAIT_UNTIL(sms_list_buffer_count == storage.used, TIMEOUT_SMS_LIST, 100, mp_warning(NULL, "Failed to poll all SMS: the list may be incomplete"));

    mp_obj_list_t *result = sms_list_buffer;
    sms_list_buffer = NULL;
    
    uint16_t i;
    for (i = sms_list_buffer_count; i < result->len; i++) {
        result->items[i] = mp_const_none;
    }
    sms_list_buffer_count = 0;

    return (mp_obj_t)result;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_sms_list_obj, modcellular_sms_list);
STATIC MP_DEFINE_CONST_STATICMETHOD_OBJ(modcellular_sms_list_static_class_obj, MP_ROM_PTR(&modcellular_sms_list_obj));

STATIC mp_obj_t modcellular_sms_poll(void) {
    // ========================================
    // Polls new SMS.
    // Returns:
    //     The number of SMS received.
    // ========================================
    mp_obj_t result = mp_obj_new_int(sms_received_count);
    sms_received_count = 0;
    return result;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_sms_poll_obj, modcellular_sms_poll);
STATIC MP_DEFINE_CONST_STATICMETHOD_OBJ(modcellular_sms_poll_static_class_obj, MP_ROM_PTR(&modcellular_sms_poll_obj));

STATIC void modcellular_sms_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    // ========================================
    // SMS.[attr]
    // ========================================
    if (dest[0] != MP_OBJ_NULL) {
    } else {
        sms_obj_t *self = MP_OBJ_TO_PTR(self_in);
        // .phone_number
        if (attr == MP_QSTR_phone_number) {
            dest[0] = self->phone_number;
        // .message
        } else if (attr == MP_QSTR_message) {
            dest[0] = self->message;
        // .status
        } else if (attr == MP_QSTR_status) {
            dest[0] = mp_obj_new_int(self->status);
        // .inbox
        } else if (attr == MP_QSTR_inbox) {
            dest[0] = modcellular_sms_inbox(self_in);
        // .unread
        } else if (attr == MP_QSTR_unread) {
            dest[0] = modcellular_sms_unread(self_in);
        // .sent
        } else if (attr == MP_QSTR_sent) {
            dest[0] = modcellular_sms_sent(self_in);
        // .send
        } else if (attr == MP_QSTR_send) {
            mp_convert_member_lookup(self_in, mp_obj_get_type(self_in), (mp_obj_t)MP_ROM_PTR(&modcellular_sms_send_obj), dest);
        // .withdraw
        } else if (attr == MP_QSTR_withdraw) {
            mp_convert_member_lookup(self_in, mp_obj_get_type(self_in), (mp_obj_t)MP_ROM_PTR(&modcellular_sms_withdraw_obj), dest);
        // .list[static]
        } else if (attr == MP_QSTR_list) {
            mp_convert_member_lookup(self_in, mp_obj_get_type(self_in), (mp_obj_t)MP_ROM_PTR(&modcellular_sms_list_static_class_obj), dest);
        // .poll[static]
        } else if (attr == MP_QSTR_poll) {
            mp_convert_member_lookup(self_in, mp_obj_get_type(self_in), (mp_obj_t)MP_ROM_PTR(&modcellular_sms_poll_static_class_obj), dest);
        }
    }
}

STATIC void modcellular_sms_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    // ========================================
    // SMS.__str__()
    // ========================================
    sms_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "SMS(\"%s\"(%s), \"%s\", 0x%02x, #%d)",
            mp_obj_str_get_str(self->phone_number),
            self->phone_number_type == SMS_NUMBER_TYPE_UNKNOWN ? "unk" :
            self->phone_number_type == SMS_NUMBER_TYPE_INTERNATIONAL ? "int" :
            self->phone_number_type == SMS_NUMBER_TYPE_NATIONAL ? "loc" : "???",
            mp_obj_str_get_str(self->message),
            self->status,
            self->index
    );
}

STATIC const mp_rom_map_elem_t sms_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_send), MP_ROM_PTR(&modcellular_sms_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_withdraw), MP_ROM_PTR(&modcellular_sms_withdraw_obj) },
    { MP_ROM_QSTR(MP_QSTR_list), MP_ROM_PTR(&modcellular_sms_list_static_class_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&modcellular_sms_poll_static_class_obj) },
};

STATIC MP_DEFINE_CONST_DICT(sms_locals_dict, sms_locals_dict_table);

STATIC const mp_obj_type_t modcellular_sms_type = {
    { &mp_type_type },
    .name = MP_QSTR_SMS,
    .make_new = modcellular_sms_make_new,
    .print = modcellular_sms_print,
    .attr = modcellular_sms_attr,
    .locals_dict = (mp_obj_dict_t*)&sms_locals_dict,
};

// -------
// Private
// -------

STATIC mp_obj_t modcellular_sms_from_record(SMS_Message_Info_t* record) {
    // ========================================
    // Prepares an SMS object from the record.
    // Returns:
    //     A new SMS object.
    // ========================================
    sms_obj_t *self = m_new_obj_with_finaliser(sms_obj_t);
    self->base.type = &modcellular_sms_type;
    self->index = record->index;
    self->status = (uint8_t)record->status;
    self->phone_number_type = (uint8_t)record->phoneNumberType;
    self->phone_number = mp_obj_new_str((char*)record->phoneNumber + 1, SMS_PHONE_NUMBER_MAX_LEN - 1);
    self->message = mp_obj_new_str((char*)record->data, record->dataLen);
    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t modcellular_sms_from_raw(uint8_t* header, uint32_t header_length, uint8_t* content, uint32_t content_length) {
    // ========================================
    // Prepares an SMS object from raw header and contents.
    // Returns:
    //     A new SMS object.
    // ========================================

    // TODO: This function is not tested / not used. Consider removing it.
    if (header[0] != '"') {
        return mp_const_none;
    }

    uint32_t i;
    for (i=1; i<header_length; i++) {
        if (header[i] == '"') {
            break;
        }
    }

    if (header[i] != '"') {
        return mp_const_none;
    }

    sms_obj_t *self = m_new_obj_with_finaliser(sms_obj_t);
    self->base.type = &modcellular_sms_type;
    self->index = 0;
    self->status = 0;
    self->phone_number_type = 0;
    self->phone_number = mp_obj_new_str((char*)header + 1, i - 1);
    self->message = mp_obj_new_str((char*)content, content_length);
    return MP_OBJ_FROM_PTR(self);
}

// -------
// Methods
// -------

STATIC mp_obj_t modcellular_get_signal_quality(void) {
    // ========================================
    // Retrieves the network signal quality.
    // Returns:
    //     Two integers: quality
    // ========================================
    mp_obj_t tuple[2] = {
        network_signal_quality == 99 ? mp_const_none : mp_obj_new_int(network_signal_quality),
        network_signal_rx_level == 99 ? mp_const_none : mp_obj_new_int(network_signal_rx_level),
    };
    return mp_obj_new_tuple(2, tuple);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_get_signal_quality_obj, modcellular_get_signal_quality);

STATIC mp_obj_t modcellular_get_imei(void) {
    // ========================================
    // Retrieves IMEI number.
    // Returns:
    //     IMEI number as a string.
    // ========================================
    char imei[16];
    memset(imei,0,sizeof(imei));
    INFO_GetIMEI((uint8_t*)imei);
    return mp_obj_new_str(imei, strlen(imei));
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_get_imei_obj, modcellular_get_imei);

STATIC mp_obj_t modcellular_is_sim_present(void) {
    // ========================================
    // Checks whether the SIM card is inserted and ICCID can be retrieved.
    // Returns:
    //     True if SIM present.
    // ========================================
    char iccid[21];
    memset(iccid, 0, sizeof(iccid));
    return mp_obj_new_bool(SIM_GetICCID((uint8_t*)iccid));
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_is_sim_present_obj, modcellular_is_sim_present);

STATIC mp_obj_t modcellular_network_status_changed(void) {
    // ========================================
    // Checks whether the network status was updated.
    // Returns:
    //     True if it was updated since the last check.
    // ========================================
    uint8_t result = network_status_updated;
    network_status_updated = 0;
    return mp_obj_new_bool(result);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_network_status_changed_obj, modcellular_network_status_changed);

STATIC mp_obj_t modcellular_poll_network_exception(void) {
    // ========================================
    // Checks whether network exception occurred since the last check.
    // Returns:
    //    An integer representing network exception.
    // ========================================
    uint8_t e = network_exception;
    network_exception = NTW_NO_EXC;

    switch (e) {

        case NTW_EXC_NOSIM:
            mp_raise_CellularError("No SIM card inserted");
            break;

        case NTW_EXC_REG_DENIED:
            mp_raise_CellularError("Failed to register on the cellular network");
            break;

        case NTW_EXC_SMS_SEND:
            mp_raise_CellularError("SMS was not sent");
            break;

        case NTW_EXC_SIM_DROP:
            mp_raise_CellularError("SIM card dropped");
            break;

        case NTW_EXC_ATT_FAILED:
            mp_raise_CellularError("Failed to attach to the cellular network");
            break;

        case NTW_EXC_ACT_FAILED:
            mp_raise_CellularError("Failed to activate the cellular network");
            break;

        case NTW_EXC_SMS_DROP:
            mp_raise_CellularError("SMS message was discarded due to a timeout or a wrong SMS storage information");
            break;

        case NTW_NO_EXC:
            break;

        default:
            mp_raise_msg(&mp_type_RuntimeError, "Unknown network exception occurred");
            break;

    }
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_poll_network_exception_obj, modcellular_poll_network_exception);

STATIC mp_obj_t modcellular_get_network_status(void) {
    // ========================================
    // Retrieves the network status.
    // Returns:
    //     Network status as an integer.
    // ========================================
    return mp_obj_new_int(network_status);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_get_network_status_obj, modcellular_get_network_status);

STATIC mp_obj_t modcellular_is_network_registered(void) {
    // ========================================
    // Checks whether registered on the cellular network.
    // Returns:
    //     True if registered.
    // ========================================
    return mp_obj_new_bool(network_status & NTW_REG_BIT);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_is_network_registered_obj, modcellular_is_network_registered);

STATIC mp_obj_t modcellular_is_roaming(void) {
    // ========================================
    // Checks whether registered on the roaming network.
    // Returns:
    //     True if roaming.
    // ========================================
    REQUIRES_NETWORK_REGISTRATION;

    return mp_obj_new_bool(network_status & NTW_ROAM_BIT);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_is_roaming_obj, modcellular_is_roaming);

STATIC mp_obj_t modcellular_get_iccid(void) {
    // ========================================
    // Retrieves ICCID number.
    // Returns:
    //     ICCID number as a string.
    // ========================================
    char iccid[21];
    memset(iccid, 0, sizeof(iccid));
    if (SIM_GetICCID((uint8_t*)iccid))
        return mp_obj_new_str(iccid, strlen(iccid));
    else {
        mp_raise_CellularError("No ICCID data available");
        return mp_const_none;
    }
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_get_iccid_obj, modcellular_get_iccid);

STATIC mp_obj_t modcellular_get_imsi(void) {
    // ========================================
    // Retrieves IMSI number.
    // Returns:
    //     IMSI number as a string.
    // ========================================
    char imsi[21];
    memset(imsi, 0, sizeof(imsi));
    if (SIM_GetIMSI((uint8_t*)imsi))
        return mp_obj_new_str(imsi, strlen(imsi));
    else {
        mp_raise_CellularError("No IMSI data available");
        return mp_const_none;
    }
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_get_imsi_obj, modcellular_get_imsi);

bool get_flight_mode(void) {
    // Polls flight mode
    bool flag;
    if (!Network_GetFlightMode(&flag)) {
        mp_raise_CellularError("Failed to retrieve flight mode status");
    }
    return !flag;  // By fact, the meaning of the output is inverse
}

STATIC mp_obj_t modcellular_flight_mode(size_t n_args, const mp_obj_t *args) {
    // ========================================
    // Retrieves and switches the flight mode
    // status.
    // Returns:
    //     The new flight mode status.
    // ========================================
    if (n_args == 1) {
        mp_int_t set_flag = mp_obj_get_int(args[0]);
        if (!Network_SetFlightMode(set_flag)) {
            mp_raise_CellularError("Failed to set flight mode status");
            return mp_const_none;
        }
        WAIT_UNTIL(set_flag == get_flight_mode(), TIMEOUT_FLIGHT_MODE, 100, mp_raise_CellularError("Flight mode change timeout"));
    }
    return mp_obj_new_bool(get_flight_mode());
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(modcellular_flight_mode_obj, 0, 1, modcellular_flight_mode);

STATIC mp_obj_t modcellular_set_bands(size_t n_args, const mp_obj_t *args) {
    // ========================================
    // Sets 2G bands the module operates at.
    // Args:
    //     bands (int): a mask specifying
    //     bands;
    // ========================================
    if (n_args == 0) {
        if (!Network_SetFrequencyBand(NETWORK_FREQ_BAND_GSM_900P | NETWORK_FREQ_BAND_GSM_900E | NETWORK_FREQ_BAND_GSM_850 | NETWORK_FREQ_BAND_DCS_1800 | NETWORK_FREQ_BAND_PCS_1900)) {
            mp_raise_CellularError("Failed to reset 2G GSM bands");
            return mp_const_none;
        }
    } else if (n_args == 1) {
        if (!Network_SetFrequencyBand(mp_obj_get_int(args[0]))) {
            mp_raise_CellularError("Failed to set 2G GSM bands");
            return mp_const_none;
        }
    }
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(modcellular_set_bands_obj, 0, 1, modcellular_set_bands);

STATIC mp_obj_t modcellular_gprs(size_t n_args, const mp_obj_t *args) {
    // ========================================
    // Polls and switches GPRS status.
    // Args:
    //     apn (str, bool): access point name
    //     or False if GPRS shutdown requested.
    //     user (str): username;
    //     pass (str): password;
    // Returns:
    //     True if GPRS is active, False
    //     otherwise.
    // ========================================
    REQUIRES_NETWORK_REGISTRATION;

    if (n_args == 1) {
        mp_int_t flag = mp_obj_get_int(args[0]);
        if (flag != 0) {
            mp_raise_ValueError("Unkown integer argument supplied, zero (or False) expected");
            return mp_const_none;
        }

        if (network_status & NTW_ACT_BIT) {
            if (!Network_StartDeactive(1)) {
                mp_raise_CellularError("Cannot initiate context deactivation");
                return mp_const_none;
            }
            WAIT_UNTIL(!(network_status & NTW_ACT_BIT), TIMEOUT_GPRS_ACTIVATION, 100, mp_raise_CellularError("Network context deactivation timeout"));
        }

        if (network_status & NTW_ATT_BIT) {
            if (!Network_StartDetach()) {
                mp_raise_CellularError("Cannot initiate detachment");
                return mp_const_none;
            }
            WAIT_UNTIL(!(network_status & NTW_ATT_BIT), TIMEOUT_GPRS_ATTACHMENT, 100, mp_raise_CellularError("Network detach timeout"));
        }

    } else if (n_args == 3) {
        const char* c_apn = mp_obj_str_get_str(args[0]);
        const char* c_user = mp_obj_str_get_str(args[1]);
        const char* c_pass = mp_obj_str_get_str(args[2]);

        if (network_status & NTW_ACT_BIT) {
            mp_raise_CellularError("GPRS is already on");
            return mp_const_none;
        }

        if (!(network_status & NTW_ATT_BIT)) {
            if (!Network_StartAttach()) {
                mp_raise_CellularError("Cannot initiate attachment");
                return mp_const_none;
            }
            WAIT_UNTIL(network_status & NTW_ATT_BIT, TIMEOUT_GPRS_ATTACHMENT, 100, mp_raise_CellularError("Network attach timeout"));
        }

        if (!(network_status & NTW_ACT_BIT)) {
            Network_PDP_Context_t context;
            memcpy(context.apn, c_apn, MIN(strlen(c_apn) + 1, sizeof(context.apn)));
            memcpy(context.userName, c_user, MIN(strlen(c_user) + 1, sizeof(context.userName)));
            memcpy(context.userPasswd, c_pass, MIN(strlen(c_pass) + 1, sizeof(context.userPasswd)));

            if (!Network_StartActive(context)) {
                mp_raise_CellularError("Cannot initiate context activation");
                return mp_const_none;
            }
            WAIT_UNTIL(network_status & NTW_ACT_BIT, TIMEOUT_GPRS_ACTIVATION, 100, mp_raise_CellularError("Network context activation timeout"));
        }

    } else if (n_args != 0) {
        mp_raise_ValueError("Unexpected number of argument: 0, 1 or 3 required");
    }

    return mp_obj_new_bool(network_status & NTW_ACT_BIT);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(modcellular_gprs_obj, 0, 3, modcellular_gprs);

STATIC mp_obj_t modcellular_list_operators(void) {
    // ========================================
    // Lists network operators.
    // ========================================
    network_list_buffer = NULL;
    if (!Network_GetAvailableOperatorReq()) {
        mp_raise_CellularError("Failed to poll available operators");
        return mp_const_none;
    }
    WAIT_UNTIL(network_list_buffer != NULL, TIMEOUT_LIST_OPERATORS, 100, mp_raise_CellularError("Network list timeout"));

    mp_obj_t items[network_list_buffer_len];
    for (int i=0; i < network_list_buffer_len; i++) {

        // Name
        uint8_t *op_name;
        if (!Network_GetOperatorNameById(network_list_buffer[i].operatorId, &op_name)) {
            mp_raise_CellularError("Failed to poll operator name");
            return mp_const_none;
        }

        mp_obj_t tuple[3] = {
            mp_obj_new_bytearray(sizeof(network_list_buffer[i].operatorId), network_list_buffer[i].operatorId),
            mp_obj_new_int(network_list_buffer[i].status),
            mp_obj_new_str((char*) op_name, strlen((char*) op_name)),
        };
        items[i] = mp_obj_new_tuple(sizeof(tuple) / sizeof(mp_obj_t), tuple);
    }

    free(network_list_buffer);

    return mp_obj_new_list(sizeof(items) / sizeof(mp_obj_t), items);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_list_operators_obj, modcellular_list_operators);

STATIC mp_obj_t modcellular_reset(void) {
    // ========================================
    // Resets network settings to defaults.
    // ========================================
    mp_obj_t mp_false = mp_obj_new_int(0);
    network_exception = NTW_NO_EXC;
    modcellular_gprs(1, &mp_false);
    modcellular_set_bands(0, NULL);
    modcellular_flight_mode(1, &mp_false);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(modcellular_reset_obj, modcellular_reset);

STATIC const mp_map_elem_t mp_module_cellular_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_cellular) },

    { MP_OBJ_NEW_QSTR(MP_QSTR_CellularError), (mp_obj_t)MP_ROM_PTR(&mp_type_CellularError) },

    { MP_OBJ_NEW_QSTR(MP_QSTR_SMS), (mp_obj_t)MP_ROM_PTR(&modcellular_sms_type) },

    { MP_OBJ_NEW_QSTR(MP_QSTR_get_imei), (mp_obj_t)&modcellular_get_imei_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_signal_quality), (mp_obj_t)&modcellular_get_signal_quality_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_network_status_changed), (mp_obj_t)&modcellular_network_status_changed_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_poll_network_exception), (mp_obj_t)&modcellular_poll_network_exception_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_network_status), (mp_obj_t)&modcellular_get_network_status_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_is_sim_present), (mp_obj_t)&modcellular_is_sim_present_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_is_network_registered), (mp_obj_t)&modcellular_is_network_registered_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_is_roaming), (mp_obj_t)&modcellular_is_roaming_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_iccid), (mp_obj_t)&modcellular_get_iccid_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_imsi), (mp_obj_t)&modcellular_get_imsi_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_flight_mode), (mp_obj_t)&modcellular_flight_mode_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_bands), (mp_obj_t)&modcellular_set_bands_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_gprs), (mp_obj_t)&modcellular_gprs_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_list_operators), (mp_obj_t)&modcellular_list_operators_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_reset), (mp_obj_t)&modcellular_reset_obj },

    { MP_ROM_QSTR(MP_QSTR_NETWORK_FREQ_BAND_GSM_900P), MP_ROM_INT(NETWORK_FREQ_BAND_GSM_900P) },
    { MP_ROM_QSTR(MP_QSTR_NETWORK_FREQ_BAND_GSM_900E), MP_ROM_INT(NETWORK_FREQ_BAND_GSM_900E) },
    { MP_ROM_QSTR(MP_QSTR_NETWORK_FREQ_BAND_GSM_850),  MP_ROM_INT(NETWORK_FREQ_BAND_GSM_850)  },
    { MP_ROM_QSTR(MP_QSTR_NETWORK_FREQ_BAND_DCS_1800), MP_ROM_INT(NETWORK_FREQ_BAND_DCS_1800) },
    { MP_ROM_QSTR(MP_QSTR_NETWORK_FREQ_BAND_PCS_1900), MP_ROM_INT(NETWORK_FREQ_BAND_PCS_1900) },

    { MP_ROM_QSTR(MP_QSTR_OPERATOR_STATUS_UNKNOWN), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_OPERATOR_STATUS_AVAILABLE), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_OPERATOR_STATUS_CURRENT), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_OPERATOR_STATUS_DISABLED), MP_ROM_INT(3) },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_cellular_globals, mp_module_cellular_globals_table);

const mp_obj_module_t cellular_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_cellular_globals,
};
