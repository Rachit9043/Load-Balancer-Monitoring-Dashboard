#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <sqlite3.h>

class Database {
private:
    sqlite3* db;

public:
    Database();
    ~Database();

    bool isConnected();
    bool execute(const std::string& sql);
    void initialize();

    void updateServerStatus(
        int server_id,
        const std::string& status,
        int active_requests
    );

    void logRequest(
        int server_id,
        double response_time,
        bool success
    );

    void setAlgorithm(
        const std::string& new_algorithm,
        const std::string& reason
    );

    std::string getAlgorithm();
    void displayRequestLogs();
};

#endif
