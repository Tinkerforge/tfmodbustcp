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

#include <vector>

#include "TFModbusTCPClient.h"

#define TF_MODBUS_TCP_CLIENT_POOL_MAX_HANDLE_COUNT 8
#define TF_MODBUS_TCP_CLIENT_POOL_MAX_SLOT_COUNT 8

struct TFModbusTCPClientPoolHandle;

typedef std::function<void(TFGenericTCPClientConnectResult result, int error_number, TFModbusTCPClientPoolHandle *handle)> TFGenericTCPClientPoolConnectCallback;
typedef std::function<void(TFGenericTCPClientDisconnectReason reason, int error_number, TFModbusTCPClientPoolHandle *handle)> TFGenericTCPClientPoolDisconnectCallback;

struct TFModbusTCPClientPoolHandle
{
    bool pending_release = false;
    TFModbusTCPClient *client;
    TFGenericTCPClientPoolConnectCallback connect_callback;
    TFGenericTCPClientPoolDisconnectCallback pending_disconnect_callback;
    TFGenericTCPClientPoolDisconnectCallback disconnect_callback;
};

struct TFModbusTCPClientPoolSlot
{
    TFModbusTCPClientPoolSlot() { memset(handles, 0, sizeof(handles)); }

    uint32_t id;
    TFModbusTCPClient client;
    TFModbusTCPClientPoolHandle *handles[TF_MODBUS_TCP_CLIENT_POOL_MAX_HANDLE_COUNT];
};

class TFModbusTCPClientPool
{
public:
    TFModbusTCPClientPool() { memset(slots, 0, sizeof(slots)); }

    void acquire(const char *host_name, uint16_t port,
                 TFGenericTCPClientPoolConnectCallback &&connect_callback,
                 TFGenericTCPClientPoolDisconnectCallback &&disconnect_callback);
    void release(TFModbusTCPClientPoolHandle *handle);
    void tick();

private:
    uint32_t next_slot_id = 0;
    TFModbusTCPClientPoolSlot *slots[TF_MODBUS_TCP_CLIENT_POOL_MAX_SLOT_COUNT];
};
