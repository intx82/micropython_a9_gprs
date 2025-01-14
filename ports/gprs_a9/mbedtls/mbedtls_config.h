/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018-2019 Damien P. George
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
#ifndef MICROPY_INCLUDED_MBEDTLS_CONFIG_H
#define MICROPY_INCLUDED_MBEDTLS_CONFIG_H

// Set mbedtls configuration
#define MBEDTLS_CIPHER_MODE_CTR // needed for MICROPY_PY_UCRYPTOLIB_CTR

// Set mbedtls configuration
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED

// Enable mbedtls modules
#define MBEDTLS_GCM_C
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE

// Time hook
#include <time.h>

#define MBEDTLS_PLATFORM_TIME_MACRO TIME_GetTime

#undef MBEDTLS_FS_IO
#undef MBEDTLS_HAVE_ASM
#undef MBEDTLS_AESNI_C

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t) -1)
#endif

#ifndef UINT64_MAX
#define UINT64_MAX ((uint64_t) -1)
#endif



// Include common mbedtls configuration.
#include "extmod/mbedtls/mbedtls_config_common.h"

#endif /* MICROPY_INCLUDED_MBEDTLS_CONFIG_H */
