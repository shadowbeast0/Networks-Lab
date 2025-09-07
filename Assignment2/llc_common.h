#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")

#include <cstdint>
#include <cstdlib>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <random>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <condition_variable>

namespace llc {

inline double clampd(double x, double lo, double hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

inline void winsock_init() {
    WSADATA wsaData{};
    int r = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (r != 0) { std::cerr << "WSAStartup failed: " << r << "\n"; std::exit(1); }
}
inline void winsock_cleanup() { WSACleanup(); }

inline SOCKET make_listen_socket(uint16_t port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { std::cerr << "socket() failed\n"; std::exit(1); }
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) { std::cerr << "bind() failed\n"; std::exit(1); }
    if (listen(s, SOMAXCONN) == SOCKET_ERROR) { std::cerr << "listen() failed\n"; std::exit(1); }
    return s;
}
inline SOCKET make_connect_socket(const char* host, uint16_t port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { std::cerr << "socket() failed\n"; std::exit(1); }
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "connect() failed\n"; closesocket(s); std::exit(1);
    }
    return s;
}

inline bool send_all(SOCKET s, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int r = send(s, reinterpret_cast<const char*>(buf + sent), int(len - sent), 0);
        if (r == SOCKET_ERROR || r == 0) return false;
        sent += size_t(r);
    }
    return true;
}
inline bool recv_exact(SOCKET s, uint8_t* buf, size_t len, int timeout_ms) {
    size_t got = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (got < len) {
        auto now = std::chrono::steady_clock::now();
        int remaining = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        if (remaining <= 0) return false;

        fd_set fds; FD_ZERO(&fds); FD_SET(s, &fds);
        timeval tv{}; tv.tv_sec = remaining / 1000; tv.tv_usec = (remaining % 1000) * 1000;
        int pr = select(0, &fds, nullptr, nullptr, &tv);
        if (pr <= 0) return false;

        int r = recv(s, reinterpret_cast<char*>(buf + got), int(len - got), 0);
        if (r <= 0) return false;
        got += size_t(r);
    }
    return true;
}

struct Channel {
    double bit_error_prob = 0.0;
    int max_delay_ms = 0;
    double loss_prob = 0.0;
    std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<double> U{0.0, 1.0};

    void apply_delay() {
        if (max_delay_ms <= 0) return;
        int d = int(U(rng) * (max_delay_ms + 1));
        Sleep(static_cast<DWORD>(d));
    }
    bool maybe_drop() { return U(rng) < loss_prob; }

    void flip_bits(std::vector<uint8_t>& buf) {
        if (bit_error_prob <= 0.0) return;
        std::bernoulli_distribution B(bit_error_prob);
        for (auto& b : buf) {
            uint8_t m = 0;
            for (int i = 0; i < 8; ++i) if (B(rng)) m ^= (1u << i);
            b ^= m;
        }
    }
};

inline uint32_t crc32(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool inited = false;
    if (!inited) {
        const uint32_t poly = 0xEDB88320u;
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j)
                c = (c & 1) ? (poly ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        inited = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

static constexpr size_t MIN_PAYLOAD = 46;
struct Frame {
    uint8_t src[6]{};
    uint8_t dst[6]{};
    uint16_t length{0};
    uint8_t seq{0};
    std::vector<uint8_t> payload;
    uint32_t fcs{0};

    std::vector<uint8_t> serialize_no_crc() const {
        std::vector<uint8_t> out;
        out.insert(out.end(), src, src + 6);
        out.insert(out.end(), dst, dst + 6);
        uint16_t be_len = htons(length);
        out.push_back(uint8_t(be_len >> 8));
        out.push_back(uint8_t(be_len & 0xFF));
        out.push_back(seq);
        out.insert(out.end(), payload.begin(), payload.end());
        if (out.size() < (15 + MIN_PAYLOAD)) {
            size_t need = (15 + MIN_PAYLOAD) - out.size();
            out.insert(out.end(), need, uint8_t(' '));
        }
        return out;
    }
    std::vector<uint8_t> serialize_with_crc() {
        auto body = serialize_no_crc();
        uint32_t c = crc32(body.data(), body.size());
        fcs = c;
        body.push_back(uint8_t((c >> 24) & 0xFF));
        body.push_back(uint8_t((c >> 16) & 0xFF));
        body.push_back(uint8_t((c >> 8) & 0xFF));
        body.push_back(uint8_t(c & 0xFF));
        return body;
    }
    static bool parse(const std::vector<uint8_t>& buf, Frame& out) {
        if (buf.size() < 15 + MIN_PAYLOAD + 4) return false;
        std::copy(buf.begin(), buf.begin() + 6, out.src);
        std::copy(buf.begin() + 6, buf.begin() + 12, out.dst);
        uint16_t be_len = (uint16_t(buf[12]) << 8) | uint16_t(buf[13]);
        out.length = ntohs(be_len);
        out.seq = buf[14];
        size_t header_payload = 15 + std::max<size_t>(MIN_PAYLOAD, out.length);
        if (buf.size() < header_payload + 4) return false;
        out.payload.assign(buf.begin() + 15, buf.begin() + 15 + std::max<size_t>(MIN_PAYLOAD, out.length));
        out.fcs = (uint32_t(buf[header_payload]) << 24) | (uint32_t(buf[header_payload + 1]) << 16)
                | (uint32_t(buf[header_payload + 2]) << 8) | uint32_t(buf[header_payload + 3]);
        return true;
    }
    static bool verify_crc(const std::vector<uint8_t>& buf) {
        if (buf.size() < 4) return false;
        uint32_t got = (uint32_t(buf[buf.size() - 4]) << 24) | (uint32_t(buf[buf.size() - 3]) << 16)
                     | (uint32_t(buf[buf.size() - 2]) << 8) | uint32_t(buf[buf.size() - 1]);
        uint32_t calc = crc32(buf.data(), buf.size() - 4);
        return got == calc;
    }
};

enum : uint8_t { ACK = 0x06, NAK = 0x15 };
struct Ack {
    uint8_t type{ACK};
    uint8_t seq{0};
    uint32_t fcs{0};
    std::vector<uint8_t> serialize() {
        std::vector<uint8_t> b{type, seq};
        uint32_t c = crc32(b.data(), b.size());
        fcs = c;
        b.push_back(uint8_t((c >> 24) & 0xFF));
        b.push_back(uint8_t((c >> 16) & 0xFF));
        b.push_back(uint8_t((c >> 8) & 0xFF));
        b.push_back(uint8_t(c & 0xFF));
        return b;
    }
    static bool parse(const uint8_t* buf, size_t len, Ack& out) {
        if (len < 6) return false;
        out.type = buf[0];
        out.seq = buf[1];
        uint32_t got = (uint32_t(buf[2]) << 24) | (uint32_t(buf[3]) << 16) | (uint32_t(buf[4]) << 8) | uint32_t(buf[5]);
        std::vector<uint8_t> b{out.type, out.seq};
        uint32_t calc = crc32(b.data(), b.size());
        if (got != calc) return false;
        out.fcs = got;
        return true;
    }
};

inline void random_mac(uint8_t mac[6]) {
    static std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<int> D(0, 255);
    for (int i = 0; i < 6; ++i) mac[i] = uint8_t(D(rng));
    mac[0] &= 0xFE;
    mac[0] |= 0x02;
}

struct RttEstimator {
    bool have = false;
    double srtt = 0.0, rttvar = 0.0;
    double rto_ms = 1000.0;
    void observe(double sample_ms) {
        if (!have) {
            srtt = sample_ms;
            rttvar = sample_ms / 2.0;
            have = true;
        } else {
            const double alpha = 1.0 / 8.0;
            const double beta  = 1.0 / 4.0;
            rttvar = (1.0 - beta) * rttvar + beta * std::abs(srtt - sample_ms);
            srtt   = (1.0 - alpha) * srtt   + alpha * sample_ms;
        }
        rto_ms = clampd(srtt + 4.0 * rttvar, 200.0, 4000.0);
    }
};

} // namespace llc
