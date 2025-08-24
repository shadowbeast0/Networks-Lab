#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <cctype>
#include <cstdlib>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#include "common.h"

using namespace std;

#define PAYLOAD_SIZE 64

class Server
{
    SOCKET listenfd{INVALID_SOCKET};
    sockaddr_in addr{};

public:
    Server(int port)
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            cerr << "WSAStartup failed: " << WSAGetLastError() << "\n";
            exit(1);
        }

        listenfd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenfd == INVALID_SOCKET) {
            cerr << "socket failed: " << WSAGetLastError() << "\n";
            exit(1);
        }

        int opt = 1;
        if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
            cerr << "setsockopt failed: " << WSAGetLastError() << "\n";
            exit(1);
        }

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((u_short)port);

        if (bind(listenfd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            cerr << "bind failed: " << WSAGetLastError() << "\n";
            exit(1);
        }
        if (listen(listenfd, 1) == SOCKET_ERROR) {
            cerr << "listen failed: " << WSAGetLastError() << "\n";
            exit(1);
        }
        cout << "[Server] Listening on port " << port << " ...\n";
    }

    ~Server()
    {
        if (listenfd != INVALID_SOCKET) closesocket(listenfd);
        WSACleanup();
    }

    void serve_once()
    {
        sockaddr_in cli{};
        int clen = sizeof(cli);
        SOCKET fd = accept(listenfd, (sockaddr*)&cli, &clen);
        if (fd == INVALID_SOCKET) {
            cerr << "accept failed: " << WSAGetLastError() << "\n";
            return;
        }
        cout << "[Server] Client connected.\n";

        string buf;
        char tmp[8 * PAYLOAD_SIZE];
        int n;
        while ((n = recv(fd, tmp, (int)sizeof(tmp), 0)) > 0) {
            buf.append(tmp, tmp + n);
            if (n < (int)sizeof(tmp)) break;
        }
        if (n == SOCKET_ERROR) {
            cerr << "recv failed: " << WSAGetLastError() << "\n";
        }

        size_t nl = buf.find('\n');
        if (nl == string::npos) {
            cerr << "Malformed payload (no header newline)\n";
            closesocket(fd);
            return;
        }
        string header = buf.substr(0, nl);
        string body = trim01(buf.substr(nl + 1));

        unordered_map<string, string> H;
        {
            stringstream ss(header);
            string kv;
            while (getline(ss, kv, ';')) {
                auto p = kv.find('=');
                if (p != string::npos) {
                    string k = kv.substr(0, p);
                    string v = kv.substr(p + 1);
                    auto trim = [](string& x) {
                        while (!x.empty() && isspace((unsigned char)x.back())) x.pop_back();
                        size_t i = 0;
                        while (i < x.size() && isspace((unsigned char)x[i])) ++i;
                        x = x.substr(i);
                    };
                    trim(k);
                    trim(v);
                    H[k] = v;
                }
            }
        }

        string scheme = H.count("scheme") ? H["scheme"] : "";
        string client_ip = H.count("client_ip") ? H["client_ip"] : "";
        string etype = H.count("error_type") ? H["error_type"] : "none";
        string data_len = H.count("data_len") ? H["data_len"] : "?";

        cout << "[Server] Header: " << header << "\n";
        cout << "[Server] Body bits length: " << body.size() << "\n";
        cout << "[Server] Client ip: " << client_ip << "\n";
        bool ok = false;
        if (scheme == "checksum16") {
            ok = checksum16_verify(body);
        } else if (is_crc_scheme(scheme)) {
            ok = crc_verify_codeword(body, crc_generators().at(scheme));
        } else {
            cerr << "[Server] Unknown scheme.\n";
        }

        cout << "[Server] Validation: " << (ok ? "ACCEPT (no error detected)" : "REJECT (error detected)") << "\n";
        cout << "[Server] Meta:error_type=" << etype << "\n";
        string ack = (ok ? "ACCEPT (no error detected)" : "REJECT (error detected)");
        send(fd, ack.c_str(), (int)ack.size(), 0);
        cout << "\nACK sent\n";
        closesocket(fd);
    }
};

static void usage()
{
    cerr << "Usage: Server.exe <port>\n";
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        usage();
        return 1;
    }
    int port = stoi(argv[1]);
    Server r(port);
    while (true) r.serve_once();
    return 0;
}
