#ifndef DLSSG_FRAME_TELEMETRY_API_H
#define DLSSG_FRAME_TELEMETRY_API_H
/* Read-only ABI. Rates describe successful, unique slSetConstants submissions,
 * not GPU completion, input latency, or generated/presented frame counts. */
#define DLSSG_FRAME_TELEMETRY_VERSION 1u
#define DLSSG_TELEMETRY_ENABLED 1u
#define DLSSG_TELEMETRY_BASE_VALID 2u
typedef struct DLSSGFrameTelemetry {
    unsigned int size,version,flags,viewport;
    double base_fps;
    unsigned int sample_frames,sample_ms,age_ms,reserved;
} DLSSGFrameTelemetry;
#endif
