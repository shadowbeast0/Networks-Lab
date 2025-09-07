#include "llc_common.h"
using namespace llc;

static const uint16_t PORT = 8000;

int main(int argc, char** argv) {
    winsock_init();

    double p_err = (argc >= 2 ? std::stod(argv[1]) : 0.0);
    int max_delay = (argc >= 3 ? std::stoi(argv[2]) : 0);
    Channel chan{p_err, max_delay, 0.0};

    SOCKET s = make_connect_socket("127.0.0.1", PORT);
    std::cout << "[RECV] Connected to sender (Stop&Wait)\n";

    uint8_t expected = 0;
    std::vector<uint8_t> buf(15 + MIN_PAYLOAD + 4 + 2048);

    while (true) {
        int timeout_ms = 60000;
        if (!recv_exact(s, buf.data(), 15 + MIN_PAYLOAD + 4, timeout_ms)) {
            std::cout << "[RECV] Connection closing or no more data.\n";
            break;
        }

        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        int r;
        size_t have = 15 + MIN_PAYLOAD + 4;
        do {
            r = recv(s, reinterpret_cast<char*>(buf.data() + have), int(buf.size() - have), 0);
            if (r > 0) have += size_t(r);
        } while (r > 0);
        nb = 0;
        ioctlsocket(s, FIONBIO, &nb);
        buf.resize(have);

        bool ok_crc = Frame::verify_crc(buf);
        Frame f{};
        bool parsed = Frame::parse(buf, f);

        if (!parsed) {
            std::cout << "[RECV] Bad frame parse -> drop\n";
            continue;
        }
        std::cout << "[RECV] Frame seq=" << int(f.seq)
                  << " len=" << f.length
                  << " (got " << have << " bytes), CRC=" << (ok_crc ? "OK" : "BAD") << "\n";

        if (ok_crc && f.seq == expected) {
            expected = uint8_t(expected + 1);
            Ack a{ACK, f.seq};
            auto wire = a.serialize();
            chan.apply_delay();
            chan.flip_bits(wire);
            if (!chan.maybe_drop()) send_all(s, wire.data(), wire.size());
            std::cout << "[RECV] ACK sent for " << int(f.seq) << "\n";
        } else {
            std::cout << "[RECV] Discarded (crc/seq mismatch). No ACK -> sender will timeout.\n";
        }

        buf.assign(buf.size(), 0);
        buf.resize(15 + MIN_PAYLOAD + 4 + 2048);
    }

    closesocket(s);
    winsock_cleanup();
    return 0;
}
