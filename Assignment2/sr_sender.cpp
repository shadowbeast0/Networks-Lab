#include "llc_common.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
using namespace llc;

static const uint16_t PORT = 8000;

bool read_payloads(const std::string& path, std::vector<std::vector<uint8_t>>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        std::vector<uint8_t> p(line.begin(), line.end());
        if (p.size() < MIN_PAYLOAD) p.resize(MIN_PAYLOAD, uint8_t(' '));
        out.emplace_back(std::move(p));
    }
    return true;
}

int main(int argc, char** argv) {
    winsock_init();

    int N = (argc >= 2 ? std::stoi(argv[1]) : 4);
    double p_err = (argc >= 3 ? std::stod(argv[2]) : 0.0);
    int max_delay = (argc >= 4 ? std::stoi(argv[3]) : 0);
    Channel chan{p_err, max_delay, 0.0};

    SOCKET ls = make_listen_socket(PORT);
    std::cout << "[SR SENDER] Listening on " << PORT << " (N=" << N << ")\n";
    SOCKET conn = accept(ls, nullptr, nullptr);
    if (conn == INVALID_SOCKET) { std::cerr << "accept() failed\n"; return 1; }
    std::cout << "[SR SENDER] Connection established.\n";

    std::vector<std::vector<uint8_t>> payloads;
    if (!read_payloads("data.txt", payloads) || payloads.empty()) {
        std::cerr << "No data\n"; return 1;
    }

    uint8_t src[6], dst[6];
    random_mac(src);
    random_mac(dst);
    RttEstimator rtt;

    uint8_t base = 0;
    uint8_t nextseq = 0;
    size_t idx = 0;

    struct Slot {
        bool in_use = false;
        bool acked = false;
        std::vector<uint8_t> wire;
        std::chrono::steady_clock::time_point deadline;
    };
    std::map<uint8_t, Slot> window;

    auto in_window = [&](uint8_t s) {
        int diff = int(uint8_t(s - base));
        return 0 <= diff && diff < N;
    };

    auto send_or_resend = [&](uint8_t seq, bool is_resend) {
        if (!window.count(seq)) return;
        auto& slot = window[seq];
        chan.apply_delay();
        auto w = slot.wire;
        chan.flip_bits(w);
        if (!chan.maybe_drop()) send_all(conn, w.data(), w.size());
        slot.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(int(rtt.rto_ms));
        std::cout << "[SR SENDER] " << (is_resend ? "Resent" : "Sent") << " seq=" << int(seq) << "\n";
    };

    auto push_new = [&] {
        while (in_window(nextseq) && idx < payloads.size()) {
            Frame f;
            std::copy(src, src + 6, f.src);
            std::copy(dst, dst + 6, f.dst);
            f.length = uint16_t(std::min<size_t>(payloads[idx].size(), 1500));
            f.seq = nextseq;
            f.payload = payloads[idx];
            Slot slot;
            slot.in_use = true;
            slot.acked = false;
            slot.wire = f.serialize_with_crc();
            window[nextseq] = std::move(slot);
            send_or_resend(nextseq, false);
            ++idx;
            nextseq = uint8_t(nextseq + 1);
        }
    };

    push_new();

    while (!window.empty()) {
        u_long nb = 1;
        ioctlsocket(conn, FIONBIO, &nb);
        uint8_t ackbuf[6];
        int r = recv(conn, reinterpret_cast<char*>(ackbuf), sizeof(ackbuf), 0);
        nb = 0;
        ioctlsocket(conn, FIONBIO, &nb);

        if (r == sizeof(ackbuf)) {
            Ack a{};
            if (Ack::parse(ackbuf, sizeof(ackbuf), a)) {
                if (a.type == ACK) {
                    if (window.count(a.seq)) {
                        window[a.seq].acked = true;
                        std::cout << "[SR SENDER] ACK for " << int(a.seq) << "\n";
                        while (!window.empty() && window.begin()->second.acked) {
                            base = uint8_t(window.begin()->first + 1);
                            window.erase(window.begin());
                        }
                        push_new();
                    }
                } else if (a.type == NAK) {
                    if (window.count(a.seq)) {
                        std::cout << "[SR SENDER] NAK for " << int(a.seq) << " -> retransmit\n";
                        send_or_resend(a.seq, true);
                    }
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        for (auto& kv : window) {
            auto& seq = kv.first;
            auto& slot = kv.second;
            if (!slot.acked && now >= slot.deadline) {
                std::cout << "[SR SENDER] Timeout seq=" << int(seq) << " -> retransmit\n";
                rtt.rto_ms = std::min(4000.0, rtt.rto_ms * 1.5);
                send_or_resend(seq, true);
            }
        }
        Sleep(10);
    }

    std::cout << "[SR SENDER] All frames delivered.\n";
    closesocket(conn);
    closesocket(ls);
    winsock_cleanup();
    return 0;
}
