#include "database.h"

#include <iostream>
#include <iomanip>
#include <ctime>
#include <sqlite3.h>

using namespace std;

// ========================================
// CONSTRUCTOR
// ========================================

Database::Database() {
    db = nullptr;

    int result = sqlite3_open("load_balancer.db", &db);

    if (result != SQLITE_OK) {
        cerr << "Database connection failed: "
             << sqlite3_errmsg(db) << endl;
    } else {
        cout << "Database connected successfully." << endl;

        initialize();
    }
}


// ========================================
// DESTRUCTOR
// ========================================

Database::~Database() {
    if (db != nullptr) {
        sqlite3_close(db);
        db = nullptr;
    }
}


// ========================================
// CHECK DATABASE CONNECTION
// ========================================

bool Database::isConnected() {
    return db != nullptr;
}


// ========================================
// EXECUTE SQL QUERY
// ========================================

bool Database::execute(const string& sql) {
    if (db == nullptr) {
        cerr << "Error: Database is not connected." << endl;
        return false;
    }

    char* errorMessage = nullptr;

    int result = sqlite3_exec(
        db,
        sql.c_str(),
        nullptr,
        nullptr,
        &errorMessage
    );

    if (result != SQLITE_OK) {
        cerr << "SQL Error: "
             << (errorMessage ? errorMessage : "Unknown error")
             << endl;

        sqlite3_free(errorMessage);
        return false;
    }

    return true;
}


// ========================================
// INITIALIZE DATABASE TABLES
// ========================================

void Database::initialize() {
    if (db == nullptr) {
        cerr << "Cannot initialize database." << endl;
        return;
    }

    // Enable foreign-key support
    execute("PRAGMA foreign_keys = ON;");

    // ------------------------------------
    // SERVERS TABLE
    // ------------------------------------

    execute(
        "CREATE TABLE IF NOT EXISTS servers ("
        "server_id INTEGER PRIMARY KEY,"
        "status TEXT DEFAULT 'UP',"
        "active_requests INTEGER DEFAULT 0,"
        "total_requests INTEGER DEFAULT 0,"
        "avg_response_time REAL DEFAULT 0.0"
        ");"
    );

    // ------------------------------------
    // REQUEST LOGS TABLE
    // ------------------------------------

    execute(
        "CREATE TABLE IF NOT EXISTS request_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_id INTEGER,"
        "response_time REAL,"
        "success INTEGER,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY(server_id) REFERENCES servers(server_id)"
        ");"
    );

    // ------------------------------------
    // CURRENT ALGORITHM STATE TABLE
    // ------------------------------------

    execute(
        "CREATE TABLE IF NOT EXISTS algorithm_state ("
        "id INTEGER PRIMARY KEY CHECK(id = 1),"
        "algorithm TEXT NOT NULL,"
        "reason TEXT,"
        "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
    );

    // ------------------------------------
    // ALGORITHM HISTORY TABLE
    // ------------------------------------

    execute(
        "CREATE TABLE IF NOT EXISTS algorithm_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "old_algorithm TEXT,"
        "new_algorithm TEXT NOT NULL,"
        "reason TEXT,"
        "changed_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
    );

    // ------------------------------------
    // OPERATION HISTORY TABLE
    // ------------------------------------

    execute(
        "CREATE TABLE IF NOT EXISTS operation_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "operation TEXT NOT NULL,"
        "details TEXT,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
    );

    // ------------------------------------
    // INSERT DEFAULT ALGORITHM IF NEEDED
    // ------------------------------------

    execute(
        "INSERT OR IGNORE INTO algorithm_state "
        "(id, algorithm, reason) "
        "VALUES (1, 'ROUND_ROBIN', 'Default algorithm');"
    );

    // ------------------------------------
    // INSERT DEFAULT BACKEND SERVERS
    // ------------------------------------

    execute(
        "INSERT OR IGNORE INTO servers "
        "(server_id, status, active_requests, total_requests, "
        "avg_response_time) "
        "VALUES "
        "(1, 'UP', 0, 0, 0.0),"
        "(2, 'UP', 0, 0, 0.0),"
        "(3, 'UP', 0, 0, 0.0);"
    );

    cout << "Database tables initialized successfully." << endl;
}


// ========================================
// UPDATE SERVER STATUS
// ========================================

void Database::updateServerStatus(
    int server_id,
    const string& status,
    int active_requests
) {
    if (db == nullptr) {
        return;
    }

    const char* sql =
        "UPDATE servers "
        "SET status = ?, active_requests = ? "
        "WHERE server_id = ?;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr
        ) != SQLITE_OK) {
        cerr << "Failed to prepare server status update."
             << endl;
        return;
    }

    sqlite3_bind_text(
        stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT
    );

    sqlite3_bind_int(stmt, 2, active_requests);
    sqlite3_bind_int(stmt, 3, server_id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        cerr << "Failed to update server status: "
             << sqlite3_errmsg(db) << endl;
    }

    sqlite3_finalize(stmt);
}


// ========================================
// LOG REQUEST INFORMATION
// ========================================

void Database::logRequest(
    int server_id,
    double response_time,
    bool success
) {
    if (db == nullptr) {
        return;
    }

    // Insert request into request_logs
    const char* insertSQL =
        "INSERT INTO request_logs "
        "(server_id, response_time, success) "
        "VALUES (?, ?, ?);";

    sqlite3_stmt* insertStmt = nullptr;

    if (sqlite3_prepare_v2(
            db, insertSQL, -1, &insertStmt, nullptr
        ) != SQLITE_OK) {
        cerr << "Failed to prepare request log."
             << endl;
        return;
    }

    sqlite3_bind_int(insertStmt, 1, server_id);
    sqlite3_bind_double(insertStmt, 2, response_time);
    sqlite3_bind_int(insertStmt, 3, success ? 1 : 0);

    if (sqlite3_step(insertStmt) != SQLITE_DONE) {
        cerr << "Failed to insert request log: "
             << sqlite3_errmsg(db) << endl;

        sqlite3_finalize(insertStmt);
        return;
    }

    sqlite3_finalize(insertStmt);

    // Update server statistics
    const char* updateSQL =
        "UPDATE servers "
        "SET total_requests = total_requests + 1, "
        "avg_response_time = "
        "(avg_response_time * total_requests + ?) "
        "/ (total_requests + 1) "
        "WHERE server_id = ?;";

    sqlite3_stmt* updateStmt = nullptr;

    if (sqlite3_prepare_v2(
            db, updateSQL, -1, &updateStmt, nullptr
        ) != SQLITE_OK) {
        cerr << "Failed to prepare server statistics update."
             << endl;
        return;
    }

    sqlite3_bind_double(updateStmt, 1, response_time);
    sqlite3_bind_int(updateStmt, 2, server_id);

    if (sqlite3_step(updateStmt) != SQLITE_DONE) {
        cerr << "Failed to update server statistics: "
             << sqlite3_errmsg(db) << endl;
    }

    sqlite3_finalize(updateStmt);
}


// ========================================
// SET LOAD-BALANCING ALGORITHM
// AND RECORD ALGORITHM HISTORY
// ========================================

void Database::setAlgorithm(
    const string& new_algorithm,
    const string& reason
) {
    if (db == nullptr) {
        cerr << "Cannot change algorithm: database unavailable."
             << endl;
        return;
    }

    // Read the previous algorithm
    string old_algorithm = getAlgorithm();

    // Update the current algorithm
    const char* updateSQL =
        "INSERT INTO algorithm_state "
        "(id, algorithm, reason, updated_at) "
        "VALUES (1, ?, ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(id) DO UPDATE SET "
        "algorithm = excluded.algorithm, "
        "reason = excluded.reason, "
        "updated_at = CURRENT_TIMESTAMP;";

    sqlite3_stmt* updateStmt = nullptr;

    if (sqlite3_prepare_v2(
            db, updateSQL, -1, &updateStmt, nullptr
        ) != SQLITE_OK) {
        cerr << "Failed to prepare algorithm update: "
             << sqlite3_errmsg(db) << endl;
        return;
    }

    sqlite3_bind_text(
        updateStmt, 1, new_algorithm.c_str(),
        -1, SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        updateStmt, 2, reason.c_str(),
        -1, SQLITE_TRANSIENT
    );

    if (sqlite3_step(updateStmt) != SQLITE_DONE) {
        cerr << "Failed to update algorithm: "
             << sqlite3_errmsg(db) << endl;

        sqlite3_finalize(updateStmt);
        return;
    }

    sqlite3_finalize(updateStmt);

    // Record history only when the algorithm changes
    if (old_algorithm != new_algorithm) {
        const char* historySQL =
            "INSERT INTO algorithm_history "
            "(old_algorithm, new_algorithm, reason) "
            "VALUES (?, ?, ?);";

        sqlite3_stmt* historyStmt = nullptr;

        if (sqlite3_prepare_v2(
                db, historySQL, -1, &historyStmt, nullptr
            ) != SQLITE_OK) {
            cerr << "Failed to prepare algorithm history."
                 << endl;
            return;
        }

        sqlite3_bind_text(
            historyStmt, 1, old_algorithm.c_str(),
            -1, SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            historyStmt, 2, new_algorithm.c_str(),
            -1, SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            historyStmt, 3, reason.c_str(),
            -1, SQLITE_TRANSIENT
        );

        if (sqlite3_step(historyStmt) != SQLITE_DONE) {
            cerr << "Failed to record algorithm history: "
                 << sqlite3_errmsg(db) << endl;
        }

        sqlite3_finalize(historyStmt);

        // Record the operation
        const char* operationSQL =
            "INSERT INTO operation_history "
            "(operation, details) VALUES (?, ?);";

        sqlite3_stmt* operationStmt = nullptr;

        if (sqlite3_prepare_v2(
                db, operationSQL, -1, &operationStmt, nullptr
            ) == SQLITE_OK) {

            string details =
                "Algorithm changed from " + old_algorithm +
                " to " + new_algorithm +
                ". Reason: " + reason;

            sqlite3_bind_text(
                operationStmt, 1, "ALGORITHM_CHANGE",
                -1, SQLITE_STATIC
            );

            sqlite3_bind_text(
                operationStmt, 2, details.c_str(),
                -1, SQLITE_TRANSIENT
            );

            if (sqlite3_step(operationStmt) != SQLITE_DONE) {
                cerr << "Failed to record operation history."
                     << endl;
            }

            sqlite3_finalize(operationStmt);
        }

        cout << "Algorithm changed: "
             << old_algorithm << " -> " << new_algorithm
             << endl;
    }
}


// ========================================
// GET CURRENT ALGORITHM
// ========================================

string Database::getAlgorithm() {
    if (db == nullptr) {
        return "ROUND_ROBIN";
    }

    const char* sql =
        "SELECT algorithm FROM algorithm_state WHERE id = 1;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr
        ) != SQLITE_OK) {
        return "ROUND_ROBIN";
    }

    string algorithm = "ROUND_ROBIN";

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* value =
            sqlite3_column_text(stmt, 0);

        if (value != nullptr) {
            algorithm = reinterpret_cast<const char*>(value);
        }
    }

    sqlite3_finalize(stmt);

    return algorithm;
}


// ========================================
// DISPLAY REQUEST LOGS
// ========================================

void Database::displayRequestLogs() {
    if (db == nullptr) {
        cerr << "Database is not connected." << endl;
        return;
    }

    const char* sql =
        "SELECT id, server_id, response_time, success, timestamp "
        "FROM request_logs "
        "ORDER BY id DESC;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr
        ) != SQLITE_OK) {
        cerr << "Failed to retrieve request logs: "
             << sqlite3_errmsg(db) << endl;
        return;
    }

    cout << "\n========== REQUEST LOGS ==========\n";

    cout << left
         << setw(8)  << "ID"
         << setw(12) << "Server"
         << setw(18) << "Response (ms)"
         << setw(12) << "Status"
         << "Timestamp"
         << endl;

    cout << string(75, '-') << endl;

    bool found = false;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;

        int id = sqlite3_column_int(stmt, 0);
        int server_id = sqlite3_column_int(stmt, 1);
        double response_time = sqlite3_column_double(stmt, 2);
        int success = sqlite3_column_int(stmt, 3);

        const unsigned char* timestamp =
            sqlite3_column_text(stmt, 4);

        cout << left
             << setw(8) << id
             << setw(12) << server_id
             << setw(18) << fixed << setprecision(2)
             << response_time
             << setw(12) << (success ? "SUCCESS" : "FAILED")
             << (timestamp
                 ? reinterpret_cast<const char*>(timestamp)
                 : "N/A")
             << endl;
    }

    if (!found) {
        cout << "No requests have been logged yet." << endl;
    }

    cout << "==================================\n";

    sqlite3_finalize(stmt);
}
