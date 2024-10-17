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

#pragma once

#include <stdint.h>
#include <functional>

typedef std::function<void(const char *message)> TFNetworkUtilLoglnCallback;
typedef std::function<int64_t(void)> TFNetworkUtilMicrosecondsCallback;
typedef std::function<void(uint32_t host_address, int error_number)> TFNetworkUtilResolveResultCallback;
typedef std::function<void(const char *host_name, TFNetworkUtilResolveResultCallback &&callback)> TFNetworkUtilResolveCallback;

#if TF_NETWORK_UTIL_DEBUG_LOG
#define tf_network_util_debugfln(fmt, ...) TFNetworkUtil::logfln(fmt, __VA_ARGS__)
#else
#define tf_network_util_debugfln(fmt, ...) do {} while (0)
#endif

class TFNetworkUtil
{
public:
    static void set_logln_callback(TFNetworkUtilLoglnCallback &&callback);
    [[gnu::format(__printf__, 1, 2)]] static void logfln(const char *fmt, ...);

    static void set_microseconds_callback(TFNetworkUtilMicrosecondsCallback &&callback);
    static int64_t microseconds();

    static bool deadline_elapsed(int64_t deadline_us);
    static int64_t calculate_deadline(int64_t delay_us);

    static void set_resolve_callback(TFNetworkUtilResolveCallback &&callback);
    static void resolve(const char *host_name, TFNetworkUtilResolveResultCallback &&callback);

    static const char *printf_safe(const char *string);
};
