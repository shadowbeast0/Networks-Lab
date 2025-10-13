#include "common.h"
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
s
const uint8_t SENDER_ADDR[6]   = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33};
const uint8_t RECEIVER_ADDR[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x44, 0x55};

static uint32_t poly_for(int w)
{
    switch (w)
    {
    case 8:  return 0x07;        // CRC-8
    case 10: return 0x233;       // CRC-10
    case 16: return 0x1021;      // CRC-16-CCITT
    case 32: return 0x04C11DB7;  // CRC-32
    default: return 0x07;
    }
}

bool is_supported_crc(int w)
{
    return (w == 8 || w == 10 || w == 16 || w == 32);
}

uint32_t compute_crc(const std::vector<uint8_t> &bytes, int widthBits)
{
    uint32_t poly = poly_for(widthBits);
    uint64_t mask = (widthBits == 32) ? 0xFFFFFFFFu : ((1ull << widthBits) - 1ull);

    uint32_t crc = 0; // init
    for (uint8_t b : bytes)
    {
        crc ^= ((uint32_t)b) << (widthBits - 8);
        for (int i = 0; i < 8; i++)
        {
            if (crc & (1u << (widthBits - 1)))
                crc = ((crc << 1) ^ poly) & mask;
            else
                crc = (crc << 1) & mask;
        }
    }
    return crc & (uint32_t)mask;
}

void fill_header(FrameHeader &h, const uint8_t src[6], const uint8_t dest[6],
                 uint16_t length, uint8_t seq)
{
    std::memcpy(h.src,  src,  6);
    std::memcpy(h.dest, dest, 6);
    h.length = htons(length);
    h.seq    = seq;
}

void append_header(std::vector<uint8_t> &out, const FrameHeader &h)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&h);
    out.insert(out.end(), p, p + sizeof(FrameHeader));
}

void append_payload(std::vector<uint8_t> &out, const std::string &p)
{
    out.insert(out.end(), p.begin(), p.end());
}

void append_crc(std::vector<uint8_t> &out, Crc32 c)
{
    uint32_t be = htonl(c); // send big-endian
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&be);
    out.insert(out.end(), p, p + sizeof(uint32_t));
}

std::vector<uint8_t> bytes_for_crc(const FrameHeader &h, const std::string &payload)
{
    std::vector<uint8_t> v;
    append_header(v, h);
    append_payload(v, payload);
    return v;
}

bool read_exact(int fd, void *buf, size_t n)
{
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < n)
    {
        ssize_t r = ::read(fd, p + got, n - got);
        if (r <= 0)
            return false;
        got += (size_t)r;
    }
    return true;
}

bool write_exact(int fd, const void *buf, size_t n)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < n)
    {
        ssize_t r = ::write(fd, p + sent, n - sent);
        if (r <= 0)
            return false;
        sent += (size_t)r;
    }
    return true;
}
