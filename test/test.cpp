#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "../src/TFModbusTCPClient.h"

int main()
{
    TFModbusTCPClient tfmbt;
    uint16_t tfmbt_buffer[2];

    printf("%lu | connect...\n", time(nullptr));
    tfmbt.connect(IPAddress(10, 2, 80, 4), 505, [&tfmbt, &tfmbt_buffer](TFModbusTCPClientConnectionStatus status, int error_number){
        printf("%lu | status: %s / %s (%d)\n",
               time(nullptr),
               get_tf_modbus_tcp_client_connection_status_name(status),
               strerror(error_number),
               error_number);

        if (status == TFModbusTCPClientConnectionStatus::Connected) {
            printf("%lu | read_register...\n", time(nullptr));
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
                                    printf("%lu | read_register: %s (%d) [%u %u -> %f]\n",
                                           time(nullptr),
                                           get_tf_modbus_tcp_client_transaction_result_name(result),
                                           static_cast<int>(result),
                                           c32.r[0],
                                           c32.r[1],
                                           static_cast<double>(c32.f));
                                });
        }
    });

    while (true) {
        tfmbt.tick();
    }

    return 0;
}
