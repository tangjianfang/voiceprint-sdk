#ifndef VOICEPRINT_TYPES_H
#define VOICEPRINT_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Feature flags for vp_analyze() / vp_init_analyzer()
// ============================================================
#define VP_FEATURE_GENDER        0x001u
#define VP_FEATURE_AGE           0x002u
#define VP_FEATURE_EMOTION       0x004u
#define VP_FEATURE_ANTISPOOF     0x008u
#define VP_FEATURE_QUALITY       0x010u
#define VP_FEATURE_VOICE_FEATS   0x020u
#define VP_FEATURE_PLEASANTNESS  0x040u
#define VP_FEATURE_VOICE_STATE   0x080u
#define VP_FEATURE_LANGUAGE      0x100u
#define VP_FEATURE_ALL           0x1FFu

// ============================================================
// Gender constants
// ============================================================
#define VP_GENDER_FEMALE   0
#define VP_GENDER_MALE     1
#define VP_GENDER_CHILD    2

// ============================================================
// Age group constants
// ============================================================
#define VP_AGE_GROUP_CHILD    0   // 0-12
#define VP_AGE_GROUP_TEEN     1   // 13-17
#define VP_AGE_GROUP_ADULT    2   // 18-59
#define VP_AGE_GROUP_ELDER    3   // 60+

// ============================================================
// Emotion constants
// ============================================================
#define VP_EMOTION_NEUTRAL    0
#define VP_EMOTION_HAPPY      1
#define VP_EMOTION_SAD        2
#define VP_EMOTION_ANGRY      3
#define VP_EMOTION_FEARFUL    4
#define VP_EMOTION_DISGUSTED  5
#define VP_EMOTION_SURPRISED  6
#define VP_EMOTION_CALM       7
#define VP_EMOTION_COUNT      8

// ============================================================
// Voice state constants
// ============================================================
#define VP_FATIGUE_NORMAL    0
#define VP_FATIGUE_MODERATE  1
#define VP_FATIGUE_HIGH      2

#define VP_HEALTH_NORMAL     0
#define VP_HEALTH_HOARSE     1
#define VP_HEALTH_NASAL      2
#define VP_HEALTH_BREATHY    3

#define VP_STRESS_LOW        0
#define VP_STRESS_MEDIUM     1
#define VP_STRESS_HIGH       2

// ============================================================
// Result structures (all POD / C-compatible)
// ============================================================

/** Gender recognition result */
typedef struct VpGenderResult {
    int   gender;       /**< VP_GENDER_* */
    float scores[3];    /**< softmax scores: [female, male, child] */
    int   reserved[2];
} VpGenderResult;

/** Age estimation result */
typedef struct VpAgeResult {
    int   age_years;    /**< estimated age in years */
    int   age_group;    /**< VP_AGE_GROUP_* */
    float confidence;   /**< [0,1] confidence of age group */
    float group_scores[4]; /**< per-group probabilities */
    int   reserved[2];
} VpAgeResult;

/** Emotion recognition result */
typedef struct VpEmotionResult {
    int   emotion_id;            /**< VP_EMOTION_* - dominant emotion */
    float scores[VP_EMOTION_COUNT]; /**< per-emotion probability [0,1] */
    float valence;               /**< [-1,1] negative→positive */
    float arousal;               /**< [-1,1] calm→excited */
    int   reserved[2];
} VpEmotionResult;

/** Anti-spoofing / liveness detection result */
typedef struct VpAntiSpoofResult {
    int   is_genuine;     /**< 1=real speaker, 0=spoof (recording/TTS) */
    float genuine_score;  /**< [0,1] probability of genuine speech */
    float spoof_score;    /**< [0,1] probability of spoofed speech */
    int   reserved[2];
} VpAntiSpoofResult;

/** Voice quality assessment result */
typedef struct VpQualityResult {
    float mos_score;      /**< Mean Opinion Score [1,5] */
    float snr_db;         /**< Signal-to-Noise Ratio in dB */
    float clarity;        /**< Clarity/intelligibility [0,1] */
    float noise_level;    /**< Background noise level [0,1] */
    float loudness_lufs;  /**< Integrated loudness (ITU-R BS.1770-4) in LUFS */
    float hnr_db;         /**< Harmonics-to-Noise Ratio in dB */
    int   reserved[2];
} VpQualityResult;

/** Acoustic voice feature analysis */
typedef struct VpVoiceFeatures {
    float pitch_hz;           /**< Mean fundamental frequency F0 in Hz (0=unvoiced) */
    float pitch_variability;  /**< F0 std dev in Hz (expressiveness indicator) */
    float speaking_rate;      /**< Estimated syllables per second */
    float voice_stability;    /**< Jitter/shimmer based stability [0,1] */
    float resonance_score;    /**< Chest/head resonance ratio [0,1] */
    float breathiness;        /**< Breathiness index [0,1] */
    float energy_mean;        /**< Mean RMS energy */
    float energy_variability; /**< Energy variability (dynamic range indicator) */
    int   reserved[2];
} VpVoiceFeatures;

/** Voice pleasantness / attractiveness evaluation */
typedef struct VpPleasantnessResult {
    float overall_score;  /**< Composite score [0,100] */
    float magnetism;      /**< Magnetic/charismatic quality [0,100] */
    float warmth;         /**< Warmth/friendliness [0,100] */
    float authority;      /**< Authoritative/trustworthy [0,100] */
    float clarity_score;  /**< Vocal clarity [0,100] */
    int   reserved[2];
} VpPleasantnessResult;

/** Voice state / condition detection */
typedef struct VpVoiceState {
    int   fatigue_level;  /**< VP_FATIGUE_* */
    int   health_state;   /**< VP_HEALTH_* */
    int   stress_level;   /**< VP_STRESS_* */
    float fatigue_score;  /**< [0,1] continuous fatigue score */
    float stress_score;   /**< [0,1] continuous stress score */
    float health_score;   /**< [0,1] vocal health score (1=healthy) */
    int   reserved[2];
} VpVoiceState;

/** Language / accent identification result */
typedef struct VpLanguageResult {
    char  language[16];      /**< ISO 639-1 code, e.g. "zh", "en" */
    char  language_name[64]; /**< Human readable, e.g. "Chinese" */
    float confidence;        /**< [0,1] language detection confidence */
    float accent_score;      /**< [0,1] accent strength (0=standard, 1=heavy) */
    char  accent_region[64]; /**< e.g. "Mandarin", "Cantonese", "British EN" */
    int   reserved[2];
} VpLanguageResult;

/** Single diarization segment (one speaker's speech interval) */
typedef struct VpDiarizeSegment {
    float start_sec;        /**< Segment start time in seconds */
    float end_sec;          /**< Segment end time in seconds */
    char  speaker_label[64]; /**< Auto-assigned label, e.g. "SPEAKER_0" */
    char  speaker_id[128];  /**< Matched registered speaker ID (empty if unknown) */
    float confidence;       /**< [0,1] speaker assignment confidence */
    int   reserved[2];
} VpDiarizeSegment;

/** Aggregated analysis result from vp_analyze() */
typedef struct VpAnalysisResult {
    unsigned int     features_computed; /**< Bitmask of VP_FEATURE_* flags actually computed */
    VpGenderResult       gender;
    VpAgeResult          age;
    VpEmotionResult      emotion;
    VpAntiSpoofResult    antispoof;
    VpQualityResult      quality;
    VpVoiceFeatures      voice_features;
    VpPleasantnessResult pleasantness;
    VpVoiceState         voice_state;
    VpLanguageResult     language;
    int                  reserved[4];
} VpAnalysisResult;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VOICEPRINT_TYPES_H
