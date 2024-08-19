#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "../src/TFModbusTCPClient.h"

int main()
{
    TFModbusTCPClient tfmbt;
    uint16_t tfmbt_buffer[2];
    std::function<void(IPAddress host_address, int error_number)> resolve_callback;
    uint32_t resolve_callback_time;

    set_tf_modbus_tcp_client_resolve_callback([&resolve_callback, &resolve_callback_time](String host_name, std::function<void(IPAddress host_address, int error_number)> &&callback) {
        resolve_callback = callback;
        resolve_callback_time = millis();
    });

    printf("%u | connect...\n", millis());
    tfmbt.connect("foobar", 502, [&tfmbt, &tfmbt_buffer](TFModbusTCPClientConnectionStatus status, int error_number){
        printf("%u | status: %s / %s (%d)\n",
               millis(),
               get_tf_modbus_tcp_client_connection_status_name(status),
               strerror(error_number),
               error_number);

        if (status == TFModbusTCPClientConnectionStatus::Connected) {
            printf("%u | read_register...\n", millis());
            tfmbt.read_register(TFModbusTCPClientRegisterType::InputRegister,
                                1,
                                1013,
                                2,
                                tfmbt_buffer,
                                [&tfmbt_buffer](TFModbusTCPClientTransactionResult result){
                                    union {
                                        float f;
                                        uint16_t r[2];
                                    } c32;

                                    c32.r[0] = tfmbt_buffer[0];
                                    c32.r[1] = tfmbt_buffer[1];
                                    printf("%u | read_register: %s (%d) [%u %u -> %f]\n",
                                           millis(),
                                           get_tf_modbus_tcp_client_transaction_result_name(result),
                                           static_cast<int>(result),
                                           c32.r[0],
                                           c32.r[1],
                                           static_cast<double>(c32.f));
                                });
        }
    });

    while (true) {
        if (resolve_callback && resolve_callback_time + 5000 < millis()) {
            resolve_callback(IPAddress(10, 2, 80, 4), 0);
            resolve_callback = nullptr;
        }

        tfmbt.tick();
    }

    return 0;
}
