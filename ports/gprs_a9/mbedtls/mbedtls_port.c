/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Damien P. George
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
#include <py/mpconfig.h>
#include <time.h>
#include <stdint.h>
#include <stddef.h>

#define SECONDSPERDAY (24*60*60)
#define SECONDSPERHOUR (60*60)
#define DIFFDAYS (3 * DAYSPER100YEARS + 17 * DAYSPER4YEARS + 1 * DAYSPERYEAR)
#define DAYSPERYEAR 365
#define DAYSPER4YEARS (4*DAYSPERYEAR+1)
#define DAYSPER100YEARS (25*DAYSPER4YEARS-1)
#define DAYSPER400YEARS (4*DAYSPER100YEARS+1)
#define LEAPDAY 59

unsigned int g_monthdays[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};
unsigned int g_lpmonthdays[13] = {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366};
  
static long dst_begin = 0;
static long dst_end = 0;
static long _dstbias = 0;

typedef uint64_t __time64_t;

static inline long leapyears_passed(long days)
{
    long quadcenturies, centuries, quadyears;
    quadcenturies = days / DAYSPER400YEARS;
    days -= quadcenturies;
    centuries = days / DAYSPER100YEARS;
    days += centuries;
    quadyears = days / DAYSPER4YEARS;
    return quadyears - centuries + quadcenturies;
}
 
static inline long leapdays_passed(long days)
{
    return leapyears_passed(days + DAYSPERYEAR - LEAPDAY + 1);
}
 
static inline long years_passed(long days)
{
    return (days - leapdays_passed(days)) / 365;
}

struct tm *
_gmtime_worker(struct tm *ptm, __time64_t time, int do_dst)
{
    unsigned int days, daystoyear, dayinyear, leapdays, leapyears, years, month;
    unsigned int secondinday, secondinhour;
    unsigned int *padays;
 
    if (time < 0)
    {
        return 0;
    }
 
    /* Divide into date and time */
    days = (unsigned int)(time / SECONDSPERDAY);
    secondinday = time % SECONDSPERDAY;
 
    /* Shift to days from 1.1.1601 */
    days += DIFFDAYS;
 
    /* Calculate leap days passed till today */
    leapdays = leapdays_passed(days);
 
    /* Calculate number of full leap years passed */
    leapyears = leapyears_passed(days);
 
    /* Are more leap days passed than leap years? */
    if (leapdays > leapyears)
    {
        /* Yes, we're in a leap year */
        padays = g_lpmonthdays;
    }
    else
    {
        /* No, normal year */
        padays = g_monthdays;
    }
 
    /* Calculate year */
    years = (days - leapdays) / 365;
    ptm->tm_year = years - 299;
 
    /* Calculate number of days till 1.1. of this year */
    daystoyear = years * 365 + leapyears;
 
    /* Calculate the day in this year */
    dayinyear = days - daystoyear;
 
    /* Shall we do DST corrections? */
    ptm->tm_isdst = 0;
    if (do_dst)
    {
        int yeartime = dayinyear * SECONDSPERDAY + secondinday ;
        if (yeartime >= dst_begin && yeartime <= dst_end) // FIXME! DST in winter
        {
            time -= _dstbias;
            days = (unsigned int)(time / SECONDSPERDAY + DIFFDAYS);
            dayinyear = days - daystoyear;
            ptm->tm_isdst = 1;
        }
    }
 
    ptm->tm_yday = dayinyear;
 
    /* dayinyear < 366 => terminates with i <= 11 */
    for (month = 0; dayinyear >= padays[month+1]; month++)
        ;
 
    /* Set month and day in month */
    ptm->tm_mon = month;
    ptm->tm_mday = 1 + dayinyear - padays[month];
 
    /* Get weekday */
    ptm->tm_wday = (days + 1) % 7;
 
    /* Calculate hour and second in hour */
    ptm->tm_hour = secondinday / SECONDSPERHOUR;
    secondinhour = secondinday % SECONDSPERHOUR;
 
    /* Calculate minute and second */
    ptm->tm_min = secondinhour / 60;
    ptm->tm_sec = secondinhour % 60;
 
    return ptm;
}

struct tm *
_gmtime64(const __time64_t * ptime)
{
    /* Validate parameters */
    if (!ptime || *ptime < 0)
    {
        return NULL;
    }

    static struct tm time_buffer;
    return _gmtime_worker(&time_buffer, *ptime, 0);
}

struct tm *
gmtime(const time_t * ptime)
{
    __time64_t time64;
 
    if (!ptime) {
        return NULL;
    }

    time64 = *ptime;
    return _gmtime64(&time64);
}

#ifdef MICROPY_SSL_MBEDTLS

#include "mbedtls_config.h"

#include "shared/timeutils/timeutils.h"

extern uint8_t rosc_random_u8(size_t cycles);

#define POLY (0x2d0000)

static bool bit(uint8_t *d, uint8_t b)
{
    return (d[b >> 3] & (1 << (b & 7))) == (1 << (b & 7));
}

static uint32_t rng_val = 0x12345678;

static uint8_t rnd_u8(size_t cycles)
{
    while (cycles) {
        uint32_t x = 1;
        for (uint8_t i = 0; i < 32; i++)
        {
            if (POLY & (1 << i))
            {
                x ^= bit((uint8_t *)&rng_val, i);
            }
        }
        rng_val = rng_val >> 1;
        rng_val |= x << 31;
        cycles -= 1;
    }

    return rng_val;
}

static uint32_t rnd_u32(void)
{
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i)
    {
        value = value << 8 | rosc_random_u8(32);
    }
    return value;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    *olen = len;
    for (size_t i = 0; i < len; i++)
    {
        output[i] = rnd_u8(8);
    }
    return 0;
}


#endif
