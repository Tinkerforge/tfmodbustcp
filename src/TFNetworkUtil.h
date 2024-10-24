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
#include <stdarg.h>
#include <functional>

#if TF_NETWORK_UTIL_DEBUG_LOG
#define tf_network_util_debugfln(fmt, ...) TFNetworkUtil::logfln(fmt __VA_OPT__(,) __VA_ARGS__)
#else
#define tf_network_util_debugfln(fmt, ...) do {} while (0)
#endif

typedef std::function<void(const char *fmt, va_list args)> TFNetworkUtilVLogFLnFunction;
typedef std::function<int64_t(void)> TFNetworkUtilMicrosecondsFunction;
typedef std::function<void(uint32_t host_address, int error_number)> TFNetworkUtilResolveResultCallback;
typedef std::function<void(const char *host_name, TFNetworkUtilResolveResultCallback &&callback)> TFNetworkUtilResolveFunction;

namespace TFNetworkUtil
{
    const char *printf_safe(const char *string);

    extern TFNetworkUtilVLogFLnFunction vlogfln;
    [[gnu::format(__printf__, 1, 2)]] void logfln(const char *fmt, ...);

    extern TFNetworkUtilMicrosecondsFunction microseconds;
    bool deadline_elapsed(int64_t deadline_us);
    int64_t calculate_deadline(int64_t delay_us);

    extern TFNetworkUtilResolveFunction resolve;

    class NonReentrantScope
    {
    public:
        NonReentrantScope(bool *non_reentrant_) : non_reentrant(non_reentrant_) { *non_reentrant = true; }
        ~NonReentrantScope() { *non_reentrant = false; }

    private:
        bool *non_reentrant;
    };
};
