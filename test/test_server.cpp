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
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <Arduino.h>
#include "../src/TFNetworkUtil.h"
#include "../src/TFModbusTCPServer.h"

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
    TFModbusTCPServer server;

    signal(SIGINT, sigint_handler);

    TFNetworkUtil::vlogfln =
    [](const char *format, va_list args) {
        printf("%lu | ", microseconds());
        vprintf(format, args);
        puts("");
    };

    TFNetworkUtil::microseconds = microseconds;

    server.start(0, 502,
    [](uint32_t peer_address, uint16_t port) {
        printf("%lu | connected peer_address=%u port=%u\n", microseconds(), peer_address, port);
    },
    [](uint32_t peer_address, uint16_t port, TFModbusTCPServerDisconnectReason reason, int error_number) {
        printf("%lu | disconnected peer_address=%u port=%u reason=%s error_number=%d\n", microseconds(), peer_address, port, get_tf_modbus_tcp_server_client_disconnect_reason_name(reason), error_number);
    },
    [](uint8_t unit_id, TFModbusTCPFunctionCode function_code, uint16_t start_address, uint16_t data_count, void *data_values) {
        if (function_code == TFModbusTCPFunctionCode::ReadCoils) {
            for (uint16_t i = 0; i < data_count; ++i) {
                if (((start_address + i) & 1) == 0) {
                    static_cast<uint8_t *>(data_values)[i / 8] &= ~static_cast<uint8_t>(1 << (i % 8));
                }
                else {
                    static_cast<uint8_t *>(data_values)[i / 8] |= static_cast<uint8_t>(1 << (i % 8));
                }
            }

            return TFModbusTCPExceptionCode::Success;
        }
        else if (function_code == TFModbusTCPFunctionCode::ReadInputRegisters) {
            for (uint16_t i = 0; i < data_count; ++i) {
                static_cast<uint16_t *>(data_values)[i] = start_address + i;
            }

            return TFModbusTCPExceptionCode::Success;
        }

        return TFModbusTCPExceptionCode::IllegalFunction;
    });

    while (running) {
        server.tick();
        usleep(100);
    }

    server.stop();

    return 0;
}
