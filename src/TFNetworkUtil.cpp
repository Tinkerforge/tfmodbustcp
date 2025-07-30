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
#include <lwip/sockets.h>

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

static void resolve_dummy(const char *host, TFNetworkUtilResolveResultCallback &&callback)
{
    (void)host;

    callback(0, ENOSYS);
}

TFNetworkUtilResolveFunction TFNetworkUtil::resolve = resolve_dummy;

char *TFNetworkUtil::ipv4_ntoa(char *buffer, size_t buffer_length, uint32_t address)
{
    if (buffer_length < 1) {
        return buffer;
    }

    struct in_addr addr;
    addr.s_addr = address;

    return const_cast<char *>(inet_ntop(AF_INET, &addr, buffer, buffer_length));
}
