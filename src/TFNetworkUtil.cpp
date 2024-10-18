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

const char *TFNetworkUtil::printf_safe(const char *string)
{
    return string != nullptr ? string : "[nullptr]";
}

static void vlogfln_dummy(const char *fmt, va_list args)
{
    (void)fmt;
    (void)args;
}

TFNetworkUtilVLogFLnFunction TFNetworkUtil::vlogfln = vlogfln_dummy;

void TFNetworkUtil::logfln(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vlogfln(fmt, args);
    va_end(args);
}

static int64_t microseconds_dummy()
{
    abort();

    return 0;
}

TFNetworkUtilMicrosecondsFunction TFNetworkUtil::microseconds = microseconds_dummy;

bool TFNetworkUtil::deadline_elapsed(int64_t deadline_us)
{
    return deadline_us < microseconds();
}

int64_t TFNetworkUtil::calculate_deadline(int64_t delay_us)
{
    return microseconds() + delay_us;
}

static void resolve_dummy(const char *host_name, TFNetworkUtilResolveResultCallback &&callback)
{
    (void)host_name;

    callback(0, ENOSYS);
}

TFNetworkUtilResolveFunction TFNetworkUtil::resolve = resolve_dummy;
