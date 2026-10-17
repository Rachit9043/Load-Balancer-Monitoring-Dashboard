#include "database.h"
#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cerrno>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace std;
using namespace chrono;

struct Backend {
    string ip;
    int port;
    int server_id;
    int weight;
};

Backend backends[] = {
    {"127.0.0.1", 8001, 1, 2},
    {"127.0.0.1", 8002, 2, 1},
    {"127.0.0.1", 8003, 3, 1}
};

const int BACKEND_COUNT = 3;

// Algorithm IDs:
// 1 = Round Robin
// 2 = Least Response Time
// 3 = Weighted Round Robin

int current_backend = 0;
int wrr_backend = 0;
int wrr_slot = 0;
int next_unmeasured = 0;

// Response-time statistics.
double avg_response_time[BACKEND_COUNT] = {};
int response_count[BACKEND_COUNT] = {};
bool server_healthy[BACKEND_COUNT] = {true, true, true};

// Adaptive controller settings.
const int MIN_SAMPLES = 3;
const double LRT_DIFFERENCE_THRESHOLD = 0.30;
const int SWITCH_COOLDOWN_REQUESTS = 10;

int requests_since_switch = SWITCH_COOLDOWN_REQUESTS;

// Convert algorithm ID to its database name.
string algorithmName(int algorithm) {
    if (algorithm == 1) return "ROUND_ROBIN";
    if (algorithm == 2) return "LEAST_RESPONSE_TIME";
    return "WEIGHTED_ROUND_ROBIN";
}

// Convert database name to algorithm ID.
int algorithmId(const string& name) {
    if (name == "LEAST_RESPONSE_TIME") return 2;
    if (name == "WEIGHTED_ROUND_ROBIN") return 3;
    return 1;
}

// Select a backend using the chosen algorithm.
Backend selectBackend(int algorithm) {
    vector<int> healthy;

    for (int i = 0; i < BACKEND_COUNT; i++) {
        if (server_healthy[i]) {
            healthy.push_back(i);
        }
    }

    // If all servers appear unhealthy, try them all.
    // This allows the system to recover when a backend returns.
    if (healthy.empty()) {
        for (int i = 0; i < BACKEND_COUNT; i++) {
            healthy.push_back(i);
        }
    }

    // Round Robin.
    if (algorithm == 1) {
        for (int i = 0; i < BACKEND_COUNT; i++) {
            int index = current_backend;
            current_backend = (current_backend + 1) % BACKEND_COUNT;

            if (server_healthy[index] || healthy.size() == BACKEND_COUNT) {
                return backends[index];
            }
        }

        return backends[healthy.front()];
    }

    // Least Response Time.
    if (algorithm == 2) {
        // Give unmeasured healthy servers a chance first.
        for (int i = 0; i < BACKEND_COUNT; i++) {
            int index = (next_unmeasured + i) % BACKEND_COUNT;

            if (server_healthy[index] && response_count[index] == 0) {
                next_unmeasured = (index + 1) % BACKEND_COUNT;
                return backends[index];
            }
        }

        int best = healthy.front();

        for (int index : healthy) {
            if (response_count[index] == 0) {
                return backends[index];
            }

            if (avg_response_time[index] < avg_response_time[best]) {
                best = index;
            }
        }

        return backends[best];
    }

    // Weighted Round Robin.
    int total_weight = 0;

    for (int index : healthy) {
        total_weight += max(1, backends[index].weight);
    }

    int slot = wrr_slot % total_weight;
    wrr_slot++;

    for (int index : healthy) {
        int weight = max(1, backends[index].weight);

        if (slot < weight) {
            return backends[index];
        }

        slot -= weight;
    }

    return backends[healthy.front()];
}

// Decide which algorithm should be used next.
int recommendAlgorithm() {
    vector<double> healthy_averages;

    for (int i = 0; i < BACKEND_COUNT; i++) {
        if (server_healthy[i] &&
            response_count[i] >= MIN_SAMPLES) {
            healthy_averages.push_back(avg_response_time[i]);
        }
    }

    // Insufficient measurements: use the default.
    if (healthy_averages.size() < 2) {
        return 1;
    }

    double min_time = *min_element(
        healthy_averages.begin(), healthy_averages.end()
    );

    double max_time = *max_element(
        healthy_averages.begin(), healthy_averages.end()
    );

    double mean_time = 0.0;

    for (double value : healthy_averages) {
        mean_time += value;
    }

    mean_time /= healthy_averages.size();

    // Avoid division by zero.
    if (mean_time <= 0.0) {
        return 1;
    }

    double relative_difference = (max_time - min_time) / mean_time;

    // Significant response-time variation: use LRT.
    if (relative_difference >= LRT_DIFFERENCE_THRESHOLD) {
        return 2;
    }

    // Similar response times: use WRR if weights differ.
    bool weights_differ = false;

    for (int i = 1; i < BACKEND_COUNT; i++) {
        if (backends[i].weight != backends[0].weight) {
            weights_differ = true;
            break;
        }
    }

    if (weights_differ) {
        return 3;
    }

    return 1;
}

// Update the algorithm when the controller recommends a change.
void adaptAlgorithm(Database& db, int& current_algorithm) {
    int recommended = recommendAlgorithm();

    if (recommended == current_algorithm) {
        return;
    }

    // Avoid rapid switching.
    if (requests_since_switch < SWITCH_COOLDOWN_REQUESTS) {
        return;
    }

    string reason;

    if (recommended == 1) {
        reason = "Backend response times are similar; using Round Robin";
    } else if (recommended == 2) {
        reason = "Significant response-time differences detected; using Least Response Time";
    } else {
        reason = "Response times are similar and configured weights differ; using Weighted Round Robin";
    }

    db.setAlgorithm(algorithmName(recommended), reason);

    current_algorithm = recommended;
    requests_since_switch = 0;

    cout << "[ADAPTIVE CONTROLLER] Active algorithm: "
         << algorithmName(current_algorithm) << endl;
}

int main() {
    Database db;

    if (!db.isConnected()) {
        cerr << "Database connection failed." << endl;
        return 1;
    }

    // Start automatically with the algorithm stored in SQLite.
    int current_algorithm = algorithmId(db.getAlgorithm());

    cout << "Adaptive Load Balancer Starting..." << endl;
    cout << "Initial algorithm: "
         << algorithmName(current_algorithm) << endl;

    // Create TCP socket for the load balancer.
    int lb_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (lb_socket < 0) {
        perror("Load balancer socket failed");
        return 1;
    }

    int opt = 1;
    setsockopt(lb_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in lb_address{};
    lb_address.sin_family = AF_INET;
    lb_address.sin_addr.s_addr = INADDR_ANY;
    lb_address.sin_port = htons(8080);

    if (bind(lb_socket, (sockaddr*)&lb_address, sizeof(lb_address)) < 0) {
        perror("Load balancer bind failed");
        close(lb_socket);
        return 1;
    }

    if (listen(lb_socket, 10) < 0) {
        perror("Load balancer listen failed");
        close(lb_socket);
        return 1;
    }

    cout << "Load balancer listening on port 8080" << endl;

    while (true) {
        sockaddr_in client_address{};
        socklen_t client_size = sizeof(client_address);

        int client_socket = accept(
            lb_socket,
            (sockaddr*)&client_address,
            &client_size
        );

        if (client_socket < 0) {
            if (errno == EINTR) continue;

            perror("Client accept failed");
            continue;
        }

        char buffer[1024] = {};

        ssize_t bytes_received = recv(
            client_socket,
            buffer,
            sizeof(buffer) - 1,
            0
        );

        if (bytes_received <= 0) {
            close(client_socket);
            continue;
        }

        // Select a backend using the active algorithm.
        Backend backend = selectBackend(current_algorithm);
        int index = backend.server_id - 1;

        cout << "\nForwarding request to Backend Server "
             << backend.server_id << endl;

        auto start_time = steady_clock::now();

        int backend_socket = socket(AF_INET, SOCK_STREAM, 0);

        if (backend_socket < 0) {
            perror("Backend socket failed");

            db.logRequest(backend.server_id, 0.0, false);
            db.updateServerStatus(backend.server_id, "DOWN", 0);

            server_healthy[index] = false;
            requests_since_switch++;

            close(client_socket);
            adaptAlgorithm(db, current_algorithm);
            continue;
        }

        sockaddr_in backend_address{};
        backend_address.sin_family = AF_INET;
        backend_address.sin_port = htons(backend.port);

        if (inet_pton(
                AF_INET,
                backend.ip.c_str(),
                &backend_address.sin_addr
            ) <= 0) {
            cerr << "Invalid backend IP address." << endl;

            db.logRequest(backend.server_id, 0.0, false);
            db.updateServerStatus(backend.server_id, "DOWN", 0);

            server_healthy[index] = false;
            requests_since_switch++;

            close(backend_socket);
            close(client_socket);
            adaptAlgorithm(db, current_algorithm);
            continue;
        }

        if (connect(
                backend_socket,
                (sockaddr*)&backend_address,
                sizeof(backend_address)
            ) < 0) {
            perror("Backend connection failed");

            db.logRequest(backend.server_id, 0.0, false);
            db.updateServerStatus(backend.server_id, "DOWN", 0);

            server_healthy[index] = false;
            requests_since_switch++;

            close(backend_socket);
            close(client_socket);
            adaptAlgorithm(db, current_algorithm);
            continue;
        }

        if (send(
                backend_socket,
                buffer,
                bytes_received,
                0
            ) < 0) {
            perror("Request forwarding failed");

            db.logRequest(backend.server_id, 0.0, false);
            db.updateServerStatus(backend.server_id, "UP", 0);

            requests_since_switch++;

            close(backend_socket);
            close(client_socket);
            adaptAlgorithm(db, current_algorithm);
            continue;
        }

        char response[1024] = {};

        ssize_t response_size = recv(
            backend_socket,
            response,
            sizeof(response) - 1,
            0
        );

        if (response_size > 0) {
            auto end_time = steady_clock::now();

            double response_time = duration<double, milli>(
                end_time - start_time
            ).count();

            // Update the response-time average.
            avg_response_time[index] =
                (avg_response_time[index] * response_count[index]
                 + response_time) / (response_count[index] + 1);

            response_count[index]++;
            server_healthy[index] = true;

            db.logRequest(backend.server_id, response_time, true);
            db.updateServerStatus(backend.server_id, "UP", 0);

            cout << "Server " << backend.server_id
                 << " response time: " << response_time << " ms" << endl;

            cout << "Server " << backend.server_id
                 << " average response time: "
                 << avg_response_time[index] << " ms" << endl;

            if (send(
                    client_socket,
                    response,
                    response_size,
                    0
                ) < 0) {
                perror("Client response failed");
            }
        } else {
            cerr << "Backend returned no response." << endl;

            db.logRequest(backend.server_id, 0.0, false);
            db.updateServerStatus(backend.server_id, "DOWN", 0);

            server_healthy[index] = false;
        }

        close(backend_socket);
        close(client_socket);

        requests_since_switch++;

        // Re-evaluate after collecting the latest result.
        adaptAlgorithm(db, current_algorithm);
    }

    close(lb_socket);
    return 0;
}
