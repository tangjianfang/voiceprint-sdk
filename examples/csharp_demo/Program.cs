using System;
using System.Runtime.InteropServices;
using System.Text;

namespace VoicePrintDemo
{
    // ================================================================
    // Feature flag constants (mirrors VP_FEATURE_* in voiceprint_types.h)
    // ================================================================
    public static class VpFeature
    {
        public const uint Gender      = 0x001u;
        public const uint Age         = 0x002u;
        public const uint Emotion     = 0x004u;
        public const uint AntiSpoof   = 0x008u;
        public const uint Quality     = 0x010u;
        public const uint VoiceFeats  = 0x020u;
        public const uint Pleasantness= 0x040u;
        public const uint VoiceState  = 0x080u;
        public const uint Language    = 0x100u;
        public const uint All         = 0x1FFu;
    }

    // ================================================================
    // Gender / Age / Emotion / State class constants
    // ================================================================
    public static class VpConst
    {
        public const int GenderFemale  = 0;
        public const int GenderMale    = 1;
        public const int GenderChild   = 2;

        public const int AgeChild      = 0;
        public const int AgeTeen       = 1;
        public const int AgeAdult      = 2;
        public const int AgeElder      = 3;

        public const int EmotionNeutral   = 0;
        public const int EmotionHappy     = 1;
        public const int EmotionSad       = 2;
        public const int EmotionAngry     = 3;
        public const int EmotionFearful   = 4;
        public const int EmotionDisgusted = 5;
        public const int EmotionSurprised = 6;
        public const int EmotionCalm      = 7;

        public const int FatigueNormal   = 0;
        public const int FatigueModerate = 1;
        public const int FatigueHigh     = 2;

        public const int HealthNormal    = 0;
        public const int HealthHoarse    = 1;
        public const int HealthNasal     = 2;
        public const int HealthBreathy   = 3;

        public const int StressLow       = 0;
        public const int StressMedium    = 1;
        public const int StressHigh      = 2;
    }

    // ================================================================
    // Result structures (must exactly match C struct layout)
    // ================================================================

    [StructLayout(LayoutKind.Sequential)]
    public struct VpGenderResult
    {
        public int    Gender;          // VpConst.Gender*
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
        public float[] Scores;         // [female, male, child]
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public int[]   Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct VpAgeResult
    {
        public int    AgeYears;
        public int    AgeGroup;        // VpConst.Age*
        public float  Confidence;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
        public float[] GroupScores;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public int[]   Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct VpEmotionResult
    {
        public int   EmotionId;        // VpConst.Emotion*
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
        public float[] Scores;
        public float Valence;          // [-1,1]
        public float Arousal;          // [-1,1]
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public int[]  Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct VpAntiSpoofResult
    {
        public int   IsGenuine;        // 1=real, 0=spoof
        public float GenuineScore;
        public float SpoofScore;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public int[] Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct VpQualityResult
    {
        public float MosScore;
        public float SnrDb;
        public float Clarity;
        public float NoiseLevel;
        public float LoudnessLufs;
        public float HnrDb;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public int[] Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct VpVoiceFeatures
    {
        public float PitchHz;
        public float PitchVariability;
        public float SpeakingRate;
        public float VoiceStability;
        public float ResonanceScore;
        public float Breathiness;
        public float EnergyMean;
        public float EnergyVariability;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public int[] Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct VpPleasantnessResult
    {
        public float OverallScore;
        public float Magnetism;
        public float Warmth;
        public float Authority;
        public float ClarityScore;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public int[] Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct VpVoiceState
    {
        public int   FatigueLevel;     // VpConst.Fatigue*
        public int   HealthState;      // VpConst.Health*
        public int   StressLevel;      // VpConst.Stress*
        public float FatigueScore;
        public float StressScore;
        public float HealthScore;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public int[] Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct VpLanguageResult
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)]
        public string Language;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string LanguageName;
        public float  Confidence;
        public float  AccentScore;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string AccentRegion;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public int[]  Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct VpDiarizeSegment
    {
        public float StartSec;
        public float EndSec;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string SpeakerLabel;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string SpeakerId;
        public float  Confidence;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public int[]  Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct VpAnalysisResult
    {
        public uint                FeaturesComputed;
        public VpGenderResult      Gender;
        public VpAgeResult         Age;
        public VpEmotionResult     Emotion;
        public VpAntiSpoofResult   AntiSpoof;
        public VpQualityResult     Quality;
        public VpVoiceFeatures     VoiceFeatures;
        public VpPleasantnessResult Pleasantness;
        public VpVoiceState        VoiceState;
        public VpLanguageResult    Language;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
        public int[]               Reserved;
    }

    // ================================================================
    // P/Invoke wrapper
    // ================================================================
    public static class VoicePrint
    {
        private const string DLL_NAME = "voiceprint.dll";

        // ── Error codes ──────────────────────────────────────────────
        public const int VP_OK                        =  0;
        public const int VP_ERROR_UNKNOWN             = -1;
        public const int VP_ERROR_INVALID_PARAM       = -2;
        public const int VP_ERROR_NOT_INIT            = -3;
        public const int VP_ERROR_ALREADY_INIT        = -4;
        public const int VP_ERROR_MODEL_LOAD          = -5;
        public const int VP_ERROR_AUDIO_TOO_SHORT     = -6;
        public const int VP_ERROR_AUDIO_INVALID       = -7;
        public const int VP_ERROR_SPEAKER_EXISTS      = -8;
        public const int VP_ERROR_SPEAKER_NOT_FOUND   = -9;
        public const int VP_ERROR_DB_ERROR            = -10;
        public const int VP_ERROR_FILE_NOT_FOUND      = -11;
        public const int VP_ERROR_BUFFER_TOO_SMALL    = -12;
        public const int VP_ERROR_NO_MATCH            = -13;
        public const int VP_ERROR_WAV_FORMAT          = -14;
        public const int VP_ERROR_INFERENCE           = -15;
        public const int VP_ERROR_MODEL_NOT_AVAILABLE = -16;
        public const int VP_ERROR_ANALYSIS_FAILED     = -17;
        public const int VP_ERROR_DIARIZE_FAILED      = -18;

        // ── Core lifecycle ───────────────────────────────────────────
        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_init(string model_dir, string db_path);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void vp_release();

        // ── Voiceprint (existing) ─────────────────────────────────────
        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_enroll(string speaker_id, float[] pcm_data, int sample_count);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_enroll_file(string speaker_id, string wav_path);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_remove_speaker(string speaker_id);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_identify(float[] pcm_data, int sample_count,
            StringBuilder out_speaker_id, int id_buf_size, out float out_score);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_verify(string speaker_id,
            float[] pcm_data, int sample_count, out float out_score);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_set_threshold(float threshold);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_get_speaker_count();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr vp_get_last_error();

        // ── Voice Analysis ────────────────────────────────────────────
        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_init_analyzer(uint feature_flags);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_analyze(float[] pcm_data, int sample_count,
            uint feature_flags, out VpAnalysisResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_analyze_file(string wav_path,
            uint feature_flags, out VpAnalysisResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_get_gender(float[] pcm_data, int sample_count,
            out VpGenderResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_get_gender_file(string wav_path, out VpGenderResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_get_age(float[] pcm_data, int sample_count,
            out VpAgeResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_get_age_file(string wav_path, out VpAgeResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_get_emotion(float[] pcm_data, int sample_count,
            out VpEmotionResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_get_emotion_file(string wav_path, out VpEmotionResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr vp_emotion_name(int emotion_id);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_anti_spoof(float[] pcm_data, int sample_count,
            out VpAntiSpoofResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_anti_spoof_file(string wav_path, out VpAntiSpoofResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_set_antispoof_enabled(int enabled);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_assess_quality(float[] pcm_data, int sample_count,
            out VpQualityResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_assess_quality_file(string wav_path, out VpQualityResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_analyze_voice(float[] pcm_data, int sample_count,
            out VpVoiceFeatures result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_analyze_voice_file(string wav_path, out VpVoiceFeatures result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_get_pleasantness(float[] pcm_data, int sample_count,
            out VpPleasantnessResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_get_pleasantness_file(string wav_path,
            out VpPleasantnessResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_get_voice_state(float[] pcm_data, int sample_count,
            out VpVoiceState result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_get_voice_state_file(string wav_path, out VpVoiceState result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_detect_language(float[] pcm_data, int sample_count,
            out VpLanguageResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_detect_language_file(string wav_path,
            out VpLanguageResult result);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern IntPtr vp_language_name(string lang_code);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_diarize(float[] pcm_data, int sample_count,
            [Out, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 3)]
            VpDiarizeSegment[] out_segments, int max_segments, out int out_count);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_diarize_file(string wav_path,
            [Out, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)]
            VpDiarizeSegment[] out_segments, int max_segments, out int out_count);

        // ── Helpers ───────────────────────────────────────────────────
        public static string GetLastError()
            => Marshal.PtrToStringAnsi(vp_get_last_error()) ?? string.Empty;

        public static string EmotionName(int id)
            => Marshal.PtrToStringAnsi(vp_emotion_name(id)) ?? "unknown";

        public static string LanguageName(string code)
            => Marshal.PtrToStringAnsi(vp_language_name(code)) ?? code;
    }

    // ================================================================
    // Demo program
    // ================================================================
    class Program
    {
        static void Main(string[] args)
        {
            Console.WriteLine("=== VoicePrint SDK C# Demo (v2.0) ===\n");

            string modelDir = args.Length > 0 ? args[0] : "models";
            string dbPath   = args.Length > 1 ? args[1] : "voiceprint_demo.db";

            // ── 1. Initialize core SDK ──────────────────────────────
            Console.WriteLine("[1] Initializing VoicePrint SDK...");
            int rc = VoicePrint.vp_init(modelDir, dbPath);
            if (rc != VoicePrint.VP_OK)
            {
                Console.Error.WriteLine($"Init failed ({rc}): {VoicePrint.GetLastError()}");
                return;
            }
            Console.WriteLine("    Core SDK initialized.\n");

            // ── 2. Initialize voice analyzer ───────────────────────
            Console.WriteLine("[2] Initializing voice analyzer (all features)...");
            rc = VoicePrint.vp_init_analyzer(VpFeature.All);
            if (rc == VoicePrint.VP_OK)
                Console.WriteLine("    VoiceAnalyzer ready.\n");
            else
                Console.WriteLine($"    VoiceAnalyzer partially ready (some models missing): {VoicePrint.GetLastError()}\n");

            // ── 3. Speaker enrollment demo ─────────────────────────
            if (args.Length >= 4)
            {
                string wav1 = args[2], wav2 = args[3];
                Console.WriteLine("[3] Enrolling speakers from WAV files...");
                rc = VoicePrint.vp_enroll_file("Alice", wav1);
                Console.WriteLine(rc == VoicePrint.VP_OK
                    ? "    Alice enrolled." : $"    Alice failed: {VoicePrint.GetLastError()}");
                rc = VoicePrint.vp_enroll_file("Bob", wav2);
                Console.WriteLine(rc == VoicePrint.VP_OK
                    ? "    Bob enrolled." : $"    Bob failed: {VoicePrint.GetLastError()}");
                Console.WriteLine($"    Total speakers: {VoicePrint.vp_get_speaker_count()}\n");

                // ── 4. Full voice analysis on wav1 ─────────────────
                Console.WriteLine("[4] Full voice analysis on Alice's audio...");
                VpAnalysisResult result;
                rc = VoicePrint.vp_analyze_file(wav1, VpFeature.All, out result);
                if (rc == VoicePrint.VP_OK)
                    PrintAnalysisResult(result);
                else
                    Console.WriteLine($"    Analysis failed: {VoicePrint.GetLastError()}");

                // ── 5. Multi-speaker diarization on wav1 (single speaker) ──
                Console.WriteLine("\n[5] Diarization on Alice's audio...");
                var segs = new VpDiarizeSegment[32];
                rc = VoicePrint.vp_diarize_file(wav1, segs, segs.Length, out int segCount);
                if (rc == VoicePrint.VP_OK)
                {
                    Console.WriteLine($"    {segCount} segment(s) detected:");
                    for (int i = 0; i < segCount; i++)
                        Console.WriteLine($"      [{segs[i].StartSec:F2}s - {segs[i].EndSec:F2}s]  {segs[i].SpeakerLabel}  conf={segs[i].Confidence:F2}");
                }
                else
                    Console.WriteLine($"    Diarization failed: {VoicePrint.GetLastError()}");

                // Cleanup
                VoicePrint.vp_remove_speaker("Alice");
                VoicePrint.vp_remove_speaker("Bob");
            }
            else
            {
                // ── Minimal demo with synthetic audio ──────────────
                Console.WriteLine("[3] No WAV files provided — running DSP-only demo...");
                Console.WriteLine("    Usage: VoicePrintDemo <model_dir> <db_path> <alice.wav> <bob.wav>\n");

                // Generate a synthetic 3s 200 Hz sine (male-like pitch)
                int n = 16000 * 3;
                var pcm = new float[n];
                for (int i = 0; i < n; i++)
                    pcm[i] = 0.4f * (float)Math.Sin(2 * Math.PI * 200.0 * i / 16000);

                Console.WriteLine("    Assessing quality...");
                VpQualityResult q;
                rc = VoicePrint.vp_assess_quality(pcm, n, out q);
                if (rc == VoicePrint.VP_OK)
                {
                    Console.WriteLine($"      MOS        = {q.MosScore:F2}");
                    Console.WriteLine($"      SNR        = {q.SnrDb:F1} dB");
                    Console.WriteLine($"      Loudness   = {q.LoudnessLufs:F1} LUFS");
                    Console.WriteLine($"      Clarity    = {q.Clarity:F2}");
                }

                Console.WriteLine("\n    Analyzing voice features...");
                VpVoiceFeatures vf;
                rc = VoicePrint.vp_analyze_voice(pcm, n, out vf);
                if (rc == VoicePrint.VP_OK)
                {
                    Console.WriteLine($"      Pitch      = {vf.PitchHz:F1} Hz");
                    Console.WriteLine($"      Stability  = {vf.VoiceStability:F2}");
                    Console.WriteLine($"      Rate       = {vf.SpeakingRate:F1} syll/s");
                    Console.WriteLine($"      Resonance  = {vf.ResonanceScore:F2}");
                    Console.WriteLine($"      Breathiness= {vf.Breathiness:F2}");
                }

                Console.WriteLine("\n    Evaluating pleasantness...");
                VpPleasantnessResult pl;
                rc = VoicePrint.vp_get_pleasantness(pcm, n, out pl);
                if (rc == VoicePrint.VP_OK)
                {
                    Console.WriteLine($"      Overall    = {pl.OverallScore:F1}/100");
                    Console.WriteLine($"      Magnetism  = {pl.Magnetism:F1}");
                    Console.WriteLine($"      Warmth     = {pl.Warmth:F1}");
                    Console.WriteLine($"      Authority  = {pl.Authority:F1}");
                    Console.WriteLine($"      Clarity    = {pl.ClarityScore:F1}");
                }

                Console.WriteLine("\n    Checking voice state...");
                VpVoiceState vs;
                rc = VoicePrint.vp_get_voice_state(pcm, n, out vs);
                if (rc == VoicePrint.VP_OK)
                {
                    string[] fatigue = { "Normal", "Moderate", "High" };
                    string[] health  = { "Normal", "Hoarse", "Nasal", "Breathy" };
                    string[] stress  = { "Low", "Medium", "High" };
                    Console.WriteLine($"      Fatigue    = {fatigue[vs.FatigueLevel]} ({vs.FatigueScore:F2})");
                    Console.WriteLine($"      Health     = {health[vs.HealthState]} ({vs.HealthScore:F2})");
                    Console.WriteLine($"      Stress     = {stress[vs.StressLevel]} ({vs.StressScore:F2})");
                }
            }

            VoicePrint.vp_release();
            Console.WriteLine("\nDone.");
        }

        static void PrintAnalysisResult(VpAnalysisResult r)
        {
            Console.WriteLine($"    Features computed: 0x{r.FeaturesComputed:X3}");

            if ((r.FeaturesComputed & VpFeature.Gender) != 0)
            {
                string[] gnames = { "Female", "Male", "Child" };
                string gname = r.Gender.Gender < gnames.Length ? gnames[r.Gender.Gender] : "?";
                Console.WriteLine($"    Gender     : {gname}  (conf {r.Gender.Scores?[r.Gender.Gender]:0:F2})");
            }
            if ((r.FeaturesComputed & VpFeature.Age) != 0)
            {
                string[] grpnames = { "Child", "Teen", "Adult", "Elder" };
                string grp = r.Age.AgeGroup < grpnames.Length ? grpnames[r.Age.AgeGroup] : "?";
                Console.WriteLine($"    Age        : ~{r.Age.AgeYears} years  ({grp}, conf {r.Age.Confidence:F2})");
            }
            if ((r.FeaturesComputed & VpFeature.Emotion) != 0)
            {
                Console.WriteLine($"    Emotion    : {VoicePrint.EmotionName(r.Emotion.EmotionId)}  " +
                                  $"valence={r.Emotion.Valence:+0.00;-0.00}  arousal={r.Emotion.Arousal:+0.00;-0.00}");
            }
            if ((r.FeaturesComputed & VpFeature.AntiSpoof) != 0)
            {
                string verdict = r.AntiSpoof.IsGenuine == 1 ? "GENUINE" : "SPOOFED";
                Console.WriteLine($"    Anti-spoof : {verdict}  (genuine={r.AntiSpoof.GenuineScore:F2})");
            }
            if ((r.FeaturesComputed & VpFeature.Quality) != 0)
            {
                Console.WriteLine($"    Quality    : MOS={r.Quality.MosScore:F2}  SNR={r.Quality.SnrDb:F1}dB  " +
                                  $"LUFS={r.Quality.LoudnessLufs:F1}  clarity={r.Quality.Clarity:F2}");
            }
            if ((r.FeaturesComputed & VpFeature.VoiceFeats) != 0)
            {
                Console.WriteLine($"    Voice      : pitch={r.VoiceFeatures.PitchHz:F1}Hz  " +
                                  $"rate={r.VoiceFeatures.SpeakingRate:F1}syll/s  " +
                                  $"stability={r.VoiceFeatures.VoiceStability:F2}  " +
                                  $"breathiness={r.VoiceFeatures.Breathiness:F2}");
            }
            if ((r.FeaturesComputed & VpFeature.Pleasantness) != 0)
            {
                Console.WriteLine($"    Pleasant.  : {r.Pleasantness.OverallScore:F1}/100  " +
                                  $"(magnet={r.Pleasantness.Magnetism:F1}  " +
                                  $"warmth={r.Pleasantness.Warmth:F1}  " +
                                  $"authority={r.Pleasantness.Authority:F1})");
            }
            if ((r.FeaturesComputed & VpFeature.VoiceState) != 0)
            {
                string[] fl = { "Normal", "Moderate fatigued", "Highly fatigued" };
                Console.WriteLine($"    State      : {fl[r.VoiceState.FatigueLevel]}  " +
                                  $"stress={r.VoiceState.StressLevel}  health={r.VoiceState.HealthScore:F2}");
            }
            if ((r.FeaturesComputed & VpFeature.Language) != 0)
            {
                Console.WriteLine($"    Language   : {r.Language.LanguageName} ({r.Language.Language})  " +
                                  $"conf={r.Language.Confidence:F2}  " +
                                  $"accent={r.Language.AccentRegion}");
            }
        }
    }
}


        public const int VP_OK = 0;
        public const int VP_ERROR_UNKNOWN = -1;
        public const int VP_ERROR_INVALID_PARAM = -2;
        public const int VP_ERROR_NOT_INIT = -3;
        public const int VP_ERROR_ALREADY_INIT = -4;
        public const int VP_ERROR_MODEL_LOAD = -5;
        public const int VP_ERROR_AUDIO_TOO_SHORT = -6;
        public const int VP_ERROR_AUDIO_INVALID = -7;
        public const int VP_ERROR_SPEAKER_NOT_FOUND = -9;
        public const int VP_ERROR_NO_MATCH = -13;

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_init(string model_dir, string db_path);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void vp_release();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_enroll(string speaker_id, float[] pcm_data, int sample_count);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_enroll_file(string speaker_id, string wav_path);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_remove_speaker(string speaker_id);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_identify(float[] pcm_data, int sample_count,
            StringBuilder out_speaker_id, int id_buf_size, out float out_score);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int vp_verify(string speaker_id,
            float[] pcm_data, int sample_count, out float out_score);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_set_threshold(float threshold);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int vp_get_speaker_count();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr vp_get_last_error();

        public static string GetLastError()
        {
            IntPtr ptr = vp_get_last_error();
            return Marshal.PtrToStringAnsi(ptr) ?? string.Empty;
        }
    }

    class Program
    {
        static void Main(string[] args)
        {
            Console.WriteLine("=== VoicePrint SDK C# Demo ===\n");

            string modelDir = args.Length > 0 ? args[0] : "models";
            string dbPath = args.Length > 1 ? args[1] : "voiceprint_demo.db";

            // 1. Initialize
            Console.WriteLine("[1] Initializing SDK...");
            int ret = VoicePrint.vp_init(modelDir, dbPath);
            if (ret != VoicePrint.VP_OK)
            {
                Console.WriteLine($"Init failed ({ret}): {VoicePrint.GetLastError()}");
                return;
            }
            Console.WriteLine("SDK initialized!\n");

            // 2. Enroll from WAV files
            if (args.Length >= 4)
            {
                string speaker1Wav = args[2];
                string speaker2Wav = args[3];

                Console.WriteLine("[2] Enrolling speaker_A...");
                ret = VoicePrint.vp_enroll_file("speaker_A", speaker1Wav);
                Console.WriteLine(ret == VoicePrint.VP_OK
                    ? "  Enrolled successfully!"
                    : $"  Failed: {VoicePrint.GetLastError()}");

                Console.WriteLine("[3] Enrolling speaker_B...");
                ret = VoicePrint.vp_enroll_file("speaker_B", speaker2Wav);
                Console.WriteLine(ret == VoicePrint.VP_OK
                    ? "  Enrolled successfully!"
                    : $"  Failed: {VoicePrint.GetLastError()}");

                Console.WriteLine($"\nTotal speakers: {VoicePrint.vp_get_speaker_count()}\n");

                // 3. Verify
                Console.WriteLine("[4] Verifying speaker_A with their own audio...");
                // Would need to load WAV to float[] and call vp_verify

                // 4. Remove
                Console.WriteLine("[5] Removing speaker_B...");
                ret = VoicePrint.vp_remove_speaker("speaker_B");
                Console.WriteLine(ret == VoicePrint.VP_OK
                    ? "  Removed!"
                    : $"  Failed: {VoicePrint.GetLastError()}");
            }
            else
            {
                Console.WriteLine("Usage: VoicePrintDemo <model_dir> <db_path> <speaker1.wav> <speaker2.wav>");
                Console.WriteLine("\nRunning basic API test...");

                VoicePrint.vp_set_threshold(0.30f);
                Console.WriteLine($"Speaker count: {VoicePrint.vp_get_speaker_count()}");
            }

            // 5. Release
            Console.WriteLine("\n[Final] Releasing SDK...");
            VoicePrint.vp_release();
            Console.WriteLine("Done!");
        }
    }
}
