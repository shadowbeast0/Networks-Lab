#include <fstream>
#include <sys/stat.h>
#include <ctime>
#include <iomanip>
#include "common.h"
#include <bits/stdc++.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <vector>
#include <atomic>
#include <fcntl.h>
#include <errno.h>
using namespace std;

static constexpr int PORT = 5000;

mutex channel_mtx;
bool channel_busy_flag = false;

atomic<int> collision_count{0};
atomic<int> active_clients{0};
atomic<bool> server_running{true};

void handle_client(int client_sock, int id)
{
    active_clients++;
    char buffer[2048];

    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(client_sock, buffer, sizeof(buffer), 0);
        if (n <= 0) break;

        bool collided = false;

        { 
            lock_guard<mutex> lock(channel_mtx);
            if (channel_busy_flag)
            {
                cerr << "Collision detected by CSMA/CD in Client " << id << "\n";
                collision_count++;
                collided = true; 
            }
            else
            {
                channel_busy_flag = true;
            }
        }

        if (collided)
            continue;

        this_thread::sleep_for(chrono::milliseconds(20));

        send(client_sock, "ACK", 3, 0);
        {
            lock_guard<mutex> lock(channel_mtx);
            channel_busy_flag = false;
        }
    }

    close(client_sock);
    cout << "Receiver: Client has " << id << " disconnected\n";
    active_clients--;
}
static bool file_exists_and_nonempty(const char* path) {
    struct stat st{};
    if (stat(path, &st) != 0) return false;
    return st.st_size > 0;
}

static void append_receiver_csv_row(int total_clients_served, int total_collisions, double total_active_time_s)
{
    const char* path = "results_server.csv";
    bool need_header = !file_exists_and_nonempty(path);

    std::ofstream ofs(path, std::ios::app);
    if (!ofs) return;

    if (need_header) {
        ofs << "timestamp,total_clients_served,total_collisions,total_active_time_s\n";
    }

    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);

    ofs << ts << ","
        << total_clients_served << ","
        << total_collisions << ","
        << std::fixed << std::setprecision(6) << total_active_time_s
        << "\n";
}

int main()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        cerr << "Socket creation failed\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        cerr << "Bind failed\n";
        return 1;
    }

    listen(server_fd, 10);
    cout << "Receiver is waiting and listening at port: " << PORT << "...\n";

    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    vector<thread> threads;
    int client_id = 0;

    thread monitor_thread([]{
        while (server_running)
        {
            this_thread::sleep_for(chrono::seconds(3));
            cout << "Active clients: " << active_clients.load()
                 << ", Collisions there has been so far: " << collision_count.load() << "\n";
        }
    });

    auto start_time      = chrono::steady_clock::now();
    auto last_activity   = chrono::steady_clock::now();
    const auto idle_limit= chrono::seconds(15);

    while (true)
    {
        sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        int client_sock = accept(server_fd, (sockaddr *)&caddr, &clen);

        if (client_sock >= 0)
        {
            cout << "Client " << client_id << " connected.\n";
            threads.emplace_back(handle_client, client_sock, client_id++);
            last_activity = chrono::steady_clock::now();
        }
        else
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            {
                this_thread::sleep_for(chrono::milliseconds(50));
            }
        }

        if (active_clients.load() == 0 &&
            chrono::steady_clock::now() - last_activity > idle_limit)
        {
            break;
        }

        this_thread::sleep_for(chrono::milliseconds(10));
    }

    server_running = false;
    if (monitor_thread.joinable()) monitor_thread.join();

    for (auto &t : threads)
        if (t.joinable()) t.join();

    close(server_fd);

    auto end_time = chrono::steady_clock::now();
    double total_time_s = chrono::duration<double>(end_time - start_time).count();

    cout << "\nCSMA/CD p persistence complete\n";
    cout << "Total clients: " << client_id << "\n";
    cout << "Total collisions detected: " << collision_count.load() << "\n";
    cout << "Total active time: " << total_time_s << "\n";
    append_receiver_csv_row(client_id, collision_count.load(), total_time_s);

    return 0;
}
