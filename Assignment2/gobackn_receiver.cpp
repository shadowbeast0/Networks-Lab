#include "llc_common.h"
using namespace llc;

static const uint16_t PORT = 8000;

int main(int argc, char** argv) {
    winsock_init();

    double p_err = (argc >= 2 ? std::stod(argv[1]) : 0.0);
    int max_delay = (argc >= 3 ? std::stoi(argv[2]) : 0);
    Channel chan{p_err, max_delay, 0.0};

    SOCKET s = make_connect_socket("127.0.0.1", PORT);
    std::cout << "[GBN RECV] Connected (window=1)\n";

    uint8_t expected = 0;
    std::vector<uint8_t> buf(15 + MIN_PAYLOAD + 4 + 2048);

    while (true) {
        if (!recv_exact(s, buf.data(), 15 + MIN_PAYLOAD + 4, 60000)) {
            std::cout << "[GBN RECV] No more data / closing.\n";
            break;
        }

        size_t have = 15 + MIN_PAYLOAD + 4;
        uint16_t be_len = (uint16_t(buf[12]) << 8) | uint16_t(buf[13]);
        size_t payload_len = std::max<size_t>(MIN_PAYLOAD, ntohs(be_len));
        size_t total = 15 + payload_len + 4;

        if (have < total) {
            int tail_timeout = std::max(1000, 5 * max_delay + 500);
            if (!recv_exact(s, buf.data() + have, total - have, tail_timeout)) {
                std::cout << "[GBN RECV] Incomplete frame.\n";
                buf.assign(buf.size(), 0);
                buf.resize(15 + MIN_PAYLOAD + 4 + 2048);
                continue;
            }
            have = total;
        }
        buf.resize(total);

        bool ok = Frame::verify_crc(buf);
        Frame f{};
        Frame::parse(buf, f);

        std::cout << "[GBN RECV] seq=" << int(f.seq)
                  << " CRC=" << (ok ? "OK" : "BAD")
                  << " expected=" << int(expected) << "\n";

        if (ok && f.seq == expected) {
            expected = uint8_t(expected + 1);
        } else {
            std::cout << "  out-of-order or corrupted -> discard\n";
        }

        Ack a{ACK, expected};
        auto w = a.serialize();
        chan.apply_delay();
        chan.flip_bits(w);
        if (!chan.maybe_drop()) send_all(s, w.data(), w.size());
        std::cout << "[GBN RECV] Sent cumulative ACK=" << int(expected) << "\n";

        buf.assign(buf.size(), 0);
        buf.resize(15 + MIN_PAYLOAD + 4 + 2048);
    }

    closesocket(s);
    winsock_cleanup();
    return 0;
}
