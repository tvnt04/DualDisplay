#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  KYFGLib_Stub.h
//  Mirrors the real KYFGLib.h API surface.
//  All functions are no-ops / stubs.
//  When real hardware arrives:
//    1. Remove this file from CMakeLists.txt
//    2. Add the real KYFGLib include path + library
//    3. Change #include "KYFGLib_Stub.h" → #include "KYFGLibA.h"
//       in CameraWorker.cpp ONLY.
//  Everything else stays identical.
// ═══════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <cstddef>

// ── Handle types ────────────────────────────────────────────────────────
using FGHANDLE            = int;
using CAMHANDLE           = int;
using STREAM_HANDLE       = int;
using STREAM_BUFFER_HANDLE= int;

#define INVALID_FGHANDLE          (-1)
#define INVALID_CAMHANDLE         (-1)
#define INVALID_STREAMHANDLE      (-1)
#define NULL_STREAM_BUFFER_HANDLE (0)
#define KY_MAX_CAMERAS            (8)

// ── Status ──────────────────────────────────────────────────────────────
#define FGSTATUS_OK  (0)
#define FGSTATUS_ERR (-1)
using KYFGSTATUS = int;

// ── Queue types ─────────────────────────────────────────────────────────
#define KY_ACQ_QUEUE_INPUT    (1)
#define KY_ACQ_QUEUE_UNQUEUED (2)

// ── Stream info keys ─────────────────────────────────────────────────────
#define KY_STREAM_INFO_PAYLOAD_SIZE  (1)
#define KY_STREAM_INFO_BUF_ALIGNMENT (2)
#define KY_STREAM_INFO_ACTUAL_PAYLOAD_SIZE (3)

// ── Buffer info keys ─────────────────────────────────────────────────────
#define KY_STREAM_BUFFER_INFO_BASE      (1)
#define KY_STREAM_BUFFER_INFO_ID        (2)
#define KY_STREAM_BUFFER_INFO_SIZE      (3)
#define KY_STREAM_BUFFER_INFO_TIMESTAMP (4)
#define KY_STREAM_BUFFER_INFO_IMAGEID   (5)

// ── Callback type ────────────────────────────────────────────────────────
using StreamCallbackFunc = void(*)(STREAM_BUFFER_HANDLE, void*);

// ── Stub implementations (all inline no-ops) ────────────────────────────
inline int  KY_DeviceScan(int* count)                    { *count = 0; return FGSTATUS_OK; }
inline FGHANDLE KYFG_Open(int)                           { return INVALID_FGHANDLE; }
inline KYFGSTATUS KYFG_Close(FGHANDLE)                   { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_UpdateCameraList(FGHANDLE, CAMHANDLE*, int* n) { *n=0; return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_CameraOpen2(CAMHANDLE, void*)     { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_CameraClose(CAMHANDLE)            { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_StreamCreate(CAMHANDLE, STREAM_HANDLE* h, int) { *h=1; return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_StreamDelete(STREAM_HANDLE)        { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_StreamBufferCallbackRegister(STREAM_HANDLE, StreamCallbackFunc, void*) { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_StreamBufferCallbackUnregister(STREAM_HANDLE, StreamCallbackFunc)      { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_StreamGetInfo(STREAM_HANDLE, int key, void* out, void*, void*)
{
    if (key == KY_STREAM_INFO_PAYLOAD_SIZE)  *reinterpret_cast<size_t*>(out) = 1024*1024;
    if (key == KY_STREAM_INFO_ACTUAL_PAYLOAD_SIZE) *reinterpret_cast<size_t*>(out) = 1024*1024;
    if (key == KY_STREAM_INFO_BUF_ALIGNMENT) *reinterpret_cast<size_t*>(out) = 4096;
    return FGSTATUS_OK;
}
inline KYFGSTATUS KYFG_BufferAnnounce(STREAM_HANDLE, void*, size_t, void*, STREAM_BUFFER_HANDLE* h) { *h=1; return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_BufferQueueAll(STREAM_HANDLE, int, int)  { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_BufferToQueue(STREAM_BUFFER_HANDLE, int) { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_BufferGetInfo(STREAM_BUFFER_HANDLE, int, void*, void*, void*) { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_CameraStart(CAMHANDLE, STREAM_HANDLE, int) { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_CameraStop(CAMHANDLE)              { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_SetCameraValueInt  (CAMHANDLE, const char*, int)     { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_SetCameraValueFloat(CAMHANDLE, const char*, float)   { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_SetCameraValueEnum (CAMHANDLE, const char*, int64_t) { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_SetCameraValueEnum_ByValueName(CAMHANDLE, const char*, const char*) { return FGSTATUS_OK; }
inline KYFGSTATUS KYFG_SetCameraValueBool(CAMHANDLE, const char*, bool) { return FGSTATUS_OK; }
inline int        KYFG_GetCameraValueInt  (CAMHANDLE, const char*)  { return 0; }
inline float      KYFG_GetCameraValueFloat(CAMHANDLE, const char*)  { return 0.0f; }
inline float      KYFG_GetGrabberValueFloat(FGHANDLE, const char*)  { return 0.0f; }
inline int64_t    KYFG_GetGrabberValueInt(FGHANDLE, const char*)    { return 0; }
