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

#include "TFGenericTCPClientPool.h"

#include "TFNetworkUtil.h"

void TFGenericTCPClientPool::acquire(const char *host_name, uint16_t port,
                                     TFGenericTCPClientPoolConnectCallback &&connect_callback,
                                     TFGenericTCPClientPoolDisconnectCallback &&disconnect_callback)
{
    ssize_t slot_index = -1;

    tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(host_name=%s port=%u)", (void *)this, host_name, port);

    for (size_t i = 0; i < TF_GENERIC_TCP_CLIENT_POOL_MAX_SLOT_COUNT; ++i) {
        TFGenericTCPClientPoolSlot *slot = slots[i];

        if (slot == nullptr) {
            if (slot_index < 0) {
                slot_index = i;
            }

            continue;
        }

        const char *slot_host_name = slot->client->get_host_name();

        if (slot_host_name == nullptr) {
            continue;
        }

        tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(host_name=%s port=%u) checking existing client (slot=%zu client=%p host_name=%s port=%u)",
                                 (void *)this, host_name, port, i, slot->client, slot_host_name, slot->client->get_port());

        if (strcmp(slot_host_name, host_name) == 0 && slot->client->get_port() == port) {
            ssize_t handle_index = -1;

            tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(host_name=%s port=%u) found matching existing client (slot=%zu client=%p)",
                                     (void *)this, host_name, port, i, slot->client);

            for (size_t k = 0; k < TF_GENERIC_TCP_CLIENT_POOL_MAX_HANDLE_COUNT; ++k) {
                if (slot->handles[k] == nullptr) {
                    handle_index = k;
                    break;
                }
            }

            if (handle_index < 0) {
                connect_callback(TFGenericTCPClientConnectResult::NoFreePoolHandle, -1, nullptr);
                return;
            }

            TFGenericTCPClientPoolHandle *handle = new TFGenericTCPClientPoolHandle;
            handle->client = slot->client;

            if (slot->client->get_connection_status() == TFGenericTCPClientConnectionStatus::Connected) {
                handle->disconnect_callback = disconnect_callback;
                connect_callback(TFGenericTCPClientConnectResult::Connected, -1, handle);
            }
            else {
                handle->connect_callback = connect_callback;
                handle->pending_disconnect_callback = disconnect_callback;
            }

            slot->handles[handle_index] = handle;
            return;
        }
    }

    if (slot_index < 0) {
        connect_callback(TFGenericTCPClientConnectResult::NoFreePoolSlot, -1, nullptr);
        return;
    }

    uint32_t slot_id = next_slot_id++;
    TFGenericTCPClientPoolSlot *slot = new TFGenericTCPClientPoolSlot;
    slot->id = slot_id;
    slot->client = new_client();
    slots[slot_index] = slot;

    tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(host_name=%s port=%u) creating new client (slot=%zu client=%p)",
                             (void *)this, host_name, port, slot_index, slot->client);

    TFGenericTCPClientPoolHandle *handle = new TFGenericTCPClientPoolHandle;
    handle->client = slot->client;
    handle->connect_callback = connect_callback;
    handle->pending_disconnect_callback = disconnect_callback;
    slot->handles[0] = handle;

    slot->client->connect(host_name, port,
    [this, slot_index, slot_id](TFGenericTCPClientConnectResult result, int error_number) {
        TFGenericTCPClientPoolSlot *slot = slots[slot_index];

        if (slot == nullptr || slot->id != slot_id) {
            return; // slot got freed or reused
        }

        for (size_t i = 0; i < TF_GENERIC_TCP_CLIENT_POOL_MAX_HANDLE_COUNT; ++i) {
            TFGenericTCPClientPoolHandle *handle = slot->handles[i];

            if (handle == nullptr) {
                continue;
            }

            TFGenericTCPClientPoolConnectCallback connect_callback = std::move(handle->connect_callback);
            handle->connect_callback = nullptr;

            if (result == TFGenericTCPClientConnectResult::Connected) {
                handle->disconnect_callback = std::move(handle->pending_disconnect_callback);
            }

            handle->pending_disconnect_callback = nullptr;

            if (connect_callback) {
                connect_callback(result, error_number, result == TFGenericTCPClientConnectResult::Connected ? handle : nullptr);
            }

            if (result != TFGenericTCPClientConnectResult::Connected) {
                handle->pending_release = true;
            }
        }
    },
    [this, slot_index, slot_id](TFGenericTCPClientDisconnectReason reason, int error_number) {
        TFGenericTCPClientPoolSlot *slot = slots[slot_index];

        if (slot == nullptr || slot->id != slot_id) {
            return; // slot got freed or reused
        }

        for (size_t i = 0; i < TF_GENERIC_TCP_CLIENT_POOL_MAX_HANDLE_COUNT; ++i) {
            TFGenericTCPClientPoolHandle *handle = slot->handles[i];

            if (handle == nullptr) {
                continue;
            }

            TFGenericTCPClientPoolDisconnectCallback disconnect_callback = std::move(handle->disconnect_callback);
            handle->disconnect_callback = nullptr;

            if (disconnect_callback) {
                disconnect_callback(reason, error_number, handle);
            }

            handle->pending_release = true;
        }
    });
}

void TFGenericTCPClientPool::release(TFGenericTCPClientPoolHandle *handle)
{
    handle->pending_release = true;
}

void TFGenericTCPClientPool::tick()
{
    for (size_t i = 0; i < TF_GENERIC_TCP_CLIENT_POOL_MAX_SLOT_COUNT; ++i) {
        TFGenericTCPClientPoolSlot *slot = slots[i];

        if (slot == nullptr) {
            continue;
        }

        for (size_t k = 0; k < TF_GENERIC_TCP_CLIENT_POOL_MAX_HANDLE_COUNT; ++k) {
            TFGenericTCPClientPoolHandle *handle = slot->handles[k];

            if (handle == nullptr || !handle->pending_release) {
                continue;
            }

            slot->handles[k] = nullptr;

            TFGenericTCPClientPoolDisconnectCallback disconnect_callback = std::move(handle->disconnect_callback);
            handle->disconnect_callback = nullptr;

            if (disconnect_callback) {
                disconnect_callback(TFGenericTCPClientDisconnectReason::Requested, -1, handle);
            }

            delete handle;
        }

        bool slot_active = false;

        for (size_t k = 0; k < TF_GENERIC_TCP_CLIENT_POOL_MAX_HANDLE_COUNT; ++k) {
            TFGenericTCPClientPoolHandle *handle = slot->handles[k];

            if (handle != nullptr) {
                slot_active = true;
                break;
            }
        }

        if (slot_active) {
            slot->client->tick();
        }
        else {
            const char *host_name = slot->client->get_host_name();

            tf_network_util_debugfln("TFGenericTCPClientPool[%p]::tick() deleting inactive client (slot=%zu client=%p host_name=%s port=%u)",
                                     (void *)this, i, slot->client, host_name != nullptr ? host_name : "[nullptr]", slot->client->get_port());

            slots[i] = nullptr;
            slot->client->disconnect();
            delete slot->client;
            delete slot;
        }
    }
}
