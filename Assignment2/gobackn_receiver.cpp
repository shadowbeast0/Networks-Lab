#include "llc_common.h"
using namespace llc;

static const uint16_t PORT = 8000;

int main(int argc, char** argv) {
    winsock_init();
    double p_err = (argc >= 2 ? std::stod(argv[1]) : 0.0);  // errors on ACK
    int max_delay = (argc >= 3 ? std::stoi(argv[2]) : 0);
    Channel chan{p_err, max_delay, 0.0};

    SOCKET s = make_connect_socket("127.0.0.1", PORT);
    std::cout << "[GBN RECV] Connected (window=1)\n";

    uint8_t expected = 0;
    std::vector<uint8_t> buf(15 + MIN_PAYLOAD + 4 + 2048);

    while (true) {
        if (!recv_exact(s, buf.data(), 15 + MIN_PAYLOAD + 4, 60000)) {
            std::cout << "[GBN RECV] No more data / closing.\n"; break;
        }
        // non-blocking slurp rest
        u_long nb=1; ioctlsocket(s, FIONBIO, &nb);
        int r; size_t have = 15 + MIN_PAYLOAD + 4;
        do {
            r = recv(s, reinterpret_cast<char*>(buf.data()+have), int(buf.size()-have), 0);
            if (r > 0) have += size_t(r);
        } while (r > 0);
        nb=0; ioctlsocket(s, FIONBIO, &nb);
        buf.resize(have);

        bool ok = Frame::verify_crc(buf);
        Frame f{}; Frame::parse(buf, f);
        std::cout << "[GBN RECV] seq="<<int(f.seq)<<" CRC="<<(ok?"OK":"BAD")
                  <<" expected="<<int(expected)<<"\n";

        if (ok && f.seq == expected) {
            expected = uint8_t(expected + 1);
        } else {
            std::cout << "  out-of-order or corrupted -> discard\n";
        }
        // send cumulative ACK = next expected (as in your Python) :contentReference[oaicite:9]{index=9}
        Ack a{ACK, expected};
        auto w = a.serialize();
        chan.apply_delay(); chan.flip_bits(w);
        if (!chan.maybe_drop()) send_all(s, w.data(), w.size());
        std::cout << "[GBN RECV] Sent cumulative ACK="<<int(expected)<<"\n";

        buf.assign(buf.size(), 0);
        buf.resize(15 + MIN_PAYLOAD + 4 + 2048);
    }

    closesocket(s); winsock_cleanup(); return 0;
}
