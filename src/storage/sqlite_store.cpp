#include "storage/sqlite_store.h"
#include "utils/logger.h"
#include <sqlite3.h>
#include <cstring>

namespace vp {

SqliteStore::SqliteStore() = default;

SqliteStore::~SqliteStore() {
    close();
}

bool SqliteStore::open(const std::string& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        last_error_ = "Cannot open database: " + std::string(sqlite3_errmsg(db_));
        VP_LOG_ERROR(last_error_);
        return false;
    }

    // Enable WAL mode
    char* err_msg = nullptr;
    rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        VP_LOG_WARN("Failed to enable WAL mode: {}", err_msg ? err_msg : "unknown");
        if (err_msg) sqlite3_free(err_msg);
    }

    // Set busy timeout
    sqlite3_busy_timeout(db_, 5000);

    if (!create_tables()) {
        return false;
    }

    VP_LOG_INFO("Database opened: {}", db_path);
    return true;
}

void SqliteStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        VP_LOG_INFO("Database closed");
    }
}

bool SqliteStore::create_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS speakers (
            speaker_id TEXT PRIMARY KEY,
            embedding BLOB NOT NULL,
            embedding_dim INTEGER NOT NULL,
            enroll_count INTEGER DEFAULT 1,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )";

    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        last_error_ = "Failed to create tables: " + std::string(err_msg ? err_msg : "unknown");
        VP_LOG_ERROR(last_error_);
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }

    return true;
}

bool SqliteStore::save_speaker(const SpeakerProfile& profile) {
    const char* sql = R"(
        INSERT OR REPLACE INTO speakers (speaker_id, embedding, embedding_dim, enroll_count, updated_at)
        VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP);
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = "SQL prepare error: " + std::string(sqlite3_errmsg(db_));
        VP_LOG_ERROR(last_error_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, profile.speaker_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, profile.embedding.data(),
                      static_cast<int>(profile.embedding.size() * sizeof(float)),
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(profile.embedding.size()));
    sqlite3_bind_int(stmt, 4, profile.enroll_count);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        last_error_ = "SQL exec error: " + std::string(sqlite3_errmsg(db_));
        VP_LOG_ERROR(last_error_);
        return false;
    }

    VP_LOG_DEBUG("Saved speaker: {} (dim={}, count={})",
                 profile.speaker_id, profile.embedding.size(), profile.enroll_count);
    return true;
}

bool SqliteStore::load_speaker(const std::string& speaker_id, SpeakerProfile& profile) {
    const char* sql = "SELECT speaker_id, embedding, embedding_dim, enroll_count FROM speakers WHERE speaker_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = "SQL prepare error: " + std::string(sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_text(stmt, 1, speaker_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        profile.speaker_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

        const void* blob = sqlite3_column_blob(stmt, 1);
        int blob_size = sqlite3_column_bytes(stmt, 1);
        int dim = sqlite3_column_int(stmt, 2);
        profile.enroll_count = sqlite3_column_int(stmt, 3);

        profile.embedding.resize(dim);
        std::memcpy(profile.embedding.data(), blob, blob_size);

        sqlite3_finalize(stmt);
        return true;
    }

    sqlite3_finalize(stmt);
    last_error_ = "Speaker not found: " + speaker_id;
    return false;
}

bool SqliteStore::remove_speaker(const std::string& speaker_id) {
    const char* sql = "DELETE FROM speakers WHERE speaker_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = "SQL prepare error: " + std::string(sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_text(stmt, 1, speaker_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        last_error_ = "SQL exec error: " + std::string(sqlite3_errmsg(db_));
        return false;
    }

    if (changes == 0) {
        last_error_ = "Speaker not found: " + speaker_id;
        return false;
    }

    VP_LOG_INFO("Removed speaker: {}", speaker_id);
    return true;
}

std::vector<SpeakerProfile> SqliteStore::load_all_speakers() {
    std::vector<SpeakerProfile> speakers;
    const char* sql = "SELECT speaker_id, embedding, embedding_dim, enroll_count FROM speakers;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = "SQL prepare error: " + std::string(sqlite3_errmsg(db_));
        return speakers;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SpeakerProfile profile;
        profile.speaker_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

        const void* blob = sqlite3_column_blob(stmt, 1);
        int blob_size = sqlite3_column_bytes(stmt, 1);
        int dim = sqlite3_column_int(stmt, 2);
        profile.enroll_count = sqlite3_column_int(stmt, 3);

        profile.embedding.resize(dim);
        std::memcpy(profile.embedding.data(), blob, blob_size);

        speakers.push_back(std::move(profile));
    }

    sqlite3_finalize(stmt);
    VP_LOG_INFO("Loaded {} speakers from database", speakers.size());
    return speakers;
}

int SqliteStore::get_speaker_count() {
    const char* sql = "SELECT COUNT(*) FROM speakers;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

bool SqliteStore::speaker_exists(const std::string& speaker_id) {
    const char* sql = "SELECT 1 FROM speakers WHERE speaker_id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, speaker_id.c_str(), -1, SQLITE_TRANSIENT);

    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

} // namespace vp
