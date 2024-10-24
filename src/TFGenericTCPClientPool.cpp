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

#include <sys/types.h>

// non-reentrant
void TFGenericTCPClientPool::acquire(const char *host_name, uint16_t port,
                                     TFGenericTCPClientPoolConnectCallback &&connect_callback,
                                     TFGenericTCPClientPoolDisconnectCallback &&disconnect_callback)
{
    if (non_reentrant) {
        tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(host_name=%s port=%u) non-reentrant",
                                 static_cast<void *>(this), TFNetworkUtil::printf_safe(host_name), port);
        connect_callback(TFGenericTCPClientConnectResult::NonReentrant, -1, nullptr);
        return;
    }

    TFNetworkUtil::NonReentrantScope scope(&non_reentrant);

    if (host_name == nullptr || strlen(host_name) == 0 || port == 0 || !connect_callback || !disconnect_callback) {
        tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(host_name=%s port=%u) invalid argument",
                                 static_cast<void *>(this), TFNetworkUtil::printf_safe(host_name), port);
        connect_callback(TFGenericTCPClientConnectResult::InvalidArgument, -1, nullptr);
        return;
    }

    tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(host_name=%s port=%u)", static_cast<void *>(this), host_name, port);

    ssize_t slot_index = -1;

    for (size_t i = 0; i < TF_GENERIC_TCP_CLIENT_POOL_MAX_SLOT_COUNT; ++i) {
        TFGenericTCPClientPoolSlot *slot = slots[i];

        if (slot == nullptr) {
            if (slot_index < 0) {
                slot_index = i;
            }

            continue;
        }

        if (slot->delete_pending) {
            if (slot_index < 0 || slots[slot_index] == nullptr) {
                slot_index = i;
            }

            continue;
        }

        tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(host_name=%s port=%u) checking existing slot (slot_index=%zu client=%p host_name=%s port=%u)",
                                 static_cast<void *>(this), host_name, port, i, static_cast<void *>(slot->client), slot->client->get_host_name(), slot->client->get_port());

        if (strcmp(slot->client->get_host_name(), host_name) == 0 && slot->client->get_port() == port) {
            ssize_t share_index = -1;

            tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(host_name=%s port=%u) found matching existing slot (slot_index=%zu client=%p)",
                                     static_cast<void *>(this), host_name, port, i, static_cast<void *>(slot->client));

            for (size_t k = 0; k < TF_GENERIC_TCP_CLIENT_POOL_MAX_SHARE_COUNT; ++k) {
                if (slot->shares[k] == nullptr) {
                    share_index = k;
                    break;
                }
            }

            if (share_index < 0) {
                connect_callback(TFGenericTCPClientConnectResult::NoFreePoolShare, -1, nullptr);
                return;
            }

            TFGenericTCPClientPoolShare *share = new TFGenericTCPClientPoolShare;
            share->shared_client = create_shared_client(slot->client);

            if (slot->client->get_connection_status() == TFGenericTCPClientConnectionStatus::Connected) {
                share->disconnect_callback = std::move(disconnect_callback);
                connect_callback(TFGenericTCPClientConnectResult::Connected, -1, share->shared_client);
            }
            else {
                share->connect_callback = std::move(connect_callback);
                share->pending_disconnect_callback = std::move(disconnect_callback);
            }

            slot->shares[share_index] = share;
            return;
        }
    }

    if (slot_index < 0) {
        connect_callback(TFGenericTCPClientConnectResult::NoFreePoolSlot, -1, nullptr);
        return;
    }

    if (slots[slot_index] == nullptr) {
        slots[slot_index] = new TFGenericTCPClientPoolSlot;
    }

    uint32_t slot_id = next_slot_id++;
    TFGenericTCPClientPoolSlot *slot = slots[slot_index];

    if (slot->delete_pending) {
        tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(host_name=%s port=%u) reviving slot (slot_index=%zu slot=%p old_slot_id=%u new_slot_id=%u client=%p)",
                                 static_cast<void *>(this), host_name, port, slot_index, static_cast<void *>(slot), slot->id, slot_id, static_cast<void *>(slot->client));
    }

    slot->id = slot_id;
    slot->delete_pending = false;

    if (slot->client == nullptr) {
        slot->client = create_client();
    }

    tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(host_name=%s port=%u) connecting slot (slot_index=%zu slot=%p slot_id=%u client=%p)",
                             static_cast<void *>(this), host_name, port, slot_index, static_cast<void *>(slot), slot_id, static_cast<void *>(slot->client));

    TFGenericTCPClientPoolShare *share = new TFGenericTCPClientPoolShare;
    share->shared_client = create_shared_client(slot->client);
    share->connect_callback = std::move(connect_callback);
    share->pending_disconnect_callback = std::move(disconnect_callback);
    slot->shares[0] = share;

    slot->client->connect(host_name, port,
    [this, slot_index, slot_id](TFGenericTCPClientConnectResult result, int error_number) {
        TFGenericTCPClientPoolSlot *slot = slots[slot_index];

        if (slot == nullptr || slot->id != slot_id) {
            tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(...) connected... slot got freed or reused (result=%s error_number=%d slot_index=%zu slot=%p slot->id=%u slot_id=%u)",
                                     static_cast<void *>(this), get_tf_generic_tcp_client_connect_result_name(result), error_number,
                                     slot_index, static_cast<void *>(slot), slot != nullptr ? slot->id : 0, slot_id);
            return;
        }

        tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(...) connected (result=%s error_number=%d slot_index=%zu slot=%p slot->id=%u slot_id=%u)",
                                 static_cast<void *>(this), get_tf_generic_tcp_client_connect_result_name(result), error_number,
                                 slot_index, static_cast<void *>(slot), slot != nullptr ? slot->id : 0, slot_id);

        for (size_t k = 0; k < TF_GENERIC_TCP_CLIENT_POOL_MAX_SHARE_COUNT; ++k) {
            TFGenericTCPClientPoolShare *share = slot->shares[k];

            if (share == nullptr) {
                continue;
            }

            TFGenericTCPClientPoolConnectCallback connect_callback = std::move(share->connect_callback);
            share->connect_callback = nullptr;

            if (result == TFGenericTCPClientConnectResult::Connected) {
                share->disconnect_callback = std::move(share->pending_disconnect_callback);
            }

            share->pending_disconnect_callback = nullptr;

            if (connect_callback) {
                connect_callback(result, error_number, result == TFGenericTCPClientConnectResult::Connected ? share->shared_client : nullptr);
            }

            if (result != TFGenericTCPClientConnectResult::Connected) {
                release(slot_index, k, false);
            }
        }
    },
    [this, slot_index, slot_id](TFGenericTCPClientDisconnectReason reason, int error_number) {
        TFGenericTCPClientPoolSlot *slot = slots[slot_index];

        if (slot == nullptr || slot->id != slot_id || slot->delete_pending) {
            if (slot->delete_pending) {
                return;
            }

            tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(...) disconnected... slot got freed or reused (reason=%s error_number=%d slot_index=%zu slot=%p slot->id=%u slot_id=%u)",
                                     static_cast<void *>(this), get_tf_generic_tcp_client_disconnect_reason_name(reason), error_number,
                                     slot_index, static_cast<void *>(slot), slot != nullptr ? slot->id : 0, slot_id);
            return;
        }

        tf_network_util_debugfln("TFGenericTCPClientPool[%p]::acquire(...) disconnected (reason=%s error_number=%d slot_index=%zu slot=%p slot->id=%u slot_id=%u)",
                                 static_cast<void *>(this), get_tf_generic_tcp_client_disconnect_reason_name(reason), error_number,
                                 slot_index, static_cast<void *>(slot), slot != nullptr ? slot->id : 0, slot_id);

        for (size_t k = 0; k < TF_GENERIC_TCP_CLIENT_POOL_MAX_SHARE_COUNT; ++k) {
            TFGenericTCPClientPoolShare *share = slot->shares[k];

            if (share == nullptr) {
                continue;
            }

            TFGenericTCPClientPoolDisconnectCallback disconnect_callback = std::move(share->disconnect_callback);
            share->disconnect_callback = nullptr;

            if (disconnect_callback) {
                disconnect_callback(reason, error_number, share->shared_client);
            }

            release(slot_index, k, false);
        }
    });
}

// non-reentrant
void TFGenericTCPClientPool::release(TFGenericTCPSharedClient *shared_client)
{
    if (non_reentrant) {
        tf_network_util_debugfln("TFGenericTCPClientPool[%p]::release(shared_client=%p) non-reentrant",
                                 static_cast<void *>(this), static_cast<void *>(shared_client));
        return;
    }

    TFNetworkUtil::NonReentrantScope scope(&non_reentrant);

    tf_network_util_debugfln("TFGenericTCPClientPool[%p]::release(shared_client=%p)",
                             static_cast<void *>(this), static_cast<void *>(shared_client));

    for (size_t i = 0; i < TF_GENERIC_TCP_CLIENT_POOL_MAX_SLOT_COUNT; ++i) {
        TFGenericTCPClientPoolSlot *slot = slots[i];

        if (slot == nullptr) {
            continue;
        }

        for (size_t k = 0; k < TF_GENERIC_TCP_CLIENT_POOL_MAX_SHARE_COUNT; ++k) {
            TFGenericTCPClientPoolShare *share = slot->shares[k];

            if (share == nullptr || share->shared_client != shared_client) {
                continue;
            }

            release(i, k, true);
            return;
        }
    }

    tf_network_util_debugfln("TFGenericTCPClientPool[%p]::release(shared_client=%p) shared client not found",
                             static_cast<void *>(this), static_cast<void *>(shared_client));
}

// non-reentrant
void TFGenericTCPClientPool::tick()
{
    if (non_reentrant) {
        tf_network_util_debugfln("TFGenericTCPClientPool[%p]::tick() non-reentrant", static_cast<void *>(this));
        return;
    }

    TFNetworkUtil::NonReentrantScope scope(&non_reentrant);

    for (size_t i = 0; i < TF_GENERIC_TCP_CLIENT_POOL_MAX_SLOT_COUNT; ++i) {
        TFGenericTCPClientPoolSlot *slot = slots[i];

        if (slot == nullptr) {
            continue;
        }

        if (slot->delete_pending) {
            tf_network_util_debugfln("TFGenericTCPClientPool[%p]::tick() deleting slot (slot_index=%zu client=%p)",
                                     static_cast<void *>(this), i, static_cast<void *>(slot->client));

            slots[i] = nullptr;
            delete slot->client;
            delete slot;
            continue;
        }

        slot->client->tick();
    }
}

void TFGenericTCPClientPool::release(size_t slot_index, size_t share_index, bool disconnect)
{
    TFGenericTCPClientPoolSlot *slot = slots[slot_index];

    if (slot == nullptr) {
        tf_network_util_debugfln("TFGenericTCPClientPool[%p]::release(slot_index=%zu share_index=%zu disconnect=%d) invalid slot",
                                 static_cast<void *>(this), slot_index, share_index, disconnect ? 1 : 0);
        return;
    }

    TFGenericTCPClientPoolShare *share = slot->shares[share_index];

    if (share == nullptr) {
        tf_network_util_debugfln("TFGenericTCPClientPool[%p]::release(slot_index=%zu share_index=%zu disconnect=%d) invalid share",
                                 static_cast<void *>(this), slot_index, share_index, disconnect ? 1 : 0);
        return;
    }

    tf_network_util_debugfln("TFGenericTCPClientPool[%p]::release(slot_index=%zu share_index=%zu disconnect=%d)",
                             static_cast<void *>(this), slot_index, share_index, disconnect ? 1 : 0);

    slot->shares[share_index] = nullptr;

    TFGenericTCPClientPoolDisconnectCallback disconnect_callback = std::move(share->disconnect_callback);
    share->disconnect_callback = nullptr;

    if (disconnect_callback) {
        disconnect_callback(TFGenericTCPClientDisconnectReason::Requested, -1, share->shared_client);
    }

    delete share->shared_client;
    delete share;

    bool slot_active = false;

    for (size_t k = 0; k < TF_GENERIC_TCP_CLIENT_POOL_MAX_SHARE_COUNT; ++k) {
        TFGenericTCPClientPoolShare *share = slot->shares[k];

        if (share != nullptr) {
            slot_active = true;
            break;
        }
    }

    if (!slot_active) {
        tf_network_util_debugfln("TFGenericTCPClientPool[%p]::release(slot_index=%zu share_index=%zu disconnect=%d) marking inactive slot for deletion (client=%p host_name=%s port=%u)",
                                 static_cast<void *>(this), slot_index, share_index, disconnect ? 1 : 0, static_cast<void *>(slot->client),
                                 TFNetworkUtil::printf_safe(slot->client->get_host_name()), slot->client->get_port());

        slot->delete_pending = true;

        if (disconnect) {
            slot->client->disconnect();
        }
    }
}
