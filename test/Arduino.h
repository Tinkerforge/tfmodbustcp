#pragma once

#include <stdint.h>
#include <string.h>
#include <sys/time.h>

class IPAddress
{
public:
    IPAddress()
    {
        dword = 0;
    }

    IPAddress(uint8_t first_octet, uint8_t second_octet, uint8_t third_octet, uint8_t fourth_octet)
    {
        bytes[0] = first_octet;
        bytes[1] = second_octet;
        bytes[2] = third_octet;
        bytes[3] = fourth_octet;
    }

    IPAddress(uint32_t address)
    {
        dword = address;
    }

    bool operator==(const IPAddress &other) const
    {
        return dword == other.dword;
    }

    bool operator!=(const IPAddress &other) const
    {
        return dword != other.dword;
    }

    operator uint32_t() const
    {
        return dword;
    }

    union {
        uint8_t bytes[4];
        uint32_t dword;
    };
};

inline uint32_t millis()
{
    struct timeval tv;

    gettimeofday(&tv, nullptr);

    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

