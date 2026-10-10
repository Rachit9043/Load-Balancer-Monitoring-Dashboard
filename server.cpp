#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

using namespace std;

int main(int argc, char* argv[]) {

    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <port> <server_id>\n";
        return 1;
    }

    int port = stoi(argv[1]);
    int server_id = stoi(argv[2]);

    // Create TCP socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket < 0) {
        perror("Socket creation failed");
        return 1;
    }

    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Server address
    sockaddr_in server_address{};

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);

    // Bind socket to port
    if (bind(server_socket,
             (struct sockaddr*)&server_address,
             sizeof(server_address)) < 0) {

        perror("Bind failed");
        close(server_socket);
        return 1;
    }

    // Start listening
    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        close(server_socket);
        return 1;
    }

    cout << "Backend Server " << server_id
         << " listening on port " << port << endl;

    while (true) {

        sockaddr_in client_address{};
        socklen_t client_size = sizeof(client_address);

        // Accept client
        int client_socket = accept(
            server_socket,
            (struct sockaddr*)&client_address,
            &client_size
        );

        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        // Receive request
        char buffer[1024] = {0};

        int bytes_received = recv(
            client_socket,
            buffer,
            sizeof(buffer) - 1,
            0
        );

        if (bytes_received > 0) {

            buffer[bytes_received] = '\0';

            cout << "Server " << server_id
                 << " received: "
                 << buffer << endl;

            // Response
            string response =
                "Hello from Backend Server " +
                to_string(server_id) +
                "\n";

            // Send response
            send(
                client_socket,
                response.c_str(),
                response.size(),
                0
            );
        }

        close(client_socket);
    }

    close(server_socket);

    return 0;
}
