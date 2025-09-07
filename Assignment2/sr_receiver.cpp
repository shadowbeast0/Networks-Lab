#include "llc_common.h"
using namespace llc;

static const uint16_t PORT = 8000;

int main(int argc, char** argv) {
    winsock_init();
    int N = (argc >= 2 ? std::stoi(argv[1]) : 4);             // window size
    double p_err = (argc >= 3 ? std::stod(argv[2]) : 0.0);    // errors on ACK/NAK
    int max_delay = (argc >= 4 ? std::stoi(argv[3]) : 0);
    Channel chan{p_err, max_delay, 0.0};

    SOCKET s = make_connect_socket("127.0.0.1", PORT);
    std::cout << "[SR RECV] Connected (N="<<N<<")\n";

    uint8_t base = 0; // next expected in-order
    std::map<uint8_t, Frame> buffer;

    std::vector<uint8_t> buf(15 + MIN_PAYLOAD + 4 + 2048);
    while (true) {
        if (!recv_exact(s, buf.data(), 15 + MIN_PAYLOAD + 4, 60000)) {
            std::cout << "[SR RECV] Closing.\n"; break;
        }
        // non-blocking slurp rest
        u_long nb=1; ioctlsocket(s, FIONBIO, &nb);
        int r; size_t have=15+MIN_PAYLOAD+4;
        do{
            r = recv(s, reinterpret_cast<char*>(buf.data()+have), int(buf.size()-have), 0);
            if (r>0) have+=size_t(r);
        } while (r>0);
        nb=0; ioctlsocket(s, FIONBIO, &nb);
        buf.resize(have);

        bool ok = Frame::verify_crc(buf);
        Frame f{};
        if (!Frame::parse(buf, f)) continue;

        std::cout << "[SR RECV] seq="<<int(f.seq)<<" CRC="<<(ok?"OK":"BAD")
                  <<" base="<<int(base)<<"\n";

        if (!ok) {
            // NAK next expected (assignment allows NAK) :contentReference[oaicite:10]{index=10}
            Ack n{NAK, base}; auto w = n.serialize();
            chan.apply_delay(); chan.flip_bits(w);
            if (!chan.maybe_drop()) send_all(s, w.data(), w.size());
            std::cout << "  -> NAK "<<int(base)<<"\n";
            continue;
        }

        // check window membership
        int diff = int(uint8_t(f.seq - base));
        if (diff < 0 || diff >= N) {
            std::cout << "  out of window -> drop (or could send NAK)\n";
            continue;
        }

        // accept and buffer
        buffer[f.seq] = f;
        // ACK this specific frame
        Ack a{ACK, f.seq}; auto w = a.serialize();
        chan.apply_delay(); chan.flip_bits(w);
        if (!chan.maybe_drop()) send_all(s, w.data(), w.size());
        std::cout << "  -> ACK "<<int(f.seq)<<"\n";

        // deliver in order
        while (buffer.count(base)) {
            // (deliver payload if needed)
            buffer.erase(base);
            base = uint8_t(base + 1);
        }

        buf.assign(buf.size(), 0);
        buf.resize(15 + MIN_PAYLOAD + 4 + 2048);
    }

    closesocket(s); winsock_cleanup(); return 0;
}
