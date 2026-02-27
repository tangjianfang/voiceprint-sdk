using System;
using System.Runtime.InteropServices;
using System.Text;

namespace VoicePrintDemo
{
    /// <summary>
    /// P/Invoke wrapper for VoicePrint DLL SDK
    /// </summary>
    public static class VoicePrint
    {
        private const string DLL_NAME = "voiceprint.dll";

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
