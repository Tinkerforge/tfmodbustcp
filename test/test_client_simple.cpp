/* TFNetwork
 * Copyright (C) 2026 Matthias Bolte <matthias@tinkerforge.com>
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
#include <sys/random.h>
#include <Arduino.h>
#include "../src/TFNetwork.h"
#include "../src/TFModbusTCPClient.h"
#include "../src/TFModbusTCPClientPool.h"

micros_t now_us()
{
    struct timeval tv;
    static int64_t baseline_sec = 0;

    gettimeofday(&tv, nullptr);

    if (baseline_sec == 0) {
        baseline_sec = tv.tv_sec;
    }

    return micros_t{(static_cast<int64_t>(tv.tv_sec) - baseline_sec) * 1000000 + tv.tv_usec};
}

static volatile bool running = true;

void sigint_handler(int dummy)
{
    (void)dummy;

    TFNetwork::logfln("received SIGINT");

    running = false;
}

int main()
{
    TFNetwork::vlogfln =
    [](const char *format, va_list args) {
        printf("%li | ", (int64_t)now_us());
        vprintf(format, args);
        puts("");
    };

    TFNetwork::get_random_uint16 =
    []() {
        uint16_t r;

        if (getrandom(&r, sizeof(r), 0) != sizeof(r)) {
            abort();
        }

        return r;
    };

    signal(SIGINT, sigint_handler);

    std::function<void(ip_addr_t *address, int error_number)> resolve_callback;
    TFModbusTCPClient client(TFModbusTCPByteOrder::Host);

    TFNetwork::resolve =
    [](const char *host, std::function<void(ip_addr_t *address, int error_number)> &&callback) {
        hostent *result = gethostbyname(host);

        if (result == nullptr) {
            callback(nullptr, h_errno);
        }
        else {
            ip_addr_t address;
            ip_addr_set_ip4_u32_val(address, ((struct in_addr *)result->h_addr)->s_addr);

            callback(&address, 0);
        }
    };

    TFNetwork::logfln("connect...");
    client.connect("localhost", 502,
    [](TFGenericTCPClientConnectResult result, int error_number) {
        TFNetwork::logfln("connect: %s / %s (%d)",
                          get_tf_generic_tcp_client_connect_result_name(result),
                          strerror(error_number),
                          error_number);
    },
    [](TFGenericTCPClientDisconnectReason reason, int error_number) {
        TFNetwork::logfln("disconnect: %s / %s (%d)",
                          get_tf_generic_tcp_client_disconnect_reason_name(reason),
                          strerror(error_number),
                          error_number);
    });

    client.tick();

    if (client.get_connection_status() != TFGenericTCPClientConnectionStatus::Disconnected) {
        TFNetwork::logfln("disconnect...");
        client.disconnect();
        client.tick();
    }

    return 0;
}
