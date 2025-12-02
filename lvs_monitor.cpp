#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <chrono>
#include <cstdlib>
#include <regex>
#include <sstream>
#include <set>
#include <thread>

using namespace std;
using namespace std::chrono;

// ---------------- CONFIG ----------------
// BACKEND NODES
vector<string> BACKEND_SERVERS = {"10.1.1.2", "10.1.1.3"};

// Virtual IP that LVS listens on
string LVS_VIRTUAL_IP = "<eth0_ip_address>";

// Port list now supports single ports ("80") AND ranges ("11000-12000")
vector<string> TCP_SERVICES = {"80", "443", "445", "446", "5201", "55665", "11000-12000"};
vector<string> UDP_SERVICES = {"442", "55665", "11000-12000"};

int LOSS_THRESHOLD = 5;      // % above which the gateway will be droppedd
int WINDOW_SECONDS = 60;     // sliding window size the seconds it will consider to see the % of packet loss
int PING_TIMEOUT = 1;        // seconds a ping timeout is considered

// ---------------- GLOBALS ----------------
map<string, deque<int>> loss_history;
map<string, string> server_status;
set<string> created_services;

// ---------------------------------------------------------
// EXPAND PORT RANGES: "11000-12000" → [11000,11001...12000]
vector<int> expand_ports(const vector<string>& ports_raw) {
    vector<int> expanded;

    for (const auto& p : ports_raw) {
        if (p.find('-') != string::npos) {
            int start, end;
            char dash;
            stringstream ss(p);
            ss >> start >> dash >> end;

            if (start <= end) {
                for (int i = start; i <= end; i++)
                    expanded.push_back(i);
            }
        } else {
            expanded.push_back(stoi(p));
        }
    }

    return expanded;
}

// ---------------------------------------------------------
int ping_server(const std::string &ip) {
    std::string cmd =
        "timeout " + to_string(PING_TIMEOUT) +
        " ping -c 1 -W 1 " + ip + " 2>&1";

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) return 100;

    char buffer[256];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe))
        output += buffer;

    pclose(pipe);

    std::regex loss_regex(R"((\d+(\.\d+)?)%\s*packet loss)");
    std::smatch match;

    if (std::regex_search(output, match, loss_regex)) {
        float loss = 100.0f;
        std::stringstream ss(match[1].str());
        ss >> loss;
        return static_cast<int>(loss);
    }

    return 100; // treat as DOWN if parsing fails
}

// ---------------------------------------------------------
int average_loss(const deque<int>& h) {
    if (h.empty()) return 0;
    int sum = 0;
    for (int x : h) sum += x;
    return sum / h.size();
}

// ---------------------------------------------------------
void create_service_if_needed(char type, int port) {
    string proto = (type == 't') ? "TCP" : "UDP";
    string key = proto + ":" + to_string(port);

    if (created_services.count(key)) return;

    string check_cmd =
        "ipvsadm -Ln | grep -q \"^" + proto + " " + LVS_VIRTUAL_IP + ":" + to_string(port) + "\"";

    if (system(check_cmd.c_str()) != 0) {
        string cmd_add =
            "ipvsadm -A -" + string(1, type) + " " +
            LVS_VIRTUAL_IP + ":" + to_string(port) + " -s rr";

        (void)system(cmd_add.c_str());
        cout << "[INFO] Created " << proto << " " << LVS_VIRTUAL_IP << ":" << port << endl;
        created_services.insert(key);
    }
}

// ---------------------------------------------------------
void add_server_to_lvs(const string& ip) {
    vector<int> tcp_ports = expand_ports(TCP_SERVICES);
    vector<int> udp_ports = expand_ports(UDP_SERVICES);

    for (int port : tcp_ports) {
        create_service_if_needed('t', port);
        string cmd =
            "ipvsadm -a -t " + LVS_VIRTUAL_IP + ":" + to_string(port) +
            " -r " + ip + ":" + to_string(port) + " -m 2>/dev/null";
        (void)system(cmd.c_str());
    }

    for (int port : udp_ports) {
        create_service_if_needed('u', port);
        string cmd =
            "ipvsadm -a -u " + LVS_VIRTUAL_IP + ":" + to_string(port) +
            " -r " + ip + ":" + to_string(port) + " -m 2>/dev/null";
        (void)system(cmd.c_str());
    }

    cout << "[INFO] Added " << ip << " back to LVS" << endl;
}

// ---------------------------------------------------------
void remove_server_from_lvs(const string& ip) {
    vector<int> tcp_ports = expand_ports(TCP_SERVICES);
    vector<int> udp_ports = expand_ports(UDP_SERVICES);

    for (int port : tcp_ports) {
        string cmd =
            "ipvsadm -d -t " + LVS_VIRTUAL_IP + ":" + to_string(port) +
            " -r " + ip + ":" + to_string(port) + " 2>/dev/null";
        (void)system(cmd.c_str());
    }

    for (int port : udp_ports) {
        string cmd =
            "ipvsadm -d -u " + LVS_VIRTUAL_IP + ":" + to_string(port) +
            " -r " + ip + ":" + to_string(port) + " 2>/dev/null";
        (void)system(cmd.c_str());
    }

    cout << "[WARN] Removed " << ip << " from LVS" << endl;
}

// ---------------------------------------------------------
int main() {
    cout << "[START] LVS Health Monitor (Single Loop Version)\n";
    cout << "------------------------------------------------\n";

    // Initialize server states
    for (const auto& s : BACKEND_SERVERS)
        server_status[s] = "UNKNOWN";

    while (true) {
        auto loop_start = steady_clock::now();

        for (const auto& server : BACKEND_SERVERS) {
            int loss = ping_server(server);

            auto &h = loss_history[server];
            h.push_back(loss);
            if (h.size() > WINDOW_SECONDS) h.pop_front();

            int avg = average_loss(h);

            cout << "[CHECK] " << server
                 << " | Latest=" << loss << "% | Avg(" << WINDOW_SECONDS << "s)=" << avg << "%\n";

            if (avg >= LOSS_THRESHOLD && server_status[server] != "DOWN") {
                remove_server_from_lvs(server);
                server_status[server] = "DOWN";
            } else if (avg < LOSS_THRESHOLD && server_status[server] != "UP") {
                add_server_to_lvs(server);
                server_status[server] = "UP";
            }
        }

        // Keep 1-second interval
        auto loop_end = steady_clock::now();
        auto elapsed = duration_cast<milliseconds>(loop_end - loop_start).count();
        if (elapsed < 1000) this_thread::sleep_for(milliseconds(1000 - elapsed));
    }

    return 0;
}
