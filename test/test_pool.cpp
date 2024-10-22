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
#include <signal.h>
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

static volatile int running = 2;

void sigint_handler(int dummy)
{
    (void)dummy;

    printf("%lu | received SIGINT\n", microseconds());

    running = 0;
}

int main()
{
    uint16_t buffer1[2] = {0, 0};
    uint16_t buffer2[2] = {0, 0};
    TFModbusTCPClientPool pool;
    TFGenericTCPSharedClient *client_ptr1 = nullptr;
    TFGenericTCPSharedClient *client_ptr2 = nullptr;
    int64_t next_reconnect_us;

    signal(SIGINT, sigint_handler);

    TFNetworkUtil::vlogfln =
    [](const char *format, va_list args) {
        printf("%lu | ", microseconds());
        vprintf(format, args);
        puts("");
    };

    TFNetworkUtil::microseconds = microseconds;

    TFNetworkUtil::resolve =
    [](const char *host_name, std::function<void(uint32_t host_address, int error_number)> &&callback) {
        hostent *result = gethostbyname(host_name);

        if (result == nullptr) {
            callback(0, h_errno);
        }
        else {
            callback(((struct in_addr *)result->h_addr)->s_addr, 0);
        }
    };

    printf("%lu | acquire1...\n", microseconds());
    pool.acquire("localhost", 502,
    [&pool, &client_ptr1, &buffer1, &running](TFGenericTCPClientConnectResult result, int error_number, TFGenericTCPSharedClient *client) {
        printf("%lu | connect1 1st client=%p: %s / %s (%d)\n",
               microseconds(),
               static_cast<void *>(client),
               get_tf_generic_tcp_client_connect_result_name(result),
               strerror(error_number),
               error_number);

        client_ptr1 = client;

        if (result != TFGenericTCPClientConnectResult::Connected) {
            --running;
            return;
        }

        printf("%lu | read1... client=%p\n", microseconds(), client);
        static_cast<TFModbusTCPSharedClient *>(client)->read(TFModbusTCPDataType::InputRegister, 1, 1013, 2, buffer1, 1000000,
        [&pool, client, &buffer1](TFModbusTCPClientTransactionResult result) {
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
        });
    },
    [&running, &client_ptr1](TFGenericTCPClientDisconnectReason reason, int error_number, TFGenericTCPSharedClient *client) {
        printf("%lu | disconnect1 1st client=%p: %s / %s (%d)\n",
               microseconds(),
               static_cast<void *>(client),
               get_tf_generic_tcp_client_disconnect_reason_name(reason),
               strerror(error_number),
               error_number);

        client_ptr1 = nullptr;
        --running;
    });

    printf("%lu | acquire2...\n", microseconds());
    pool.acquire("localhost", 1502,
    [&pool, &client_ptr2, &buffer2, &running](TFGenericTCPClientConnectResult result, int error_number, TFGenericTCPSharedClient *client) {
        printf("%lu | connect2 client=%p: %s / %s (%d)\n",
               microseconds(),
               static_cast<void *>(client),
               get_tf_generic_tcp_client_connect_result_name(result),
               strerror(error_number),
               error_number);

        client_ptr2 = client;

        if (result != TFGenericTCPClientConnectResult::Connected) {
            --running;
            return;
        }

        printf("%lu | read2... client=%p\n", microseconds(), static_cast<void *>(client));
        static_cast<TFModbusTCPSharedClient *>(client)->read(TFModbusTCPDataType::InputRegister, 1, 1013, 2, buffer2, 1000000,
        [&pool, &buffer2](TFModbusTCPClientTransactionResult result) {
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
        });
    },
    [&running, &client_ptr2](TFGenericTCPClientDisconnectReason reason, int error_number, TFGenericTCPSharedClient *client) {
        printf("%lu | disconnect2 client=%p: %s / %s (%d)\n",
               microseconds(),
               static_cast<void *>(client),
               get_tf_generic_tcp_client_disconnect_reason_name(reason),
               strerror(error_number),
               error_number);

        client_ptr2 = nullptr;
        --running;
    });

    next_reconnect_us = TFNetworkUtil::calculate_deadline(5000000);

    while (running > 0) {
        if (client_ptr1 != nullptr && next_reconnect_us >= 0 && TFNetworkUtil::deadline_elapsed(next_reconnect_us)) {
            next_reconnect_us = -1;

            printf("%lu | release1...\n", microseconds());
            pool.release(client_ptr1);
            client_ptr1 = nullptr;

            printf("%lu | reacquire1...\n", microseconds());
            pool.acquire("localhost", 502,
            [&pool, &client_ptr1, &buffer1, &running](TFGenericTCPClientConnectResult result, int error_number, TFGenericTCPSharedClient *client) {
                printf("%lu | connect1 2nd client=%p: %s / %s (%d)\n",
                       microseconds(),
                       static_cast<void *>(client),
                       get_tf_generic_tcp_client_connect_result_name(result),
                       strerror(error_number),
                       error_number);

                client_ptr1 = client;
            },
            [&running, &client_ptr1](TFGenericTCPClientDisconnectReason reason, int error_number, TFGenericTCPSharedClient *client) {
                printf("%lu | disconnect1 2nd client=%p: %s / %s (%d)\n",
                       microseconds(),
                       static_cast<void *>(client),
                       get_tf_generic_tcp_client_disconnect_reason_name(reason),
                       strerror(error_number),
                       error_number);

                client_ptr1 = nullptr;
            });
        }

        pool.tick();
        usleep(100);
    }

    if (client_ptr1 != nullptr) {
        printf("%lu | release1 client=%p\n",
                microseconds(),
                static_cast<void *>(client_ptr1));

        pool.release(client_ptr1);
    }

    if (client_ptr2 != nullptr) {
        printf("%lu | release2 client=%p\n",
                microseconds(),
                static_cast<void *>(client_ptr2));

        pool.release(client_ptr2);
    }

    pool.tick();

    return 0;
}
