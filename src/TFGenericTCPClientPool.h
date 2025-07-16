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

#include "TFGenericTCPClient.h"

#ifndef TF_GENERIC_TCP_CLIENT_POOL_MAX_SLOT_COUNT
#define TF_GENERIC_TCP_CLIENT_POOL_MAX_SLOT_COUNT 16
#endif

#ifndef TF_GENERIC_TCP_CLIENT_POOL_MAX_SHARE_COUNT
#define TF_GENERIC_TCP_CLIENT_POOL_MAX_SHARE_COUNT 16
#endif

enum class TFGenericTCPClientPoolShareLevel
{
    Undefined,
    Primary,
    Secondary,
};

const char *get_tf_generic_tcp_client_pool_share_level_name(TFGenericTCPClientPoolShareLevel level);

typedef std::function<void(TFGenericTCPClientConnectResult result, int error_number, TFGenericTCPSharedClient *shared_client, TFGenericTCPClientPoolShareLevel share_level)> TFGenericTCPClientPoolConnectCallback;
typedef std::function<void(TFGenericTCPClientDisconnectReason reason, int error_number, TFGenericTCPSharedClient *shared_client, TFGenericTCPClientPoolShareLevel share_level)> TFGenericTCPClientPoolDisconnectCallback;

struct TFGenericTCPClientPoolShare
{
    TFGenericTCPSharedClient *shared_client;
    TFGenericTCPClientPoolConnectCallback connect_callback;
    TFGenericTCPClientPoolDisconnectCallback pending_disconnect_callback;
    TFGenericTCPClientPoolDisconnectCallback disconnect_callback;
};

struct TFGenericTCPClientPoolSlot
{
    TFGenericTCPClientPoolSlot() { memset(shares, 0, sizeof(shares)); }

    uint32_t id = 0;
    bool delete_pending = false;
    TFGenericTCPClient *client = nullptr;
    TFGenericTCPClientPoolShare *shares[TF_GENERIC_TCP_CLIENT_POOL_MAX_SHARE_COUNT];
    size_t share_count = 0;
};

class TFGenericTCPClientPool
{
public:
    TFGenericTCPClientPool() { memset(slots, 0, sizeof(slots)); }
    virtual ~TFGenericTCPClientPool() {}

    TFGenericTCPClientPool(TFGenericTCPClientPool const &other) = delete;
    TFGenericTCPClientPool &operator=(TFGenericTCPClientPool const &other) = delete;

    void acquire(const char *host, uint16_t port,
                 TFGenericTCPClientPoolConnectCallback &&connect_callback,
                 TFGenericTCPClientPoolDisconnectCallback &&disconnect_callback); // non-reentrant
    void release(TFGenericTCPSharedClient *shared_client); // non-reentrant
    void tick(); // non-reentrant

protected:
    virtual TFGenericTCPClient *create_client() = 0;
    virtual TFGenericTCPSharedClient *create_shared_client(TFGenericTCPClient *client) = 0;

private:
    void release(size_t slot_index, size_t share_index, TFGenericTCPClientDisconnectReason reason, int error_number, bool disconnect);

    bool non_reentrant    = false;
    uint32_t next_slot_id = 1;
    TFGenericTCPClientPoolSlot *slots[TF_GENERIC_TCP_CLIENT_POOL_MAX_SLOT_COUNT];
};
