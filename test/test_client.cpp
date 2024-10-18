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

static volatile bool running = true;

void sigint_handler(int dummy)
{
    (void)dummy;

    printf("%lu | received SIGINT\n", microseconds());

    running = false;
}

int main()
{
    uint16_t register_buffer[2] = {0, 0};
    uint8_t coil_buffer[2] = {0, 0};
    char *resolve_host_name = nullptr;
    std::function<void(uint32_t host_address, int error_number)> resolve_callback;
    int64_t resolve_callback_time_us;
    TFModbusTCPClient client;
    int64_t next_read_time_us = -1;

    signal(SIGINT, sigint_handler);

    TFNetworkUtil::vlogfln =
    [](const char *format, va_list args) {
        printf("%lu | ", microseconds());
        vprintf(format, args);
        puts("");
    };

    TFNetworkUtil::microseconds = microseconds;

    TFNetworkUtil::resolve =
    [&resolve_host_name, &resolve_callback, &resolve_callback_time_us](const char *host_name, std::function<void(uint32_t host_address, int error_number)> &&callback) {
        resolve_host_name = strdup(host_name);
        resolve_callback = std::move(callback);
        resolve_callback_time_us = microseconds();
    };

    printf("%lu | connect...\n", microseconds());
    client.connect("localhost", 502,
    [&running, &next_read_time_us](TFGenericTCPClientConnectResult result, int error_number) {
        printf("%lu | connect: %s / %s (%d)\n",
               microseconds(),
               get_tf_generic_tcp_client_connect_result_name(result),
               strerror(error_number),
               error_number);

        if (result == TFGenericTCPClientConnectResult::Connected) {
            next_read_time_us = TFNetworkUtil::calculate_deadline(100000);
        }
        else {
            running = false;
        }
    },
    [&running](TFGenericTCPClientDisconnectReason reason, int error_number) {
        printf("%lu | disconnect: %s / %s (%d)\n",
               microseconds(),
               get_tf_generic_tcp_client_disconnect_reason_name(reason),
               strerror(error_number),
               error_number);

        running = false;
    });

    while (running) {
        if (resolve_host_name != nullptr && resolve_callback && resolve_callback_time_us + 1000000 < microseconds()) {
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

        if (next_read_time_us >= 0 && TFNetworkUtil::deadline_elapsed(next_read_time_us)) {
            next_read_time_us = -1;

            printf("%lu | read input registers...\n", microseconds());
            client.read(TFModbusTCPDataType::InputRegister, 1, 1013, 2, register_buffer, 1000000,
            [&register_buffer](TFModbusTCPClientTransactionResult result) {
                union {
                    float f;
                    uint16_t r[2];
                } c32;

                c32.r[0] = register_buffer[0];
                c32.r[1] = register_buffer[1];

                printf("%lu | read input registers: %s (%d) [%u %u -> %f]\n",
                       microseconds(),
                       get_tf_modbus_tcp_client_transaction_result_name(result),
                       static_cast<int>(result),
                       c32.r[0],
                       c32.r[1],
                       static_cast<double>(c32.f));
            });

            printf("%lu | read coils...\n", microseconds());
            client.read(TFModbusTCPDataType::Coil, 1, 122, 10, coil_buffer, 1000000,
            [&next_read_time_us, &coil_buffer](TFModbusTCPClientTransactionResult result) {
                printf("%lu | read coils: %s (%d) [%u %u %u %u %u %u %u %u %u %u]\n",
                       microseconds(),
                       get_tf_modbus_tcp_client_transaction_result_name(result),
                       static_cast<int>(result),
                       (coil_buffer[0] >> 0) & 1,
                       (coil_buffer[0] >> 1) & 1,
                       (coil_buffer[0] >> 2) & 1,
                       (coil_buffer[0] >> 3) & 1,
                       (coil_buffer[0] >> 4) & 1,
                       (coil_buffer[0] >> 5) & 1,
                       (coil_buffer[0] >> 6) & 1,
                       (coil_buffer[0] >> 7) & 1,
                       (coil_buffer[1] >> 0) & 1,
                       (coil_buffer[1] >> 1) & 1);

                next_read_time_us = TFNetworkUtil::calculate_deadline(100000);
            });
        }

        client.tick();
        usleep(100);
    }

    client.disconnect();

    return 0;
}
