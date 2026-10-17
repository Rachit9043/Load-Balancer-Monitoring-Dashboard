# Adaptive Load Balancer and Monitoring Dashboard

## 📌 Overview

The **Adaptive Load Balancer and Monitoring Dashboard** is a C++-based project designed to distribute incoming client requests across multiple backend servers efficiently.

The project uses TCP socket programming to handle client connections and forward requests to backend servers. It aims to improve request distribution, monitor server performance, and dynamically select load-balancing algorithms based on performance metrics.

## 🎯 Objectives

- Distribute client requests across multiple backend servers.
- Implement different load-balancing algorithms.
- Measure server response times and performance.
- Store request and server metrics in a database.
- Provide a foundation for an adaptive load-balancing monitoring dashboard.

## ⚙️ Technologies Used

- **C++** — Core implementation
- **TCP/IP Socket Programming** — Client-server communication
- **Linux / Ubuntu (WSL)** — Development and execution environment
- **Git & GitHub** — Version control
- **Database (planned/in progress)** — Storing request logs and performance metrics

## 🚀 Features

- TCP-based communication between clients and the load balancer.
- Multiple backend server support.
- Request forwarding to backend servers.
- Weighted Round Robin scheduling (implementation in progress).
- Least Response Time scheduling (implementation in progress).
- Server response-time measurement and performance monitoring.
- Database integration for storing request and server metrics (planned/in progress).

## 🏗️ Project Architecture

```text
              Client
                |
                v
       +------------------+
       |  Load Balancer   |
       |   (C++ / TCP)    |
       +------------------+
          /      |      \
         v       v       v
    +--------+ +--------+ +--------+
    |Server 1| |Server 2| |Server 3|
    | :8001  | | :8002  | | :8003  |
    +--------+ +--------+ +--------+
                |
                v
       Database / Monitoring
```

The client sends a request to the load balancer, which selects a backend server according to the configured scheduling algorithm. The load balancer forwards the request and can measure the time taken to receive the server's response.

## 🧠 Load-Balancing Algorithms

### 1. Weighted Round Robin

Distributes requests among backend servers according to their assigned weights. Servers with higher weights receive a larger share of requests.

### 2. Least Response Time

Selects a backend server based on its measured response time, aiming to direct requests toward faster-performing servers.

*Note: Update the implementation status of each algorithm as you complete and test it.*

## 🛠️ Setup and Execution

### Prerequisites

- A C++ compiler such as `g++`
- Ubuntu or another Linux-compatible environment
- Git

### 1. Clone the repository

```bash
git clone <YOUR_REPOSITORY_URL>
cd <YOUR_PROJECT_DIRECTORY>
```

Replace the placeholders with your actual GitHub repository URL and directory name.

### 2. Compile the backend server

```bash
g++ server.cpp -o server
```

### 3. Compile the load balancer

```bash
g++ load_balancer.cpp -o load_balancer
```

### 4. Start the backend servers

Open separate terminals and start the backend servers using their configured ports and server IDs. For example:

```bash
./server 8001 1
./server 8002 2
./server 8003 3
```

### 5. Start the load balancer

```bash
./load_balancer
```

The load balancer listens on port `8080` in the current configuration.

### 6. Test the connection

Connect a client to the load balancer using a TCP client such as Netcat:

```bash
nc 127.0.0.1 8080
```

Ensure the backend servers are running before testing request forwarding.

## 🗄️ Database Integration

The planned database component will store information such as:

- Request identifiers
- Selected backend server
- Response time
- Request status
- Server performance metrics
- Load-balancing algorithm used

These records will support performance analysis and the monitoring dashboard.

## 🔮 Future Enhancements

- Complete database integration.
- Develop a real-time monitoring dashboard.
- Add automatic algorithm selection based on server performance.
- Improve error handling and unavailable-server detection.
- Display request distribution and response-time statistics.
- Evaluate the performance of different scheduling algorithms.

## 👨‍💻 Contributors

- Rachit Kumar
- Vineet Kalouni
- Dev Lasyal

## 📄 License

This project is developed for educational purposes. A formal open-source license can be added as the project evolves.
