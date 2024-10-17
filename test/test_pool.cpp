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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <netdb.h>
#include <unistd.h>
#include <Arduino.h>
#include "../src/TFNetworkUtil.h"
#include "../src/TFModbusTCPClient.h"
#include "../src/TFModbusTCPClientPool.h"

static int64_t microseconds()
{
    struct timeval tv;
    static int64_t baseline_sec = 0;

    gettimeofday(&tv, nullptr);

    if (baseline_sec == 0) {
        baseline_sec = tv.tv_sec;
    }

    return (static_cast<int64_t>(tv.tv_sec) - baseline_sec) * 1000000 + tv.tv_usec;
}

int main()
{
    char *resolve_host_name = nullptr;
    TFModbusTCPClient client;
    uint16_t buffer1[2] = {0, 0};
    uint16_t buffer2[2] = {0, 0};
    std::function<void(uint32_t host_address, int error_number)> resolve_callback;
    uint32_t resolve_callback_time;
    int running = 2;
    TFModbusTCPClientPool pool;
    TFGenericTCPClientPoolHandle *handle_ptr1 = nullptr;
    TFGenericTCPClientPoolHandle *handle_ptr2 = nullptr;

    TFNetworkUtil::set_logln_callback([](const char *message) {
        printf("%lu | %s\n", microseconds(), message);
    });

    TFNetworkUtil::set_microseconds_callback(microseconds);

    TFNetworkUtil::set_resolve_callback([&resolve_host_name, &resolve_callback, &resolve_callback_time](const char *host_name, std::function<void(uint32_t host_address, int error_number)> &&callback) {
        resolve_host_name = strdup(host_name);
        resolve_callback = callback;
        resolve_callback_time = microseconds();
    });

    printf("%lu | acquire1...\n", microseconds());
    pool.acquire("localhost", 502,
    [&pool, &handle_ptr1, &buffer1/*, &running*/](TFGenericTCPClientConnectResult result, int error_number, TFGenericTCPClientPoolHandle *handle) {
        printf("%lu | connect1 handle=%p: %s / %s (%d)\n",
               microseconds(),
               static_cast<void *>(handle),
               get_tf_generic_tcp_client_connect_result_name(result),
               strerror(error_number),
               error_number);

        handle_ptr1 = handle;

#if 1
        if (result != TFGenericTCPClientConnectResult::Connected) {
            --running;
            return;
        }

        pool.release(handle);
#else
        printf("%lu | read1... handle=%p\n", microseconds(), handle);
        static_cast<TFModbusTCPClient *>(handle->client)->read(TFModbusTCPDataType::InputRegister,
                                1,
                                1013,
                                2,
                                buffer1,
                                1000000,
                                [&pool, handle, &buffer1](TFModbusTCPClientTransactionResult result) {
                                    union {
                                        float f;
                                        uint16_t r[2];
                                    } c32;

                                    c32.r[0] = buffer1[0];
                                    c32.r[1] = buffer1[1];
                                    printf("%lu | read1: %s (%d) [%u %u -> %f]\n",
                                        microseconds(),
                                        get_tf_modbus_tcp_client_transaction_result_name(result),
                                        static_cast<int>(result),
                                        c32.r[0],
                                        c32.r[1],
                                        static_cast<double>(c32.f));
                                printf("%lu | release1 handle=%p\n",
                                        microseconds(),
                                        handle);
                                pool.release(handle);
                            });
#endif
    },
    [&running](TFGenericTCPClientDisconnectReason reason, int error_number, TFGenericTCPClientPoolHandle *handle) {
        printf("%lu | disconnect1 handle=%p: %s / %s (%d)\n",
               microseconds(),
               static_cast<void *>(handle),
               get_tf_generic_tcp_client_disconnect_reason_name(reason),
               strerror(error_number),
               error_number);

        --running;
    });

    printf("%lu | acquire2...\n", microseconds());
    pool.acquire("localhost", 502,
    [&pool, &handle_ptr2, &buffer2, &running](TFGenericTCPClientConnectResult result, int error_number, TFGenericTCPClientPoolHandle *handle) {
        printf("%lu | connect2 handle=%p: %s / %s (%d)\n",
               microseconds(),
               static_cast<void *>(handle),
               get_tf_generic_tcp_client_connect_result_name(result),
               strerror(error_number),
               error_number);

        handle_ptr2 = handle;

        if (result != TFGenericTCPClientConnectResult::Connected) {
            --running;
            return;
        }

        printf("%lu | read2... handle=%p\n", microseconds(), static_cast<void *>(handle));
        static_cast<TFModbusTCPClient *>(handle->client)->read(TFModbusTCPDataType::InputRegister,
                                1,
                                1013,
                                2,
                                buffer2,
                                1000000,
                                [&pool, handle, &buffer2](TFModbusTCPClientTransactionResult result) {
                                    union {
                                        float f;
                                        uint16_t r[2];
                                    } c32;

                                    c32.r[0] = buffer2[0];
                                    c32.r[1] = buffer2[1];
                                    printf("%lu | read2: %s (%d) [%u %u -> %f]\n",
                                        microseconds(),
                                        get_tf_modbus_tcp_client_transaction_result_name(result),
                                        static_cast<int>(result),
                                        c32.r[0],
                                        c32.r[1],
                                        static_cast<double>(c32.f));
                                printf("%lu | release2 handle=%p\n",
                                       microseconds(),
                                       static_cast<void *>(handle);
                                pool.release(handle);
                            });
    },
    [&running](TFGenericTCPClientDisconnectReason reason, int error_number, TFGenericTCPClientPoolHandle *handle) {
        printf("%lu | disconnect2 handle=%p: %s / %s (%d)\n",
               microseconds(),
               static_cast<void *>(handle),
               get_tf_generic_tcp_client_disconnect_reason_name(reason),
               strerror(error_number),
               error_number);

        --running;
    });

    while (running > 0) {
        if (resolve_host_name != nullptr && resolve_callback && resolve_callback_time + 1000000 < microseconds()) {
            hostent *result = gethostbyname(resolve_host_name);

            free(resolve_host_name);
            resolve_host_name = nullptr;

            if (result == nullptr) {
                resolve_callback(0, h_errno);
                resolve_callback = nullptr;
                continue;
            }

            resolve_callback(((struct in_addr *)result->h_addr)->s_addr, 0);
            resolve_callback = nullptr;
        }

        pool.tick();
        usleep(100);
    }

    pool.tick();
    pool.tick();
    pool.tick();

    return 0;
}
