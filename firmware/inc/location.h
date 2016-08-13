/*
 * Location related things
 * Copyright (C) 2015  Richard Meadows <richardeoin>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef LOCATION_H
#define LOCATION_H

#include <stdbool.h>
#include "samd20.h"


/* Update */
void location_telemetry_update(int32_t lon_hn, int32_t lat_hn);
void location_aprs_update(int32_t lon_hn, int32_t lat_hn);
void location_prefix_update(int32_t lon_hn, int32_t lat_hn);


/* Decisions */
bool location_telemetry_active(void);
bool location_aprs_could_tx(void);
bool location_aprs_should_tx(void);
int32_t location_aprs_frequency(void);
char* location_aprs_call(void);
int32_t location_aprs_deviation(void);
char* location_prefix(void);

#endif /* LOCATION_H */
