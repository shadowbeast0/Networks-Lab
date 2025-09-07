#include "llc_common.h"
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
    std::cout << "[GBN SENDER] Listening on " << PORT << " (N=" << N << ")\n";
    SOCKET conn = accept(ls, nullptr, nullptr);
    if (conn == INVALID_SOCKET) { std::cerr << "accept() failed\n"; return 1; }
    std::cout << "[GBN SENDER] Connection established.\n";

    std::vector<std::vector<uint8_t>> payloads;
    if (!read_payloads("data.txt", payloads) || payloads.empty()) {
        std::cerr << "No data in data.txt\n"; return 1;
    }

    uint8_t src[6], dst[6];
    random_mac(src);
    random_mac(dst);
    RttEstimator rtt;

    uint8_t base = 0;
    uint8_t nextseq = 0;
    size_t idx = 0;
    std::map<uint8_t, std::vector<uint8_t>> frame_cache;

    auto send_frame = [&](uint8_t seq, const std::vector<uint8_t>& payload) {
        Frame f;
        std::copy(src, src + 6, f.src);
        std::copy(dst, dst + 6, f.dst);
        f.length = uint16_t(std::min<size_t>(payload.size(), 1500));
        f.seq = seq;
        f.payload = payload;
        auto w_clean = f.serialize_with_crc();
        frame_cache[seq] = w_clean;
        auto w = w_clean;
        chan.apply_delay();
        chan.flip_bits(w);
        if (!chan.maybe_drop()) send_all(conn, w.data(), w.size());
        std::cout << "[GBN SENDER] Sent seq=" << int(seq) << "\n";
    };

    auto in_window = [&](uint8_t s) {
        int diff = int(uint8_t(s - base));
        return 0 <= diff && diff < N;
    };

    while ((base != uint8_t(idx)) || (idx < payloads.size())) {
        while (in_window(nextseq) && idx < payloads.size()) {
            send_frame(nextseq, payloads[idx]);
            ++idx;
            nextseq = uint8_t(nextseq + 1);
        }

        uint8_t ackbuf[6];
        if (recv_exact(conn, ackbuf, sizeof(ackbuf), int(rtt.rto_ms))) {
            Ack a{};
            if (Ack::parse(ackbuf, sizeof(ackbuf), a) && a.type == ACK) {
                int adv = int(uint8_t(a.seq - base));
                if (adv > 0 && adv <= N) {
                    rtt.observe(std::max(50.0, rtt.rto_ms * 0.75));
                    std::cout << "[GBN SENDER] Cumulative ACK=" << int(a.seq)
                              << " base:" << int(base) << "->" << int(a.seq)
                              << " (RTO=" << rtt.rto_ms << "ms)\n";
                    uint8_t new_base = a.seq;
                    for (auto it = frame_cache.begin(); it != frame_cache.end();) {
                        int d = int(uint8_t(it->first - new_base));
                        if (d < 0) it = frame_cache.erase(it); else ++it;
                    }
                    base = new_base;
                } else {
                    std::cout << "[GBN SENDER] Stale/out-of-range ACK=" << int(a.seq) << "\n";
                }
            } else {
                std::cout << "[GBN SENDER] Bad ACK ignored.\n";
            }
        } else {
            std::cout << "[GBN SENDER] TIMEOUT, resending window [" << int(base) << "," << int(nextseq) << ")\n";
            uint8_t s = base;
            while (s != nextseq) {
                auto it = frame_cache.find(s);
                if (it != frame_cache.end()) {
                    auto w2 = it->second;
                    chan.apply_delay();
                    chan.flip_bits(w2);
                    if (!chan.maybe_drop()) send_all(conn, w2.data(), w2.size());
                    std::cout << "  resend seq=" << int(s) << "\n";
                }
                s = uint8_t(s + 1);
            }
            rtt.rto_ms = std::min(4000.0, rtt.rto_ms * 2.0);
        }
    }

    std::cout << "[GBN SENDER] Done.\n";
    closesocket(conn);
    closesocket(ls);
    winsock_cleanup();
    return 0;
}
