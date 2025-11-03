#include <bits/stdc++.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "common.h"
using namespace std;

constexpr int PORT = 9090;
string SERVER_IP = "127.0.0.1";

int main()
{
    int n_clients, frames;
    cout << "Enter number of clients: ";
    cin >> n_clients;
    cout << "Frames per client: ";
    cin >> frames;

    ifstream fin("msg.bits");
    vector<string> msgs;
    string line;
    while (getline(fin, line))
        if (!line.empty())
            msgs.push_back(line);
    fin.close();
    if (msgs.empty())
    {
        cerr << "msg.bits empty!\n";
        return 1;
    }

    mutex cout_mtx;
    atomic<long long> bits_sent{0}, bits_acked{0};

    auto worker = [&](int id)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in serv{};
        serv.sin_family = AF_INET;
        serv.sin_port = htons(PORT);
        inet_pton(AF_INET, SERVER_IP.c_str(), &serv.sin_addr);
        if (connect(sock, (sockaddr *)&serv, sizeof(serv)) < 0)
        {
            lock_guard<mutex> lk(cout_mtx);
            cerr << "[C" << id << "] connect failed\n";
            return;
        }

        
        string hello;
        hello.reserve(64);
        char tmp[64];
        while (true)
        {
            int n = recv(sock, tmp, sizeof(tmp), 0);
            if (n <= 0)
            {
                lock_guard<mutex> lk(cout_mtx);
                cerr << "[C" << id << "] no CODE line\n";
                close(sock);
                return;
            }
            hello.append(tmp, tmp + n);
            if (hello.find('\n') != string::npos)
                break;
        }
        int code_idx = -1, L = -1;
        sscanf(hello.c_str(), "CODE %d %d", &code_idx, &L);
        auto H = walsh(L);
        const auto &code = H[code_idx];

        {
            lock_guard<mutex> lk(cout_mtx);
            cout << "[C" << id << "] assigned code " << code_idx << " (L=" << L << ")\n";
        }

        for (int f = 0; f < frames; ++f)
        {
            const string &bits = msgs[f % msgs.size()];
            for (char bch : bits)
            {
                int bit = (bch == '1') ? 1 : 0;
                auto chips = encode_bit(bit, code);
                string wire = chips_to_wire(chips);

                
                {
                    lock_guard<mutex> lk(cout_mtx);
                    cout << "[C" << id << "] send bit=" << bit << " chips=" << wire << "\n";
                }

                
                const char *p = wire.data();
                int left = (int)wire.size();
                while (left > 0)
                {
                    int n = send(sock, p, left, 0);
                    if (n <= 0)
                    {
                        close(sock);
                        return;
                    }
                    p += n;
                    left -= n;
                }
                bits_sent++;

                
                char abuf[16];
                int n = recv(sock, abuf, sizeof(abuf), 0);
                if (n > 0)
                {
                    string s(abuf, abuf + n);
                    
                    int rec_bit = -1;
                    if (s.size() >= 5 && s.rfind("ACK ", 0) == 0)
                    {
                        rec_bit = (s[4] == '1') ? 1 : 0;
                    }
                    bits_acked++;
                    {
                        lock_guard<mutex> lk(cout_mtx);
                        cout << "[C" << id << "] ACK recv decoded_bit=" << (rec_bit == -1 ? '?' : ('0' + rec_bit)) << "\n";
                    }
                }
            }
        }
        close(sock);
    };

    vector<thread> th;
    for (int i = 0; i < n_clients; i++)
        th.emplace_back(worker, i);
    for (auto &t : th)
        t.join();

    cout << "\n--------------Sender Result--------------\n";
    cout << "Bits that were sent:  " << bits_sent.load() << "\n";
    cout << "Bits in which ACK received: " << bits_acked.load() << "\n";
    return 0;
}
