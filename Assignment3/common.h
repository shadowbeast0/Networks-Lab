#pragma once
#include <cstdint>
#include <string>
#include <vector>

extern const uint8_t SENDER_ADDR[6];
extern const uint8_t RECEIVER_ADDR[6];

struct FrameHeader
{
    uint8_t src[6];
    uint8_t dest[6];
    uint16_t length;
    uint8_t seq;
} __attribute__((packed));

using Crc32 = uint32_t;

struct Frame
{
    FrameHeader hdr;
    std::string payload;
    Crc32 crc;
};

uint32_t compute_crc(const std::vector<uint8_t> &bytes, int widthBits);

void fill_header(FrameHeader &h, const uint8_t src[6], const uint8_t dest[6],
                 uint16_t length, uint8_t seq);

void append_header(std::vector<uint8_t> &out, const FrameHeader &h);
void append_payload(std::vector<uint8_t> &out, const std::string &p);
void append_crc(std::vector<uint8_t> &out, Crc32 c);

std::vector<uint8_t> bytes_for_crc(const FrameHeader &h, const std::string &payload);

bool read_exact(int fd, void *buf, size_t n);
bool write_exact(int fd, const void *buf, size_t n);

bool is_supported_crc(int widthBits);
