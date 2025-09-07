#include "llc_common.h"
using namespace llc;

static const uint16_t PORT = 8000;
static const size_t MAX_FRAME = 15 + MIN_PAYLOAD + 4 + 1500;

bool read_payload(std::ifstream& in, std::vector<uint8_t>& out) {
    std::string line;
    if (!std::getline(in, line)) return false;
    out.assign(line.begin(), line.end());
    if (out.size() < MIN_PAYLOAD) out.resize(MIN_PAYLOAD, uint8_t(' '));
    return true;
}

int main(int argc, char** argv) {
    winsock_init();

    double p_err = (argc >= 2 ? std::stod(argv[1]) : 0.0);
    int max_delay = (argc >= 3 ? std::stoi(argv[2]) : 0);
    Channel chan{p_err, max_delay, 0.0};

    SOCKET ls = make_listen_socket(PORT);
    std::cout << "[SENDER] Listening on " << PORT << " (Stop&Wait)\n";

    SOCKET conn = accept(ls, nullptr, nullptr);
    if (conn == INVALID_SOCKET) { std::cerr << "accept() failed\n"; return 1; }
    std::cout << "[SENDER] Connection established.\n";

    std::ifstream in("data.txt", std::ios::binary);
    if (!in) { std::cerr << "Cannot open data.txt\n"; return 1; }

    uint8_t src[6], dst[6];
    random_mac(src);
    random_mac(dst);
    uint8_t seq = 0;
    RttEstimator rtt;

    while (true) {
        std::vector<uint8_t> payload;
        if (!read_payload(in, payload)) { std::cout << "[SENDER] No more data.\n"; break; }

        Frame f;
        std::copy(src, src + 6, f.src);
        std::copy(dst, dst + 6, f.dst);
        f.length = uint16_t(std::min<size_t>(payload.size(), 1500));
        f.seq = seq;
        f.payload = payload;

        auto wire = f.serialize_with_crc();
        bool acked = false;
        while (!acked) {
            chan.apply_delay();
            auto tx = wire;
            chan.flip_bits(tx);
            if (chan.maybe_drop()) {
                std::cout << "[SENDER] (Simulated drop) frame seq=" << int(seq) << "\n";
            } else {
                if (!send_all(conn, tx.data(), tx.size())) { std::cerr << "send failed\n"; return 1; }
                std::cout << "[SENDER] Sent frame seq=" << int(seq) << ", len=" << tx.size() << "\n";
            }

            auto t0 = std::chrono::steady_clock::now();
            uint8_t ackbuf[6];
            if (recv_exact(conn, ackbuf, sizeof(ackbuf), int(rtt.rto_ms))) {
                Ack a{};
                if (Ack::parse(ackbuf, sizeof(ackbuf), a) && a.type == ACK && a.seq == seq) {
                    auto t1 = std::chrono::steady_clock::now();
                    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                    rtt.observe(ms);
                    std::cout << "[SENDER] ACK " << int(a.seq)
                              << " (RTT=" << ms << "ms, RTO=" << rtt.rto_ms << "ms)\n";
                    acked = true;
                    seq = uint8_t(seq + 1);
                    break;
                } else {
                    std::cout << "[SENDER] Bad ACK/NAK; retransmitting\n";
                }
            } else {
                std::cout << "[SENDER] Timeout; retransmitting seq=" << int(seq)
                          << " (RTO=" << rtt.rto_ms << "ms)\n";
            }
        }
    }

    closesocket(conn);
    closesocket(ls);
    winsock_cleanup();
    return 0;
}
