#include <fstream>
#include <sys/stat.h>
#include <ctime>
#include <iomanip>
#include "common.h"
#include <bits/stdc++.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <random>
#include <chrono>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
using namespace std;

static constexpr int PORT = 5000;

atomic<long long> total_successful_bits{0};
atomic<long long> total_successful_frames{0};
atomic<long long> total_delay_us{0};
mutex stats_mtx;
double P_persistent = 0.5;
int slot_ms = 5;
int ack_timeout_ms = 200;
int max_BEB_k = 6;
double collisionProb = 0.1;

random_device rd;
mt19937 rng(rd());
uniform_real_distribution<double> prob_dist(0.0, 1.0);

bool channel_busy(int sockfd)
{
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(sockfd, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 1000; 
    int ret = select(sockfd + 1, &rfds, nullptr, nullptr, &tv);
    return (ret > 0); 
}

void client_worker(int id, string server_ip, int frames_per_client)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        cerr << "Client " << id << " socket failed\n";
        return;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        cerr << "Client " << id << " connection failed\n";
        return;
    }

    cout << "Client " << id << " has connected\n";

    ifstream fin("msg.bits");
    string bits;
    getline(fin, bits);
    if (bits.empty())
        bits = "1011001010101110";
    fin.close();

    int frame_size = (int)bits.size();
    vector<string> frames(frames_per_client, bits);

    for (int f = 0; f < frames_per_client; ++f)
    {
        int attempt = 0;
        auto start_tx = chrono::steady_clock::now();

        while (true)
        {
            if (channel_busy(sock))
            {
                this_thread::sleep_for(chrono::milliseconds(slot_ms));
                continue;
            }

            if (prob_dist(rng) > P_persistent)
            {
                this_thread::sleep_for(chrono::milliseconds(slot_ms));
                continue;
            }

            bool local_collision = (prob_dist(rng) < collisionProb);

            string frame = frames[f];
            send(sock, frame.c_str(), (int)frame.size(), 0);

            if (local_collision)
            {
                cerr << "[Client " << id << "] has purposely injected collision\n";
                attempt++;
                int backoff_slots = rand() % (1 << min(attempt, max_BEB_k));
                this_thread::sleep_for(chrono::milliseconds(slot_ms * backoff_slots));
                continue;
            }

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);
            struct timeval tv;
            tv.tv_sec = ack_timeout_ms / 1000;
            tv.tv_usec = (ack_timeout_ms % 1000) * 1000;

            int ret = select(sock + 1, &rfds, nullptr, nullptr, &tv);
            if (ret > 0 && FD_ISSET(sock, &rfds))
            {
                char buffer[16];
                int n = recv(sock, buffer, sizeof(buffer), 0);
                if (n > 0 && strncmp(buffer, "ACK", 3) == 0)
                {
                    auto end_tx = chrono::steady_clock::now();
                    long long delay_us =
                        chrono::duration_cast<chrono::microseconds>(end_tx - start_tx).count();
                    total_delay_us += delay_us;
                    total_successful_frames++;
                    total_successful_bits += frame_size;
                    break;
                }
            }

            attempt++;
            int backoff_slots = rand() % (1 << min(attempt, max_BEB_k));
            this_thread::sleep_for(chrono::milliseconds(slot_ms * backoff_slots));
        }
    }

    close(sock);
    cout << "Client " << id << " has completed\n";
}
static bool file_exists_and_nonempty(const char* path) {
    struct stat st{};
    if (stat(path, &st) != 0) return false;
    return st.st_size > 0;
}

static int read_payload_bits_len() {
    std::ifstream fin("msg.bits");
    std::string bits;
    if (fin.good()) std::getline(fin, bits);
    if (bits.empty()) bits = "1011001010101110";
    return (int)bits.size();
}

static void append_client_csv_row(
    int n_clients, int frames_per_client, double P_persistent, int slot_ms,
    int ack_timeout_ms, int max_BEB_k, double collisionProb,
    long long total_frames, long long total_bits,
    double total_time_s, double throughput_bps, double avg_delay_ms)
{
    const char* path = "results_client.csv";
    cout<<"Results updated in results_client.csv"<<endl;
    bool need_header = !file_exists_and_nonempty(path);

    std::ofstream ofs(path, std::ios::app);
    if (!ofs) return;

    if (need_header) {
        ofs << "timestamp,clients,frames_per_client,p,slot_ms,ack_timeout_ms,max_BEB_k,"
               "collisionProb,payload_bits,total_frames,total_bits,total_time_s,"
               "throughput_bps,throughput_Mbps,avg_delay_ms\n";
    }

    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);

    int payload_bits = read_payload_bits_len();

    ofs << ts << ","
        << n_clients << ","
        << frames_per_client << ","
        << std::fixed << std::setprecision(3) << P_persistent << ","
        << slot_ms << ","
        << ack_timeout_ms << ","
        << max_BEB_k << ","
        << std::fixed << std::setprecision(3) << collisionProb << ","
        << payload_bits << ","
        << total_frames << ","
        << total_bits << ","
        << std::fixed << std::setprecision(6) << total_time_s << ","
        << std::fixed << std::setprecision(3) << throughput_bps << ","
        << std::fixed << std::setprecision(6) << (throughput_bps / 1e6) << ","
        << std::fixed << std::setprecision(3) << avg_delay_ms
        << "\n";
}

int main()
{
    string server_ip = "127.0.0.1";
    int n_clients = 3, frames_per_client;

    cout << "Number of Clients: ";
    cin >> n_clients;
    cout << "Frames per client: ";
    cin >> frames_per_client;
    cout << "p persistence: ";
    cin >> P_persistent;
    cout << "Slot time: ";
    cin >> slot_ms;
    cout << "ACK timeout time: ";
    cin >> ack_timeout_ms;
    cout << "Back-off Limit: ";
    cin >> max_BEB_k;
    collisionProb=0;

    vector<thread> clients;
    auto start_all = chrono::steady_clock::now();

    for (int i = 0; i < n_clients; ++i)
    {
        clients.emplace_back(client_worker, i, server_ip, frames_per_client);
        this_thread::sleep_for(chrono::milliseconds(20)); 
    }

    atomic<bool> stop_monitor{false};
    thread monitor([&]()
    {
        while (!stop_monitor) {
            this_thread::sleep_for(chrono::seconds(3));
            long long bits   = total_successful_bits.load();
            long long frames = total_successful_frames.load();
            long long delayus= total_delay_us.load();
            double avg_delay_ms = frames ? (delayus / (double)frames / 1000.0) : 0.0;
            cout << "Successful frame transmission: " << frames
                 << ", No of bits:" << bits
                 << ", Avg delay: " << avg_delay_ms << "ms \n";
        }
    });

    for (auto &t : clients) t.join();
    stop_monitor = true;
    monitor.join();

    auto end_all = chrono::steady_clock::now();
    double total_time_s = chrono::duration<double>(end_all - start_all).count();

    long long final_bits    = total_successful_bits.load();
    long long final_frames  = total_successful_frames.load();
    long long final_delayus = total_delay_us.load();

    double throughput_bps = final_bits / total_time_s;
    double avg_delay_ms   = final_frames ? (final_delayus / (double)final_frames / 1000.0) : 0.0;

    cout << "\nThe Transfer has been Completed\n";
    cout << "Number of Clients: " << n_clients << ", Frames per client: " << frames_per_client << "\n";
    cout << "Throughput (bps): " << throughput_bps << "\n";
    cout << "Avg fwding delay: " << avg_delay_ms << "\n";

append_client_csv_row(
    n_clients, frames_per_client, P_persistent, slot_ms,
    ack_timeout_ms, max_BEB_k, collisionProb,
    final_frames, final_bits, total_time_s, throughput_bps, avg_delay_ms);

}
