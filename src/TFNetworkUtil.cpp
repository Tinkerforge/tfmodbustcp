/* TFModbusTCP
 * Copyright (C) 2024 Matthias Bolte <matthias@tinkerforge.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "TFNetworkUtil.h"

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>

static void logln_dummy(const char *message)
{
    (void)message;
}

static TFNetworkUtilLoglnCallback logln_callback = logln_dummy;

void TFNetworkUtil::set_logln_callback(TFNetworkUtilLoglnCallback &&callback)
{
    if (callback) {
        logln_callback = std::move(callback);
    }
    else {
        logln_callback = logln_dummy;
    }
}

void TFNetworkUtil::logfln(const char *fmt, ...)
{
    char buffer[512];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    logln_callback(buffer);
}

static int64_t microseconds_dummy()
{
    return 0;
}

static TFNetworkUtilMicrosecondsCallback microseconds_callback = microseconds_dummy;

static void resolve_dummy(const char *host_name, TFNetworkUtilResolveResultCallback &&callback)
{
    (void)host_name;

    callback(0, ENOSYS);
}

static TFNetworkUtilResolveCallback resolve_callback = resolve_dummy;

void TFNetworkUtil::set_microseconds_callback(std::function<int64_t(void)> &&callback)
{
    if (callback) {
        microseconds_callback = std::move(callback);
    }
    else {
        microseconds_callback = microseconds_dummy;
    }
}

int64_t TFNetworkUtil::microseconds()
{
    return microseconds_callback();
}

bool TFNetworkUtil::deadline_elapsed(int64_t deadline_us)
{
    return deadline_us < microseconds();
}

int64_t TFNetworkUtil::calculate_deadline(int64_t delay_us)
{
    return microseconds() + delay_us;
}

void TFNetworkUtil::set_resolve_callback(TFNetworkUtilResolveCallback &&callback)
{
    if (callback) {
        resolve_callback = std::move(callback);
    }
    else {
        resolve_callback = resolve_dummy;
    }
}

void TFNetworkUtil::resolve(const char *host_name, TFNetworkUtilResolveResultCallback &&callback)
{
    resolve_callback(host_name, std::move(callback));
}
