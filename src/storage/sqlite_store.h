#ifndef VP_SQLITE_STORE_H
#define VP_SQLITE_STORE_H

#include "storage/speaker_profile.h"
#include <string>
#include <vector>
#include <memory>

struct sqlite3;

namespace vp {

class SqliteStore {
public:
    SqliteStore();
    ~SqliteStore();

    // Open or create database
    bool open(const std::string& db_path);

    // Close database
    void close();

    // Save/update a speaker profile
    bool save_speaker(const SpeakerProfile& profile);

    // Load a speaker profile by ID
    bool load_speaker(const std::string& speaker_id, SpeakerProfile& profile);

    // Remove a speaker
    bool remove_speaker(const std::string& speaker_id);

    // Load all speakers
    std::vector<SpeakerProfile> load_all_speakers();

    // Get speaker count
    int get_speaker_count();

    // Check if speaker exists
    bool speaker_exists(const std::string& speaker_id);

    const std::string& last_error() const { return last_error_; }

private:
    bool create_tables();

    sqlite3* db_ = nullptr;
    std::string last_error_;
};

} // namespace vp

#endif // VP_SQLITE_STORE_H
