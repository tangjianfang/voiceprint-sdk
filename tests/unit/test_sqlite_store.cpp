#include <gtest/gtest.h>
#include "storage/sqlite_store.h"
#include <cmath>
#include <vector>

using namespace vp;

class SqliteStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = "test_voiceprint.db";
        store_.open(db_path_);
    }

    void TearDown() override {
        store_.close();
        std::remove(db_path_);
    }

    SqliteStore store_;
    const char* db_path_;
};

TEST_F(SqliteStoreTest, SaveAndLoad) {
    SpeakerProfile profile;
    profile.speaker_id = "test_speaker";
    profile.embedding = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    profile.enroll_count = 1;

    ASSERT_TRUE(store_.save_speaker(profile));

    SpeakerProfile loaded;
    ASSERT_TRUE(store_.load_speaker("test_speaker", loaded));

    EXPECT_EQ(loaded.speaker_id, "test_speaker");
    EXPECT_EQ(loaded.enroll_count, 1);
    ASSERT_EQ(loaded.embedding.size(), 5u);
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_FLOAT_EQ(loaded.embedding[i], profile.embedding[i]);
    }
}

TEST_F(SqliteStoreTest, SaveAndUpdate) {
    SpeakerProfile profile1;
    profile1.speaker_id = "test_speaker";
    profile1.embedding = {0.1f, 0.2f, 0.3f};
    profile1.enroll_count = 1;

    ASSERT_TRUE(store_.save_speaker(profile1));

    SpeakerProfile profile2;
    profile2.speaker_id = "test_speaker";
    profile2.embedding = {0.4f, 0.5f, 0.6f};
    profile2.enroll_count = 2;

    ASSERT_TRUE(store_.save_speaker(profile2));

    SpeakerProfile loaded;
    ASSERT_TRUE(store_.load_speaker("test_speaker", loaded));
    EXPECT_EQ(loaded.enroll_count, 2);
    EXPECT_FLOAT_EQ(loaded.embedding[0], 0.4f);
}

TEST_F(SqliteStoreTest, Remove) {
    SpeakerProfile profile;
    profile.speaker_id = "to_remove";
    profile.embedding = {1.0f};
    ASSERT_TRUE(store_.save_speaker(profile));
    ASSERT_TRUE(store_.speaker_exists("to_remove"));

    ASSERT_TRUE(store_.remove_speaker("to_remove"));
    ASSERT_FALSE(store_.speaker_exists("to_remove"));
}

TEST_F(SqliteStoreTest, RemoveNonExistent) {
    EXPECT_FALSE(store_.remove_speaker("nonexistent"));
}

TEST_F(SqliteStoreTest, LoadNonExistent) {
    SpeakerProfile profile;
    EXPECT_FALSE(store_.load_speaker("nonexistent", profile));
}

TEST_F(SqliteStoreTest, GetSpeakerCount) {
    EXPECT_EQ(store_.get_speaker_count(), 0);

    SpeakerProfile p1;
    p1.speaker_id = "speaker_1";
    p1.embedding = {1.0f};
    store_.save_speaker(p1);

    EXPECT_EQ(store_.get_speaker_count(), 1);

    SpeakerProfile p2;
    p2.speaker_id = "speaker_2";
    p2.embedding = {2.0f};
    store_.save_speaker(p2);

    EXPECT_EQ(store_.get_speaker_count(), 2);
}

TEST_F(SqliteStoreTest, LoadAllSpeakers) {
    for (int i = 0; i < 5; ++i) {
        SpeakerProfile p;
        p.speaker_id = "speaker_" + std::to_string(i);
        p.embedding = {static_cast<float>(i)};
        p.enroll_count = i + 1;
        store_.save_speaker(p);
    }

    auto speakers = store_.load_all_speakers();
    EXPECT_EQ(speakers.size(), 5u);
}

TEST_F(SqliteStoreTest, EmbeddingPrecision) {
    // Test that float precision is preserved through BLOB storage
    SpeakerProfile profile;
    profile.speaker_id = "precision_test";
    profile.enroll_count = 1;

    // Use values that test float precision
    profile.embedding.resize(192);
    for (int i = 0; i < 192; ++i) {
        profile.embedding[i] = static_cast<float>(i) * 0.00123456789f;
    }

    ASSERT_TRUE(store_.save_speaker(profile));

    SpeakerProfile loaded;
    ASSERT_TRUE(store_.load_speaker("precision_test", loaded));

    ASSERT_EQ(loaded.embedding.size(), 192u);
    for (int i = 0; i < 192; ++i) {
        EXPECT_FLOAT_EQ(loaded.embedding[i], profile.embedding[i])
            << "Mismatch at index " << i;
    }
}
