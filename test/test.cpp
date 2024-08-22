#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <Arduino.h>
#include "../src/TFNetworkUtil.h"
#include "../src/TFModbusTCPClient.h"

static uint32_t milliseconds()
{
    struct timeval tv;
    static uint32_t baseline_sec = 0;

    gettimeofday(&tv, nullptr);

    if (baseline_sec == 0) {
        baseline_sec = tv.tv_sec;
    }

    return (tv.tv_sec - baseline_sec) * 1000 + tv.tv_usec / 1000;
}

int main()
{
    TFModbusTCPClient tfmbt;
    uint16_t tfmbt_buffer[2] = {0, 0};
    std::function<void(uint32_t host_address, int error_number)> resolve_callback;
    uint32_t resolve_callback_time;

    TFNetworkUtil::set_milliseconds_callback(milliseconds);

    set_tf_generic_tcp_client_resolve_callback([&resolve_callback, &resolve_callback_time](const char *host_name, std::function<void(uint32_t host_address, int error_number)> &&callback) {
        resolve_callback = callback;
        resolve_callback_time = milliseconds();
    });

    printf("%u | connect...\n", milliseconds());
    tfmbt.connect("foobar", 502, [&tfmbt, &tfmbt_buffer](TFGenericTCPClientEvent event, int error_number) {
        printf("%u | event: %s / %s (%d)\n",
               milliseconds(),
               get_tf_generic_tcp_client_event_name(event),
               strerror(error_number),
               error_number);

        if (event == TFGenericTCPClientEvent::Connected) {
            printf("%u | read_register...\n", milliseconds());
            tfmbt.read_register(TFModbusTCPClientRegisterType::InputRegister,
                                1,
                                1013,
                                2,
                                tfmbt_buffer,
                                [&tfmbt_buffer](TFModbusTCPClientResult result) {
                                    union {
                                        float f;
                                        uint16_t r[2];
                                    } c32;

                                    c32.r[0] = tfmbt_buffer[0];
                                    c32.r[1] = tfmbt_buffer[1];
                                    printf("%u | read_register: %s (%d) [%u %u -> %f]\n",
                                           milliseconds(),
                                           get_tf_modbus_tcp_client_result_name(result),
                                           static_cast<int>(result),
                                           c32.r[0],
                                           c32.r[1],
                                           static_cast<double>(c32.f));
                                });
        }
    });

    while (true) {
        if (resolve_callback && resolve_callback_time + 5000 < milliseconds()) {
            resolve_callback(IPAddress(10, 2, 80, 4), 0);
            resolve_callback = nullptr;
        }

        tfmbt.tick();
    }

    return 0;
}
