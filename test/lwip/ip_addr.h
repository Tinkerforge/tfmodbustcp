/* TFNetwork test shim
 *
 * Provides a host-side (Linux/macOS) stand-in for the lwIP ip_addr.h that the
 * firmware build of TFNetwork uses on the ESP32. Only the subset of types,
 * macros and functions actually referenced by TFNetwork and the tests is
 * implemented.
 */

#pragma once

#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

// "u32_t" is lwIP's typedef for an unsigned 32 bit integer. Pull in the
// platform header that defines it on Linux/macOS builds.
#include <sys/types.h>

#ifndef LWIP_IPV6_SCOPES
#define LWIP_IPV6_SCOPES 0
#endif

enum lwip_ip_addr_type {
    IPADDR_TYPE_V4 = 0U,
    IPADDR_TYPE_V6 = 6U,
};

struct ip4_addr {
    uint32_t addr;
};

struct ip6_addr {
    uint32_t addr[4];
#if LWIP_IPV6_SCOPES
    uint8_t zone;
#endif
};

typedef struct ip4_addr ip4_addr_t;
typedef struct ip6_addr ip6_addr_t;

struct ip_addr {
    union {
        ip6_addr_t ip6;
        ip4_addr_t ip4;
    } u_addr;
    uint8_t type;
};

typedef struct ip_addr ip_addr_t;

static inline void ip4_addr_set_u32(ip4_addr_t *dest, uint32_t src)
{
    dest->addr = src;
}

static inline void ip_addr_set_ip4_u32_val(ip_addr_t &dest, uint32_t value)
{
    ip4_addr_set_u32(&dest.u_addr.ip4, value);
    dest.type = IPADDR_TYPE_V4;
}

#define IPADDR_ANY ((uint32_t)0x00000000UL)

static inline void ip_addr_set_any(ip_addr_t &dest)
{
    dest.u_addr.ip4.addr = IPADDR_ANY;
    dest.type = IPADDR_TYPE_V4;
}

static inline void ip_addr_set_ip4_u32(ip_addr_t *dest, uint32_t value)
{
    if (dest != nullptr) {
        ip4_addr_set_u32(&dest->u_addr.ip4, value);
        dest->type = IPADDR_TYPE_V4;
    }
}

static inline int ip6_addr_isipv4mappedipv6(const ip6_addr_t *ip6addr)
{
    return ip6addr->addr[0] == 0
        && ip6addr->addr[1] == 0
        && ip6addr->addr[2] == htonl(0x0000FFFFUL);
}

static inline void ip_addr_set_ip6(ip_addr_t *dest, const ip6_addr_t *src)
{
    memcpy(&dest->u_addr.ip6, src, sizeof(ip6_addr_t));
    dest->type = IPADDR_TYPE_V6;
}

#define IP_ADDR6(dest, i0, i1, i2, i3) do {                                          \
    (dest)->u_addr.ip6.addr[0] = (i0);                                              \
    (dest)->u_addr.ip6.addr[1] = (i1);                                              \
    (dest)->u_addr.ip6.addr[2] = (i2);                                              \
    (dest)->u_addr.ip6.addr[3] = (i3);                                              \
    (dest)->type = IPADDR_TYPE_V6;                                                  \
} while (0)

static inline char *ip4addr_ntoa_r(const ip4_addr_t *addr, char *buf, int buflen)
{
    if (buf == nullptr || buflen <= 0) {
        return nullptr;
    }

    struct in_addr in;

    in.s_addr = addr->addr;

    if (inet_ntop(AF_INET, &in, buf, buflen) == nullptr) {
        return nullptr;
    }

    return buf;
}

static inline char *ip6addr_ntoa_r(const ip6_addr_t *addr, char *buf, int buflen)
{
    if (buf == nullptr || buflen <= 0) {
        return nullptr;
    }

    if (inet_ntop(AF_INET6, addr->addr, buf, buflen) == nullptr) {
        return nullptr;
    }

    return buf;
}

static inline char *ipaddr_ntoa_r(const ip_addr_t *addr, char *buf, int buflen)
{
    if (addr == nullptr) {
        return nullptr;
    }

    if (addr->type == IPADDR_TYPE_V4) {
        return ip4addr_ntoa_r(&addr->u_addr.ip4, buf, buflen);
    }

    if (addr->type == IPADDR_TYPE_V6) {
        return ip6addr_ntoa_r(&addr->u_addr.ip6, buf, buflen);
    }

    return nullptr;
}