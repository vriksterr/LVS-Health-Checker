#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <thread>
#include <chrono>
#include <mutex>
#include <sstream>
#include <cstdlib>
#include <regex>

using namespace std;
using namespace std::chrono;

// ---------------- CONFIG ----------------
vector<string> BACKEND_SERVERS = {"10.1.2.2", "10.1.2.3"};
string LVS_VIRTUAL_IP = "<ip_of_eth_interface>";

vector<int> TCP_SERVICES = {80, 443, 445, 446, 2232, 55665};
vector<int> UDP_SERVICES = {442, 55665};

int LOSS_THRESHOLD = 5;      // %
int WINDOW_SECONDS = 60;     // sliding window size
int PING_COUNT = 1;
int PING_TIMEOUT = 1;

// ---------------- GLOBALS ----------------
map<string, deque<int>> loss_history;
map<string, string> server_status;
mutex mtx;

// ---------------- HELPERS ----------------
int ping_server(const string& server) {
    string cmd = "ping -c " + to_string(PING_COUNT) + 
                 " -W " + to_string(PING_TIMEOUT) + 
                 " " + server;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return 100;

    string result;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe))
        result += buffer;
    pclose(pipe);

    regex loss_regex("(\\d+)% packet loss");
    smatch match;
    if (regex_search(result, match, loss_regex))
        return stoi(match[1]);

    return 100;
}

int average_loss(const deque<int>& history) {
    if (history.empty()) return 0;
    int sum = 0;
    for (int v : history) sum += v;
    return sum / history.size();
}

void create_service_if_needed(char type, int port) {
    string cmd_check = "ipvsadm -l | grep -q \"" + 
                       LVS_VIRTUAL_IP + ":" + to_string(port) + "\"";

    if (system(cmd_check.c_str()) != 0) {
        string cmd_add =
            "ipvsadm -A -" + string(1, type) + " " +
            LVS_VIRTUAL_IP + ":" + to_string(port) + " -s rr";

        system(cmd_add.c_str());
        cout << "[INFO] Created "
             << (type == 't' ? "TCP" : "UDP") << " "
             << LVS_VIRTUAL_IP << ":" << port << endl;
    }
}

void add_server_to_lvs(const string& server_ip) {
    for (int port : TCP_SERVICES) {
        create_service_if_needed('t', port);
        string cmd =
            "ipvsadm -a -t " + LVS_VIRTUAL_IP + ":" + to_string(port) +
            " -r " + server_ip + ":" + to_string(port) + " -m 2>/dev/null";
        system(cmd.c_str());
    }

    for (int port : UDP_SERVICES) {
        create_service_if_needed('u', port);
        string cmd =
            "ipvsadm -a -u " + LVS_VIRTUAL_IP + ":" + to_string(port) +
            " -r " + server_ip + ":" + to_string(port) + " -m 2>/dev/null";
        system(cmd.c_str());
    }

    cout << "[INFO] Added " << server_ip << " back to LVS" << endl;
}

void remove_server_from_lvs(const string& server_ip) {
    for (int port : TCP_SERVICES) {
        string cmd =
            "ipvsadm -d -t " + LVS_VIRTUAL_IP + ":" + to_string(port) +
            " -r " + server_ip + ":" + to_string(port) + " 2>/dev/null";
        system(cmd.c_str());
    }

    for (int port : UDP_SERVICES) {
        string cmd =
            "ipvsadm -d -u " + LVS_VIRTUAL_IP + ":" + to_string(port) +
            " -r " + server_ip + ":" + to_string(port) + " 2>/dev/null";
        system(cmd.c_str());
    }

    cout << "[WARN] Removed " << server_ip << " from LVS" << endl;
}

// ---------------- HEALTH CHECK THREAD ----------------
void monitor_server(const string& server) {
    while (true) {

        // -------- PARALLEL PART (NO LOCK) --------
        int loss = ping_server(server);

        // -------- CRITICAL SECTION --------
        {
            lock_guard<mutex> lock(mtx);

            auto& history = loss_history[server];
            history.push_back(loss);
            if (history.size() > WINDOW_SECONDS)
                history.pop_front();

            int avg_loss = average_loss(history);

            cout << "[CHECK] " << server
                 << " | Latest=" << loss << "%"
                 << " | Avg(60s)=" << avg_loss << "%" << endl;

            // State transitions
            if (avg_loss >= LOSS_THRESHOLD && server_status[server] != "DOWN") {
                remove_server_from_lvs(server);
                server_status[server] = "DOWN";
            } else if (avg_loss < LOSS_THRESHOLD && server_status[server] != "UP") {
                add_server_to_lvs(server);
                server_status[server] = "UP";
            }
        }

        this_thread::sleep_for(1s);
    }
}

// ---------------- MAIN ----------------
int main() {
    cout << "[START] LVS Health Monitor (Parallel Ping + Sliding Window)" << endl;
    cout << "-------------------------------------------------------------" << endl;

    for (const auto& server : BACKEND_SERVERS)
        server_status[server] = "UNKNOWN";

    vector<thread> threads;
    for (const auto& server : BACKEND_SERVERS)
        threads.emplace_back(monitor_server, server);

    for (auto& t : threads)
        t.join();

    return 0;
}
