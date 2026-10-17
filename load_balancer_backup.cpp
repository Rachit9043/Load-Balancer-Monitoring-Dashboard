#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cerrno>

using namespace std;

struct Backend {
    string ip;
    int port;
    int server_id;
};

Backend backends[] = {
    {"127.0.0.1", 8001, 1},
    {"127.0.0.1", 8002, 2},
    {"127.0.0.1", 8003, 3}
};

const int BACKEND_COUNT = 3;
int current_backend = 0;

int main() {
    // Create a new tcp socket for load balancer
    int lb_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (lb_socket < 0) {
        perror("Load balancer socket failed");
        return 1;
    }

    int opt = 1;
    setsockopt(lb_socket, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    // make the load balancer listen on port number 8080
    sockaddr_in lb_address{};
    lb_address.sin_family = AF_INET;
    lb_address.sin_addr.s_addr = INADDR_ANY;
    lb_address.sin_port = htons(8080);

    if (bind(lb_socket, (sockaddr*)&lb_address,
             sizeof(lb_address)) < 0) {
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

        // Read the client's request
        char buffer[1024] = {};

        ssize_t bytes_received = recv(
            client_socket, buffer, sizeof(buffer) - 1, 0
        );

        if (bytes_received <= 0) {
            close(client_socket);
            continue;
        }

        // Select the next backend using Round Robin
        Backend backend = backends[current_backend];
        current_backend = (current_backend + 1) % BACKEND_COUNT;

        cout << "Forwarding request to Backend Server "
             << backend.server_id << endl;

        // Connect to the selected backend
        int backend_socket = socket(AF_INET, SOCK_STREAM, 0);

        if (backend_socket < 0) {
            perror("Backend socket failed");
            close(client_socket);
            continue;
        }

        sockaddr_in backend_address{};
        backend_address.sin_family = AF_INET;
        backend_address.sin_port = htons(backend.port);

        if (inet_pton(AF_INET, backend.ip.c_str(),
                      &backend_address.sin_addr) <= 0) {
            cerr << "Invalid backend IP address" << endl;
            close(backend_socket);
            close(client_socket);
            continue;
        }

        if (connect(backend_socket,
                    (sockaddr*)&backend_address,
                    sizeof(backend_address)) < 0) {
            perror("Backend connection failed");
            close(backend_socket);
            close(client_socket);
            continue;
        }

        // Forward the request to the backend
        if (send(backend_socket, buffer,
                 bytes_received, 0) < 0) {
            perror("Request forwarding failed");
            close(backend_socket);
            close(client_socket);
            continue;
        }

        // Receive the backend's response
        char response[1024] = {};

        ssize_t response_size = recv(
            backend_socket, response,
            sizeof(response) - 1, 0
        );

        // Return the response to the client
        if (response_size > 0) {
            send(client_socket, response,
                 response_size, 0);
        } else {
            cerr << "Backend returned no response" << endl;
        }

        close(backend_socket);
        close(client_socket);
    }

    close(lb_socket);
    return 0;
}
