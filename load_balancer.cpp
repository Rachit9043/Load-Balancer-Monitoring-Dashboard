#include "database.h"
#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cerrno>
#include <chrono>

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

int current_backend = 0;
int wrr_backend = 0;
int wrr_slot = 0;
int next_unmeasured = 0;

double avg_response_time[BACKEND_COUNT] = {};
int response_count[BACKEND_COUNT] = {};

// Algorithm 1: Round Robin
// Algorithm 2: Least Response Time
// Algorithm 3: Weighted Round Robin
Backend selectBackend(int algorithm) {

    if (algorithm == 1) {
        Backend selected = backends[current_backend];
        current_backend = (current_backend + 1) % BACKEND_COUNT;
        return selected;
    }

    if (algorithm == 2) {
        for (int i = 0; i < BACKEND_COUNT; i++) {
            int index = (next_unmeasured + i) % BACKEND_COUNT;

            if (response_count[index] == 0) {
                next_unmeasured = (index + 1) % BACKEND_COUNT;
                return backends[index];
            }
        }

        int best = 0;

        for (int i = 1; i < BACKEND_COUNT; i++) {
            if (avg_response_time[i] < avg_response_time[best]) {
                best = i;
            }
        }

        return backends[best];
    }

    // Weighted Round Robin
    Backend selected = backends[wrr_backend];
    wrr_slot++;

    if (wrr_slot >= backends[wrr_backend].weight) {
        wrr_slot = 0;
        wrr_backend = (wrr_backend + 1) % BACKEND_COUNT;
    }

    return selected;
}

int main() {

    // Connect to SQLite and initialize database tables.
    Database db;

    if (!db.isConnected()) {
        cerr << "Database connection failed." << endl;
        return 1;
    }

    int algorithm_choice;

    cout << "Choose Load Balancing Algorithm:\n";
    cout << "1. Round Robin\n";
    cout << "2. Least Response Time\n";
    cout << "3. Weighted Round Robin\n";
    cout << "Enter choice: ";

    if (!(cin >> algorithm_choice) ||
        algorithm_choice < 1 || algorithm_choice > 3) {
        cerr << "Invalid algorithm choice." << endl;
        return 1;
    }

    string algorithm_name;

    if (algorithm_choice == 1) {
        algorithm_name = "ROUND_ROBIN";
    } else if (algorithm_choice == 2) {
        algorithm_name = "LEAST_RESPONSE_TIME";
    } else {
        algorithm_name = "WEIGHTED_ROUND_ROBIN";
    }

    db.setAlgorithm(algorithm_name, "Selected at startup");

    // Create TCP socket for load balancer.
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
    cout << "Active algorithm: " << algorithm_name << endl;

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

        Backend backend = selectBackend(algorithm_choice);
        int index = backend.server_id - 1;

        cout << "\nForwarding request to Backend Server "
             << backend.server_id << endl;

        auto start_time = steady_clock::now();

        // Create backend socket.
        int backend_socket = socket(AF_INET, SOCK_STREAM, 0);

        if (backend_socket < 0) {
            perror("Backend socket failed");
            db.logRequest(backend.server_id, 0.0, false);
            db.updateServerStatus(backend.server_id, "DOWN", 0);
            close(client_socket);
            continue;
        }

        // Configure backend address.
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

            close(backend_socket);
            close(client_socket);
            continue;
        }

        // Connect to selected backend.
        if (connect(
                backend_socket,
                (sockaddr*)&backend_address,
                sizeof(backend_address)
            ) < 0) {

            perror("Backend connection failed");

            db.logRequest(backend.server_id, 0.0, false);
            db.updateServerStatus(backend.server_id, "DOWN", 0);

            close(backend_socket);
            close(client_socket);
            continue;
        }

        // Forward client request.
        if (send(
                backend_socket,
                buffer,
                bytes_received,
                0
            ) < 0) {

            perror("Request forwarding failed");

            db.logRequest(backend.server_id, 0.0, false);
            db.updateServerStatus(backend.server_id, "UP", 0);

            close(backend_socket);
            close(client_socket);
            continue;
        }

        // Receive backend response.
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

            // Update average response time.
            avg_response_time[index] =
                (avg_response_time[index] * response_count[index]
                 + response_time) / (response_count[index] + 1);

            response_count[index]++;

            // Save successful request and backend status.
            db.logRequest(backend.server_id, response_time, true);
            db.updateServerStatus(backend.server_id, "UP", 0);

            cout << "Server " << backend.server_id
                 << " response time: " << response_time << " ms" << endl;

            cout << "Server " << backend.server_id
                 << " average response time: "
                 << avg_response_time[index] << " ms" << endl;

            // Return response to client.
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
        }

        close(backend_socket);
        close(client_socket);
    }

    close(lb_socket);
    return 0;
}
