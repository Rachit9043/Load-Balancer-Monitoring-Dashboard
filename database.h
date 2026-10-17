
#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <sqlite3.h>

class Database {
private:
    sqlite3* db;

public:
    // Constructor and destructor
    Database();
    ~Database();

    // Check database connection
    bool isConnected();

    // Execute an SQL query
    bool execute(const std::string& sql);

    // Create and initialize database tables
    void initialize();

    // Update backend server status
    void updateServerStatus(
        int server_id,
        const std::string& status,
        int active_requests
    );

    // Store request information
    void logRequest(
        int server_id,
        double response_time,
        bool success
    );

    // Update the current algorithm and record its history
    void setAlgorithm(
        const std::string& new_algorithm,
        const std::string& reason
    );

    // Retrieve the currently selected algorithm
    std::string getAlgorithm();

    // Display request logs
    void displayRequestLogs();
};

#endif
