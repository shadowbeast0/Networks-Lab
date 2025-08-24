// client.cpp (Windows-safe: uses inet_addr / inet_ntoa fallback)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN


#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <cstring>

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include "common.h"
#include "error_injector.h"

using namespace std;

class Client {
    SOCKET sockfd{INVALID_SOCKET};
    sockaddr_in serv{};
    string ip;
    int port;

public:
    Client(const string& ip, int port) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
            cerr << "WSAStartup failed: " << WSAGetLastError() << "\n";
            exit(1);
        }

        this->ip = ip;
        this->port = port;

        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == INVALID_SOCKET) {
            cerr << "socket failed: " << WSAGetLastError() << "\n";
            exit(1);
        }

        memset(&serv, 0, sizeof(serv));
        serv.sin_family = AF_INET;
        serv.sin_port   = htons((u_short)port);

        // Portable on all Windows toolchains
        unsigned long a = inet_addr(ip.c_str());
        if (a == INADDR_NONE) {
            cerr << "invalid IPv4 address: " << ip << "\n";
            exit(1);
        }
        serv.sin_addr.s_addr = a;

        if (connect(sockfd, (sockaddr*)&serv, sizeof(serv)) == SOCKET_ERROR) {
            cerr << "connect failed: " << WSAGetLastError() << "\n";
            exit(1);
        }
    }

    ~Client() {
        if (sockfd != INVALID_SOCKET) closesocket(sockfd);
        WSACleanup();
    }

    static string read_bits_file(const string& path) {
        ifstream f(path);
        if (!f) {
            perror("open file");
            exit(1);
        }
        string all, line;
        while (getline(f, line)) all += line;
        return trim01(all);
    }

    static string make_codeword(const string& scheme, const string& bits) {
        if (scheme == "checksum16") return checksum16_append(bits);
        if (is_crc_scheme(scheme))   return crc_make_codeword(bits, crc_generators().at(scheme));
        cerr << "Unknown scheme: " << scheme << "\n";
        exit(1);
    }

    bool send_payload(const string& scheme, string& codeword,
                      bool injected, ErrorType etype, const string& data_len_bits) {
        ostringstream hdr;

        sockaddr_in client_addr{};
        int len = sizeof(client_addr);
        if (getsockname(this->sockfd, (sockaddr*)&client_addr, &len) == SOCKET_ERROR) {
            cerr << "getsockname failed: " << WSAGetLastError() << "\n";
            return false;
        }

        char client_ip[INET_ADDRSTRLEN]{};
        const char* cip = inet_ntoa(client_addr.sin_addr);
        if (cip) {
            strncpy(client_ip, cip, INET_ADDRSTRLEN - 1);
            client_ip[INET_ADDRSTRLEN - 1] = '\0';
        } else {
            strcpy(client_ip, "0.0.0.0");
        }
        int client_port = ntohs(client_addr.sin_port);

        hdr << "scheme=" << scheme
            << ";receiver_ip=" << this->ip
            << ";receiver_port=" << this->port
            << ";client_ip=" << client_ip
            << ";client_port=" << to_string(client_port)
            << ";error_type=" << (injected ? errorTypeName(etype) : "none")
            << ";data_len=" << data_len_bits
            << "\n";

        string header  = hdr.str();
        string payload = header + codeword;

        int n = send(sockfd, payload.c_str(), (int)payload.size(), 0);
        if (n == SOCKET_ERROR) {
            cerr << "send failed: " << WSAGetLastError() << "\n";
            exit(1);
        }
        cout << "[Client] Sent " << n << " bytes\n";
        cout << "[Client] Header: " << header;

        char buffer[1024] = {0};
        int valread = recv(sockfd, buffer, (int)sizeof(buffer), 0);
        if (valread > 0) {
            buffer[valread] = '\0';
            cout << "Received ACK from server: " << buffer << endl;
            string s(buffer);
            if (s.find("ACCEPT") != string::npos) return true;
        }
        return false;
    }
};

static void usage() {
    cerr << "Usage:\n"
            "  client.exe <server_ip> <port> <input_bits_file> --scheme <checksum16|crc8|crc10|crc16|crc32>\n"
            "            [--inject yes|no] [--inject-prob 0..1]\n\n";
}

int main(int argc, char** argv) {
    // if (argc < 7) { usage(); return 1; }

    string ip   = argv[1];
    int    port = stoi(argv[2]);
    string file = argv[3];

    string scheme;
    bool   inject = false;
    double inject_prob = 0.5;
    int    inject_scheme = -1;
    bool   random = false;

    for (int i = 4; i < argc; ++i) {
        string a = argv[i];
        if (a == "--scheme" && i + 1 < argc) scheme = argv[++i];
        if (a == "--inject" && i + 1 < argc) {
            string v = argv[++i];
            inject = (v == "yes" || v == "y" || v == "true" || v == "1");
        }
        if (a == "--inject-prob" && i + 1 < argc) {
            inject_prob = stod(argv[++i]);
            inject_prob = max(0.0, min(1.0, inject_prob));
        }
        if (a == "--injectscheme" && i + 1 < argc) inject_scheme = stoi(argv[++i]);
        if (a == "--random" && i + 1 < argc) {
            string v = argv[++i];
            inject = (v == "yes" || v == "y" || v == "true" || v == "1");
        }
    }

    if (scheme.empty()) { usage(); return 1; }
    if (scheme != "checksum16" && !is_crc_scheme(scheme)) {
        cerr << "Invalid scheme\n"; return 1;
    }

    if (random) {
        srand((unsigned)time(nullptr));
        while (true) {
            string data_bits = Client::read_bits_file(file);
            if (data_bits.empty()) { cerr << "Input has no bits 0/1\n"; return 1; }
            string codeword = Client::make_codeword(scheme, data_bits);

            ErrorInjector inj;
            ErrorType etype = ErrorType::BURST;
            codeword = inj.inject(codeword, etype);

            Client s(ip, port);
            if (s.send_payload(scheme, codeword, true, etype, to_string(data_bits.size()))) {
                cout << scheme << "\n" << codeword << "\n";
                break;
            }
        }
        return 0;
    }

    string data_bits = Client::read_bits_file(file);
    if (data_bits.empty()) { cerr << "Input has no bits 0/1\n"; return 1; }

    string codeword = Client::make_codeword(scheme, data_bits);

    ErrorInjector inj;
    bool actually_injected = false;
    ErrorType etype = static_cast<ErrorType>(inject_scheme);

    if (inject) {
        if (inject_scheme == -1) etype = inj.randomType();
        codeword = inj.inject(codeword, etype);
        actually_injected = true;
    }

    try {
        Client s(ip, port);
        s.send_payload(scheme, codeword, actually_injected, etype, to_string(data_bits.size()));
    } catch (...) {
        cerr << "Client failed\n"; return 1;
    }
    return 0;
}
