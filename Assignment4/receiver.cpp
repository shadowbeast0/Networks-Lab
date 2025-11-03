#include <bits/stdc++.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "common.h"
using namespace std;

constexpr int PORT = 9090;

int main()
{

    int L, slot_ms;
    cout << "Enter Walsh code length (power of 2): ";
    cin >> L;
    cout << "Enter slot time (ms): ";
    cin >> slot_ms;

    auto H = walsh(L);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        cerr << "bind failed\n";
        return 1;
    }
    if (listen(server_fd, 64) < 0)
    {
        cerr << "listen failed\n";
        return 1;
    }

    cout << "CDMA Receiver listening on port " << PORT << " ...\n";

    mutex mtx, cout_mtx;
    vector<pair<int, int>> clients;    
    unordered_map<int, int> sock2code; 
    vector<int> slot_sum(L, 0);
    vector<int> contrib_codes; 
    atomic<bool> running{true};

    
    thread acceptor([&]()
                    {
        int next_code=0;
        while(running){
            sockaddr_in caddr{}; socklen_t clen=sizeof(caddr);
            int cs=accept(server_fd,(sockaddr*)&caddr,&clen);
            if(cs<0) continue;
            int code_idx = next_code % L;
            next_code++;

            {
                lock_guard<mutex> lk(mtx);
                clients.push_back({cs,code_idx});
                sock2code[cs]=code_idx;
            }

            
            string hello="CODE "+to_string(code_idx)+" "+to_string(L)+"\n";
            send(cs,hello.c_str(),(int)hello.size(),0);

            {
                lock_guard<mutex> lk(cout_mtx);
                cout<<"[ACCEPT] client socket "<<cs<<" -> code "<<code_idx<<"\n";
            }

            
            thread([&,cs,code_idx](){
                vector<char> buf(L);
                while(true){
                    int got=0;
                    while(got<L){
                        int n=recv(cs,buf.data()+got,L-got,0);
                        if(n<=0){
                            
                            lock_guard<mutex> lk(mtx);
                            sock2code.erase(cs);
                            clients.erase(remove_if(clients.begin(),clients.end(),
                                 [&](auto&p){return p.first==cs;}), clients.end());
                            close(cs);
                            {
                                lock_guard<mutex> lk2(cout_mtx);
                                cout<<"[DISCONNECT] socket "<<cs<<" (code "<<code_idx<<")\n";
                            }
                            return;
                        }
                        got+=n;
                    }
                    
                    {
                        lock_guard<mutex> lk(mtx);
                        for(int i=0;i<L;i++){
                            int v = (buf[i]=='+')?+1:-1;
                            slot_sum[i]+=v;
                        }
                        contrib_codes.push_back(code_idx);
                    }
                }
            }).detach();
        } });

    
    thread slotter([&]()
                   {
        long long slot_id=0;
        while(running){
            this_thread::sleep_for(chrono::milliseconds(slot_ms));
            vector<int> sum_local;
            vector<int> contrib_local;

            {
                lock_guard<mutex> lk(mtx);
                sum_local = slot_sum;
                contrib_local.swap(contrib_codes);
                fill(slot_sum.begin(),slot_sum.end(),0);
            }
            if(contrib_local.empty()){ slot_id++; continue; }

            sort(contrib_local.begin(),contrib_local.end());
            contrib_local.erase(unique(contrib_local.begin(),contrib_local.end()),contrib_local.end());

            
            {
                lock_guard<mutex> lk(cout_mtx);
                cout<<"[SLOT "<<slot_id<<"] composite chips: ";
                for(int i=0;i<L;i++){
                    int v=sum_local[i];
                    if(v>0) cout<<'+';
                    else if(v<0) cout<<'-';
                    else cout<<'0'; 
                }
                cout<<"\n";
            }

            
            for(int code_idx: contrib_local){
                int bit = decode_bit(sum_local, H[code_idx]);
                int cs = -1;
                {
                    lock_guard<mutex> lk(mtx);
                    for(auto &p:clients) if(p.second==code_idx){ cs=p.first; break; }
                }
                if(cs!=-1){
                    string ack="ACK "; ack.push_back(char('0'+bit));
                    send(cs,ack.c_str(),(int)ack.size(),0);
                }
                {
                    lock_guard<mutex> lk(cout_mtx);
                    cout<<"  └─ decode for code "<<code_idx<<" => bit "<<bit<<"\n";
                }
            }
            slot_id++;
        } });

    acceptor.join();
    slotter.join();
    close(server_fd);
    return 0;
}
