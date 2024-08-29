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

static uint32_t milliseconds_dummy()
{
    return 0;
}

static TFNetworkUtilMillisecondsCallback milliseconds_callback = milliseconds_dummy;

static void resolve_dummy(const char *host_name, TFNetworkUtilResolveResultCallback &&callback)
{
    callback(0, ENOSYS);
}

static TFNetworkUtilResolveCallback resolve_callback = resolve_dummy;

static bool a_after_b(uint32_t a, uint32_t b)
{
    return ((uint32_t)(a - b)) < (UINT32_MAX / 2);
}

void TFNetworkUtil::set_milliseconds_callback(std::function<uint32_t(void)> &&callback)
{
    if (callback) {
        milliseconds_callback = callback;
    }
    else {
        milliseconds_callback = milliseconds_dummy;
    }
}

uint32_t TFNetworkUtil::milliseconds()
{
    return milliseconds_callback();
}

bool TFNetworkUtil::deadline_elapsed(uint32_t deadline)
{
    return a_after_b(milliseconds(), deadline);
}

uint32_t TFNetworkUtil::calculate_deadline(uint32_t delay)
{
    uint32_t deadline = milliseconds() + delay;

    if (deadline == 0) {
        deadline = 1;
    }

    return deadline;
}

void TFNetworkUtil::set_resolve_callback(TFNetworkUtilResolveCallback &&callback)
{
    if (callback) {
        resolve_callback = callback;
    }
    else {
        resolve_callback = resolve_dummy;
    }
}

void TFNetworkUtil::resolve(const char *host_name, TFNetworkUtilResolveResultCallback &&callback)
{
    resolve_callback(host_name, std::forward<TFNetworkUtilResolveResultCallback>(callback));
}
