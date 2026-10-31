#ifndef PLUGIN_ASR_H
#define PLUGIN_ASR_H
#include <stdint.h>
#ifdef _WIN32
#define PLUGIN_ASR_CALL __cdecl
#if defined(PLUGIN_ASR_EXPORTS)
#define PLUGIN_ASR_API __declspec(dllexport)
#else
#define PLUGIN_ASR_API
#endif
#else
#define PLUGIN_ASR_CALL
#define PLUGIN_ASR_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define PLUGIN_ASR_ABI_VERSION 1
#define PLUGIN_ASR_SAMPLE_RATE 16000
#define PLUGIN_ASR_MAX_SAMPLES 32000
    typedef struct plugin_asr_stream plugin_asr_stream;
    enum plugin_asr_status_ {
        PLUGIN_ASR_OK = 0, PLUGIN_ASR_EMPTY = 1, PLUGIN_ASR_EVENT = 2,
        PLUGIN_ASR_BUFFER_SMALL = 3, PLUGIN_ASR_OVERFLOW = 4,
        PLUGIN_ASR_INVALID = -1, PLUGIN_ASR_CLOSED = -2, PLUGIN_ASR_FAILED = -3
    };
    enum plugin_asr_event_ {
        PLUGIN_ASR_PARTIAL = 1, PLUGIN_ASR_FINAL = 2, PLUGIN_ASR_GAP = 3,
        PLUGIN_ASR_ERROR = 4, PLUGIN_ASR_END = 5
    };
    typedef int32_t plugin_asr_status;
    typedef int32_t plugin_asr_event;
    typedef struct plugin_asr_result {
        uint32_t struct_size; /* Set to sizeof(plugin_asr_result) before poll. */
        uint32_t type;
        uint64_t utterance_id;
        uint64_t revision;
        int64_t start_sample;
        int64_t end_sample;
        double inference_ms;
        double queue_ms;
        uint32_t text_bytes; /* Required capacity INCLUDING terminating NUL. */
        uint32_t reserved;
    } plugin_asr_result;

    static_assert(sizeof(float) == 4);
    static_assert(sizeof(double) == 8);

    /* UTF-8 directory (relative to process CWD or absolute), fixed model filenames.
     * Resolved synchronously during open; later CWD changes do not affect the handle.
     * threads: 0=default,
     * 1..64=explicit. open and close may wait; exceptions stay inside DLL.
     * error may be NULL only with zero capacity. Failure clears *out. */
    PLUGIN_ASR_API int32_t PLUGIN_ASR_CALL plugin_asr_open(const char* model_directory,
        int32_t threads, plugin_asr_stream** out, char* error, uint32_t error_capacity);
    /* NEW 16kHz mono float samples, finite [-1,1]. Count is samples, not bytes.
     * Up to 32000 per call. NULL + zero count is valid while accepting input.
     * OVERFLOW means this input WAS accepted after an explicit stream break. */
    PLUGIN_ASR_API int32_t PLUGIN_ASR_CALL plugin_asr_push(plugin_asr_stream*, const float*, uint32_t count);
    /* Single consumer. EMPTY clears metadata except struct_size. BUFFER_SMALL
     * retains a stable event for retry (even empty text needs 1 byte).
     * reset invalidates retained events. All strings copied to host memory. */
    PLUGIN_ASR_API int32_t PLUGIN_ASR_CALL plugin_asr_poll(plugin_asr_stream*, plugin_asr_result*, char*, uint32_t capacity);
    /* Async and idempotent; END follows tail finals. No pushes until reset. */
    PLUGIN_ASR_API int32_t PLUGIN_ASR_CALL plugin_asr_finish(plugin_asr_stream*);
    PLUGIN_ASR_API int32_t PLUGIN_ASR_CALL plugin_asr_reset(plugin_asr_stream*);
    /* Stop all other API callers first. NULL is allowed. */
    PLUGIN_ASR_API void PLUGIN_ASR_CALL plugin_asr_close(plugin_asr_stream*);
    /* One producer + one poller; serialize finish/reset with producer.
     * No host callbacks. IDs increase for handle lifetime, revisions per utterance.
     * GAP clears drafts; ERROR requires reset. */
#ifdef __cplusplus
}
#endif
#endif
