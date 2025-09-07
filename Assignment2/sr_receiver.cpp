#include "llc_common.h"
using namespace llc;

static const uint16_t PORT = 8000;

int main(int argc, char** argv) {
    winsock_init();

    int N = (argc >= 2 ? std::stoi(argv[1]) : 4);
    double p_err = (argc >= 3 ? std::stod(argv[2]) : 0.0);
    int max_delay = (argc >= 4 ? std::stoi(argv[3]) : 0);
    Channel chan{p_err, max_delay, 0.0};

    SOCKET s = make_connect_socket("127.0.0.1", PORT);
    std::cout << "[SR RECV] Connected (N=" << N << ")\n";

    uint8_t base = 0;
    std::map<uint8_t, Frame> buffer;

    std::vector<uint8_t> buf(15 + MIN_PAYLOAD + 4 + 2048);
    while (true) {
        if (!recv_exact(s, buf.data(), 15 + MIN_PAYLOAD + 4, 60000)) {
            std::cout << "[SR RECV] Closing.\n";
            break;
        }

        size_t have = 15 + MIN_PAYLOAD + 4;
        uint16_t be_len = (uint16_t(buf[12]) << 8) | uint16_t(buf[13]);
        size_t payload_len = std::max<size_t>(MIN_PAYLOAD, ntohs(be_len));
        size_t total = 15 + payload_len + 4;

        if (have < total) {
            int tail_timeout = std::max(1000, 5 * max_delay + 500);
            if (!recv_exact(s, buf.data() + have, total - have, tail_timeout)) {
                std::cout << "[SR RECV] Incomplete frame.\n";
                buf.assign(buf.size(), 0);
                buf.resize(15 + MIN_PAYLOAD + 4 + 2048);
                continue;
            }
            have = total;
        }
        buf.resize(total);

        bool ok = Frame::verify_crc(buf);
        Frame f{};
        if (!Frame::parse(buf, f)) {
            buf.assign(buf.size(), 0);
            buf.resize(15 + MIN_PAYLOAD + 4 + 2048);
            continue;
        }

        std::cout << "[SR RECV] seq=" << int(f.seq)
                  << " CRC=" << (ok ? "OK" : "BAD")
                  << " base=" << int(base) << "\n";

        if (!ok) {
            Ack n{NAK, base};
            auto w = n.serialize();
            chan.apply_delay();
            chan.flip_bits(w);
            if (!chan.maybe_drop()) send_all(s, w.data(), w.size());
            std::cout << "  -> NAK " << int(base) << "\n";
            buf.assign(buf.size(), 0);
            buf.resize(15 + MIN_PAYLOAD + 4 + 2048);
            continue;
        }

        int diff = int(uint8_t(f.seq - base));
        if (diff < 0) {
            Ack a{ACK, f.seq};
            auto w = a.serialize();
            chan.apply_delay();
            chan.flip_bits(w);
            if (!chan.maybe_drop()) send_all(s, w.data(), w.size());
            std::cout << "  -> ACK " << int(f.seq) << "\n";
            buf.assign(buf.size(), 0);
            buf.resize(15 + MIN_PAYLOAD + 4 + 2048);
            continue;
        }
        if (diff >= N) {
            std::cout << "  out of window -> drop\n";
            buf.assign(buf.size(), 0);
            buf.resize(15 + MIN_PAYLOAD + 4 + 2048);
            continue;
        }

        buffer[f.seq] = f;

        Ack a{ACK, f.seq};
        auto w = a.serialize();
        chan.apply_delay();
        chan.flip_bits(w);
        if (!chan.maybe_drop()) send_all(s, w.data(), w.size());
        std::cout << "  -> ACK " << int(f.seq) << "\n";

        while (buffer.count(base)) {
            buffer.erase(base);
            base = uint8_t(base + 1);
        }

        buf.assign(buf.size(), 0);
        buf.resize(15 + MIN_PAYLOAD + 4 + 2048);
    }

    closesocket(s);
    winsock_cleanup();
    return 0;
}
