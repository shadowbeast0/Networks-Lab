// gobackn_sender.cpp — FIXED
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
    int N = (argc >= 2 ? std::stoi(argv[1]) : 4);          // window size
    double p_err = (argc >= 3 ? std::stod(argv[2]) : 0.0); // bit error prob
    int max_delay = (argc >= 4 ? std::stoi(argv[3]) : 0);
    Channel chan{p_err, max_delay, 0.0};

    SOCKET ls = make_listen_socket(PORT);
    std::cout << "[GBN SENDER] Listening on " << PORT << " (N="<<N<<")\n";
    SOCKET conn = accept(ls, nullptr, nullptr);
    if (conn == INVALID_SOCKET) { std::cerr << "accept() failed\n"; return 1; }
    std::cout << "[GBN SENDER] Connection established.\n";

    std::vector<std::vector<uint8_t>> payloads;
    if (!read_payloads("data.txt", payloads) || payloads.empty()) {
        std::cerr << "No data in data.txt\n"; return 1;
    }

    uint8_t src[6], dst[6]; random_mac(src); random_mac(dst);
    RttEstimator rtt; // initial RTO ~1000ms

    uint8_t base = 0;     // oldest unacked seq
    uint8_t nextseq = 0;  // next seq to use for sending
    size_t idx = 0;       // how many payloads have been queued/sent
    std::map<uint8_t, std::vector<uint8_t>> frame_cache; // seq -> serialized frame

    auto send_frame = [&](uint8_t seq, const std::vector<uint8_t>& payload){
        Frame f; std::copy(src,src+6,f.src); std::copy(dst,dst+6,f.dst);
        f.length = uint16_t(std::min<size_t>(payload.size(), 1500));
        f.seq = seq; f.payload = payload;
        auto w = f.serialize_with_crc();
        chan.apply_delay(); chan.flip_bits(w);
        if (!chan.maybe_drop()) send_all(conn, w.data(), w.size());
        std::cout << "[GBN SENDER] Sent seq=" << int(seq) << "\n";
        frame_cache[seq] = std::move(w);
    };

    auto in_window = [&](uint8_t s){
        int diff = int(uint8_t(s - base));
        return 0 <= diff && diff < N;
    };

    // Loop until ALL data has been sent (idx == payloads.size())
    // AND all outstanding data has been ACKed (base == idx).
    while ( (base != uint8_t(idx)) || (idx < payloads.size()) ) {
        // Fill the window with new frames if possible
        while (in_window(nextseq) && idx < payloads.size()) {
            send_frame(nextseq, payloads[idx]);
            ++idx;
            nextseq = uint8_t(nextseq + 1);
        }

        // Wait for cumulative ACK with a timeout based on the oldest unacked frame (RTO)
        uint8_t ackbuf[6];
        if (recv_exact(conn, ackbuf, sizeof(ackbuf), int(rtt.rto_ms))) {
            Ack a{};
            if (Ack::parse(ackbuf, sizeof(ackbuf), a) && a.type==ACK) {
                // Receiver sends cumulative ACK = next expected sequence
                // Accept only if it moves base forward within window (ignore stale/out-of-range)
                int adv = int(uint8_t(a.seq - base));
                if (adv > 0 && adv <= N) {
                    rtt.observe(std::max(50.0, rtt.rto_ms * 0.75)); // coarse sample to keep RTO reasonable
                    std::cout << "[GBN SENDER] Cumulative ACK="<< int(a.seq)
                              << " base:"<<int(base)<<"->"<<int(a.seq)
                              << " (RTO="<< rtt.rto_ms <<"ms)\n";
                    // Drop acked frames from cache: all seq < a.seq
                    uint8_t new_base = a.seq;
                    for (auto it = frame_cache.begin(); it != frame_cache.end();) {
                        int d = int(uint8_t(it->first - new_base));
                        if (d < 0) it = frame_cache.erase(it); else ++it;
                    }
                    base = new_base;
                } else {
                    // stale or weird ACK; ignore
                    std::cout << "[GBN SENDER] Stale/out-of-range ACK="<<int(a.seq)<<"\n";
                }
            } else {
                std::cout << "[GBN SENDER] Bad ACK ignored.\n";
            }
        } else {
            // Timeout -> resend entire window [base, nextseq)
            std::cout << "[GBN SENDER] TIMEOUT, resending window ["<<int(base)<<","<<int(nextseq)<<")\n";
            uint8_t s = base;
            while (s != nextseq) {
                auto it = frame_cache.find(s);
                if (it != frame_cache.end()) {
                    auto w2 = it->second;
                    chan.apply_delay(); chan.flip_bits(w2);
                    if (!chan.maybe_drop()) send_all(conn, w2.data(), w2.size());
                    std::cout << "  resend seq="<<int(s)<<"\n";
                }
                s = uint8_t(s + 1);
            }
            rtt.rto_ms = std::min(4000.0, rtt.rto_ms * 2.0); // backoff on timeout
        }
    }

    std::cout << "[GBN SENDER] Done.\n";
    closesocket(conn);
    closesocket(ls);
    winsock_cleanup();
    return 0;
}
