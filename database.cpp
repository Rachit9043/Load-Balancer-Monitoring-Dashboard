#include "database.h"
#include <iostream>

using namespace std;

Database::Database() : db(nullptr) {
    if (sqlite3_open("load_balancer.db", &db) != SQLITE_OK) {
        cerr << "Database connection failed: "
             << (db ? sqlite3_errmsg(db) : "Unknown error")
             << endl;

        if (db) sqlite3_close(db);
        db = nullptr;
        return;
    }

    cout << "Database connected successfully." << endl;
    initialize();
}

Database::~Database() {
    if (db != nullptr) {
        sqlite3_close(db);
    }
}

bool Database::isConnected() {
    return db != nullptr;
}

bool Database::execute(const string& sql) {
    if (!db) return false;

    char* error = nullptr;
    int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);

    if (result != SQLITE_OK) {
        cerr << "SQL Error: "
             << (error ? error : "Unknown error") << endl;
        sqlite3_free(error);
        return false;
    }

    return true;
}

void Database::initialize() {
    execute(
        "CREATE TABLE IF NOT EXISTS servers ("
        "server_id INTEGER PRIMARY KEY,"
        "port INTEGER NOT NULL,"
        "weight INTEGER NOT NULL DEFAULT 1,"
        "status TEXT NOT NULL DEFAULT 'UNKNOWN',"
        "active_requests INTEGER NOT NULL DEFAULT 0,"
        "last_seen INTEGER DEFAULT 0);"
    );

    execute(
        "CREATE TABLE IF NOT EXISTS request_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_id INTEGER NOT NULL,"
        "response_time REAL NOT NULL,"
        "success INTEGER NOT NULL,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);"
    );

    execute(
        "CREATE TABLE IF NOT EXISTS algorithm_state ("
        "id INTEGER PRIMARY KEY CHECK(id = 1),"
        "current_algorithm TEXT NOT NULL,"
        "previous_algorithm TEXT,"
        "switch_reason TEXT,"
        "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP);"
    );

    execute(
        "CREATE TABLE IF NOT EXISTS algorithm_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "previous_algorithm TEXT,"
        "new_algorithm TEXT NOT NULL,"
        "reason TEXT,"
        "changed_at DATETIME DEFAULT CURRENT_TIMESTAMP);"
    );

    execute(
        "CREATE TABLE IF NOT EXISTS operation_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "operation TEXT NOT NULL,"
        "details TEXT,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);"
    );

    execute(
        "INSERT OR IGNORE INTO algorithm_state "
        "(id, current_algorithm) VALUES (1, 'ROUND_ROBIN');"
    );

    execute(
        "INSERT OR IGNORE INTO servers (server_id, port, weight) "
        "VALUES (1, 8001, 2), (2, 8002, 1), (3, 8003, 1);"
    );

    cout << "Database tables initialized." << endl;
}

void Database::updateServerStatus(
    int server_id, const string& status, int active_requests
) {
    if (!db) return;

    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "UPDATE servers SET status=?, active_requests=?, "
        "last_seen=strftime('%s','now') WHERE server_id=?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Failed to prepare server status update." << endl;
        return;
    }

    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, active_requests);
    sqlite3_bind_int(stmt, 3, server_id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        cerr << "Failed to update server status." << endl;
    }

    sqlite3_finalize(stmt);
}

void Database::logRequest(
    int server_id, double response_time, bool success
) {
    if (!db) return;

    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "INSERT INTO request_logs (server_id, response_time, success) "
        "VALUES (?, ?, ?);";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Failed to prepare request log." << endl;
        return;
    }

    sqlite3_bind_int(stmt, 1, server_id);
    sqlite3_bind_double(stmt, 2, response_time);
    sqlite3_bind_int(stmt, 3, success ? 1 : 0);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        cerr << "Failed to record request." << endl;
    }

    sqlite3_finalize(stmt);
}

void Database::setAlgorithm(
    const string& new_algorithm, const string& reason
) {
    if (!db) return;

    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "UPDATE algorithm_state "
        "SET previous_algorithm=current_algorithm, "
        "current_algorithm=?, switch_reason=?, "
        "updated_at=CURRENT_TIMESTAMP "
        "WHERE id=1 AND current_algorithm != ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Failed to prepare algorithm update." << endl;
        return;
    }

    sqlite3_bind_text(stmt, 1, new_algorithm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, reason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, new_algorithm.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        cerr << "Failed to update algorithm." << endl;
    }

    sqlite3_finalize(stmt);
}

string Database::getAlgorithm() {
    if (!db) return "ROUND_ROBIN";

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT current_algorithm FROM algorithm_state WHERE id=1;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return "ROUND_ROBIN";
    }

    string algorithm = "ROUND_ROBIN";

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* value = sqlite3_column_text(stmt, 0);

        if (value) {
            algorithm = reinterpret_cast<const char*>(value);
        }
    }

    sqlite3_finalize(stmt);
    return algorithm;
}

void Database::displayRequestLogs() {
    if (!db) return;

    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "SELECT id, server_id, response_time, success, created_at "
        "FROM request_logs ORDER BY id DESC LIMIT 10;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Unable to read request logs." << endl;
        return;
    }

    cout << "\nRecent Request Logs\n";
    cout << "-------------------\n";

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* timestamp = sqlite3_column_text(stmt, 4);

        cout << "Request ID: " << sqlite3_column_int(stmt, 0)
             << " | Server: " << sqlite3_column_int(stmt, 1)
             << " | Response: " << sqlite3_column_double(stmt, 2)
             << " ms | Success: " << sqlite3_column_int(stmt, 3)
             << " | Time: "
             << (timestamp ? reinterpret_cast<const char*>(timestamp) : "")
             << '\n';
    }

    sqlite3_finalize(stmt);
}
