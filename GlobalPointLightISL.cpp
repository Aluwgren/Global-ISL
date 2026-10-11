// Global Point Light ISL 1.5-test22
// Skyrim SE 1.5.97 / SKSE 2.0.20 / Community Shaders and Open Shaders
// Standalone raw SKSE plugin. No CRT, CommonLib, ESP, Papyrus, or Address Library.
//
// Universal point-light architecture:
//   At SKSE startup only, scan CommunityShaders.dll .text for the semantic
//   LightData vector push helper and, where available,
//   InverseSquareLighting::ProcessLight using validated code signatures. The
//   push hook detects the active 80-byte Community Shaders or 96-byte Open
//   Shaders LightData ABI from the vector stride before touching any fields.
//   Hook function entries through a dynamic near trampoline page rather than
//   fixed renderer RVAs or fixed code caves.
//   Open Shaders owns real shadow resources/budgets. This build can force its
//   native normal-light promotion path and caps excessive local-light shadow
//   depth bias at the same validated AddLight gate. Hand/impact magic lights
//   can use dedicated small-radius policies.
//   An optional global radius cap can constrain newly converted and already-ISL
//   point lights; it is disabled by default. Lifecycle admission uses a fixed bounded table and SKSE main-
//   thread tasks; no raw scene pointer is retained or dereferenced across frames.
//   Unsupported/changed renderer internals fail closed.

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using i32 = int;
using u64 = unsigned long long;
using i64 = long long;
using uptr = unsigned long long;
using DWORD = unsigned int;
using BOOL = int;
using HANDLE = void*;
using HMODULE = void*;
using SIZE_T = unsigned long long;

#define WINAPI __stdcall
#define DLLEXPORT extern "C" __declspec(dllexport)

// Required by clang-cl for floating-point code in a no-CRT DLL.
extern "C" { int _fltused = 0; }

extern "C" void* memset(void* destination, int value, SIZE_T count)
{
    auto* p = static_cast<u8*>(destination);
    for (SIZE_T i = 0; i < count; ++i) {
        p[i] = static_cast<u8>(value);
    }
    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, SIZE_T count)
{
    auto* d = static_cast<u8*>(destination);
    const auto* s = static_cast<const u8*>(source);
    for (SIZE_T i = 0; i < count; ++i) {
        d[i] = s[i];
    }
    return destination;
}

extern "C" {
__declspec(dllimport) HANDLE WINAPI CreateFileA(const char*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE, const void*, DWORD, DWORD*, void*);
__declspec(dllimport) BOOL WINAPI ReadFile(HANDLE, void*, DWORD, DWORD*, void*);
__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE);
struct LARGE_INTEGER_ { i64 QuadPart; };
__declspec(dllimport) BOOL WINAPI GetFileSizeEx(HANDLE, LARGE_INTEGER_*);
__declspec(dllimport) BOOL WINAPI SetFilePointerEx(HANDLE, LARGE_INTEGER_, LARGE_INTEGER_*, DWORD);
__declspec(dllimport) HMODULE WINAPI GetModuleHandleA(const char*);
__declspec(dllimport) DWORD WINAPI GetPrivateProfileStringA(const char*, const char*, const char*, char*, DWORD, const char*);
__declspec(dllimport) BOOL WINAPI VirtualProtect(void*, SIZE_T, DWORD, DWORD*);
__declspec(dllimport) void* WINAPI VirtualAlloc(void*, SIZE_T, DWORD, DWORD);
__declspec(dllimport) BOOL WINAPI VirtualFree(void*, SIZE_T, DWORD);
__declspec(dllimport) BOOL WINAPI FlushInstructionCache(HANDLE, const void*, SIZE_T);
__declspec(dllimport) HANDLE WINAPI GetCurrentProcess();
__declspec(dllimport) DWORD WINAPI GetCurrentThreadId();
__declspec(dllimport) u64 WINAPI GetTickCount64();
}

static constexpr DWORD GENERIC_READ_ = 0x80000000u;
static constexpr DWORD GENERIC_WRITE_ = 0x40000000u;
static constexpr DWORD FILE_SHARE_READ_ = 0x00000001u;
static constexpr DWORD CREATE_ALWAYS_ = 2u;
static constexpr DWORD OPEN_EXISTING_ = 3u;
static constexpr DWORD FILE_ATTRIBUTE_NORMAL_ = 0x00000080u;
static constexpr DWORD FILE_BEGIN_ = 0u;
static constexpr DWORD PAGE_READWRITE_ = 0x04u;
static constexpr DWORD PAGE_EXECUTE_READ_ = 0x20u;
static constexpr DWORD PAGE_EXECUTE_READWRITE_ = 0x40u;
static constexpr DWORD MEM_COMMIT_ = 0x00001000u;
static constexpr DWORD MEM_RESERVE_ = 0x00002000u;
static constexpr DWORD MEM_RELEASE_ = 0x00008000u;

static HANDLE invalid_handle_value()
{
    return reinterpret_cast<HANDLE>(~static_cast<uptr>(0));
}

// -----------------------------------------------------------------------------
// SKSE ABI
// -----------------------------------------------------------------------------

using PluginHandle = u32;

struct PluginInfo {
    u32 infoVersion;
    const char* name;
    u32 version;
};

struct SKSEInterface {
    u32 skseVersion;
    u32 runtimeVersion;
    u32 editorVersion;
    u32 isEditor;
    void* (*QueryInterface)(u32 id);
    PluginHandle (*GetPluginHandle)();
    u32 (*GetReleaseIndex)();
    const PluginInfo* (*GetPluginInfo)(const char* name);
};

struct SKSEMessage {
    const char* sender;
    u32 type;
    u32 dataLen;
    void* data;
};

using MessageCallback = void (*)(SKSEMessage* message);

class TaskDelegate {
public:
    virtual void Run() = 0;
    virtual void Dispose() = 0;
};

struct SKSETaskInterface {
    u32 interfaceVersion;
    void (*AddTask)(TaskDelegate* task);
    void (*AddUITask)(void* task);
};

struct SKSEMessagingInterface {
    u32 interfaceVersion;
    bool (*RegisterListener)(PluginHandle listener, const char* sender, MessageCallback handler);
    bool (*Dispatch)(PluginHandle sender, u32 messageType, void* data, u32 dataLen, const char* receiver);
    void* (*GetEventDispatcher)(u32 dispatcherId);
};

static constexpr u32 kInterface_Task = 4u;
static constexpr u32 kInterface_Messaging = 5u;
static constexpr u32 kMessage_PostLoad = 0u;
static constexpr u32 kMessage_PreLoadGame = 2u;
static constexpr u32 kMessage_PostLoadGame = 3u;
static constexpr u32 kMessage_SaveGame = 4u;
static constexpr u32 kMessage_NewGame = 7u;
static constexpr u32 kRuntime_1_5_97 = 0x01050610u;
static constexpr PluginHandle kInvalidPluginHandle = 0xFFFFFFFFu;

// -----------------------------------------------------------------------------
// Configuration / diagnostics
// -----------------------------------------------------------------------------

static constexpr const char* kPluginName = "GlobalPointLightISL";
static constexpr const char* kLogPath = "Data/SKSE/Plugins/GlobalPointLightISL.log";
static constexpr const char* kIniPath = "Data/SKSE/Plugins/GlobalPointLightISL.ini";
static constexpr const char* kCommunityShadersModule = "CommunityShaders.dll";

static HANDLE g_log = nullptr;
static bool g_loggingEnabled = false;
static bool g_enabled = true;
static float g_maxRadius = 0.0f;
// For ordinary lights that are actually clamped by MaxRadius, this is the
// fraction of the capped radius kept before the explicit smooth edge fade
// begins. 0.10 means fade starts at 10% of the capped radius and spans the
// remaining 90%. ISL inverse-square attenuation still starts immediately.
static float g_fadeStartFraction = 0.10f;
static bool g_environmentWhitelistEnabled = true;
static bool g_forceShadowPromotion = true;

// Local point/spot shadow-depth-bias correction. This operates on Skyrim's
// transient ShadowSceneNode::LIGHT_CREATE_PARAMS at the existing Open Shaders
// AddLight interception, so plugin records/saves are never modified and no
// scanner or per-frame rewrite is required. Values below the maximum are left
// exactly unchanged. Directional sun/moon shadow state does not use this path.
static bool g_shadowBiasCorrectionEnabled = true;
static float g_shadowBiasMaximum = 1.0f;
static volatile const u8* g_openShadersPromoteNormalSetting = nullptr;

// Continuous/spray spell shadow optimization. This is deliberately event-driven:
// only dynamic NiPointLights already passing through CreatePointLight are observed.
// A rapidly repeated stream from the same runtime source is classified as a
// continuous emitter; ordinary one-shot projectiles remain untouched. Once a
// stream is classified, normal->shadow promotion is rate-limited before the
// expensive BSShadowLight path is created. Diffuse/ISL lighting is unchanged.
static bool g_continuousShadowLimiterEnabled = true;
static u32 g_continuousBurstWindowMs = 250u;
static u32 g_continuousBurstThreshold = 4u;
static u32 g_continuousShadowIntervalMs = 125u;
static u32 g_continuousHoldMs = 1200u;

static bool g_magicHandLightsEnabled = true;
static float g_magicHandMaxRadius = 40.0f;
static bool g_magicHandCastShadows = false;
static bool g_magicImpactLightsEnabled = true;
static float g_magicImpactMaxRadius = 20.0f;

// Exact-owner no-light policy. Deathbell is the first entry because the flower
// is non-bioluminescent and can exist in large numbers. New lights are vetoed
// before creation; an already-live matching NiLight encountered by the existing
// ISL ProcessLight path is zeroed/dropped and marked hidden + Disabled. No world
// scan, periodic scanner, or proximity search is used.
static bool g_disableDeathbellAttachedLights = true;

// Selective fire-color consistency. Classification is by the defining
// TESObjectLIGH EditorID (or an explicit Plugin|LocalFormID override). In
// authoritative mode chromaticity is reasserted on transient LightData after
// ProcessLight, after Effects11 when that exact engine-light call is resolved,
// and again at the clustered vector push. TESObjectLIGH/NiLight state is never
// mutated and unrelated lights remain untouched.
static bool g_fireColorEnabled = true;
static bool g_fireColorPreserveIntensity = true;
static bool g_fireColorAuthoritative = true;
// EditorID-based fire matches require warm authored LIGH RGB by default.
// Explicit FireColor.Forms entries remain authoritative overrides.
static bool g_fireColorRequireWarm = true;
static float g_fireColorRed = 1.0f;                    // 255 / 255
static float g_fireColorGreen = 140.0f / 255.0f;
static float g_fireColorBlue = 60.0f / 255.0f;
// Open Shaders appends generated particle lights after engine-backed lights in
// the same per-frame LightData vector. A classified fire engine light can
// therefore act as a transient spatial anchor for its paired source particle
// light without retaining any scene pointer or scanning the scene graph.
static bool g_fireColorPairParticleLights = true;
static bool g_fireColorParticleRequireWarm = true;
static float g_fireColorParticlePairDistance = 128.0f;

// Conservative engine-light association for lighting mods such as Lux that
// place a visible fire object and a separate TESObjectLIGH reference nearby.
// The plugin-file scan pairs each recognized local fire-source reference with
// only its single nearest eligible warm local light in the same cell, then
// tags that exact placed reference. The defining LIGH record is not globally
// reclassified, so the same bulb form can still be used elsewhere untouched.
static bool g_fireColorPairDetachedBulbs = true;
static bool g_fireColorDetachedRequireWarm = true;
static float g_fireColorDetachedPairDistance = 128.0f;

// Lifecycle safety layer. The renderer's LightData payload is transient and is
// deliberately never retained across calls. Engine-backed NiLight instances are
// admitted only after a main-thread handoff plus a short settling interval. The
// queue stores an opaque identity hash, never a dereferenceable engine pointer.
// Save/load transitions globally suspend all plugin-side renderer mutations.
static volatile long g_lifecycleSuspendActive = 1;
static volatile long g_clearPendingAfterTransition = 0;
static volatile u64 g_resumeNotBeforeTick = 0u;
static volatile u32 g_sceneGeneration = 1u;
static volatile u32 g_transitionKind = 0u; // 0=none, 1=save, 2=load/new-game
static volatile u32 g_pendingSaveReleaseTasks = 0u;
static SKSETaskInterface* g_taskInterface = nullptr;

static void schedule_deferred_task();

static constexpr u64 kNormalSettleMs = 75u;
static constexpr u64 kSaveSettleMs = 125u;
static constexpr u64 kLoadSettleMs = 350u;
static constexpr u64 kRediscoveryGapMs = 5000u;
static constexpr u64 kPendingRetryMs = 500u;
static constexpr u64 kAbandonAfterMs = 2500u;
static constexpr u32 kMaxAdmissionAttempts = 3u;

static u64 current_tick_ms()
{
    return GetTickCount64();
}

static bool processing_suspended()
{
    if (__atomic_load_n(&g_lifecycleSuspendActive, __ATOMIC_ACQUIRE) != 0) return true;

    const u64 notBefore = __atomic_load_n(&g_resumeNotBeforeTick, __ATOMIC_ACQUIRE);
    if (notBefore == 0u) return false;
    if (current_tick_ms() < notBefore) return true;

    u64 expected = notBefore;
    if (__atomic_compare_exchange_n(&g_resumeNotBeforeTick, &expected, 0u,
                                    false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        __atomic_store_n(&g_clearPendingAfterTransition, 1l, __ATOMIC_RELEASE);
        schedule_deferred_task();
    }
    return false;
}

static void begin_transition(bool advanceGeneration)
{
    __atomic_store_n(&g_lifecycleSuspendActive, 1l, __ATOMIC_RELEASE);
    __atomic_store_n(&g_resumeNotBeforeTick, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&g_clearPendingAfterTransition, 1l, __ATOMIC_RELEASE);
    if (advanceGeneration) {
        __atomic_add_fetch(&g_sceneGeneration, 1u, __ATOMIC_ACQ_REL);
    }
}

static void request_resume_after(u64 delayMs)
{
    const u64 notBefore = current_tick_ms() + delayMs;
    __atomic_store_n(&g_resumeNotBeforeTick, notBefore, __ATOMIC_RELEASE);
    __atomic_store_n(&g_lifecycleSuspendActive, 0l, __ATOMIC_RELEASE);
    __atomic_store_n(&g_clearPendingAfterTransition, 1l, __ATOMIC_RELEASE);
}

class SaveGuardReleaseTask final : public TaskDelegate
{
public:
    void Run() override
    {
        // Ignore stale save-release tasks after a load/new-game transition has
        // superseded them. Multiple saves queued before task processing are
        // reference-counted so the first release cannot resume processing while
        // a later save is still pending.
        if (__atomic_load_n(&g_transitionKind, __ATOMIC_ACQUIRE) != 1u) return;

        u32 pending = __atomic_load_n(&g_pendingSaveReleaseTasks, __ATOMIC_ACQUIRE);
        while (pending != 0u) {
            const u32 desired = pending - 1u;
            if (__atomic_compare_exchange_n(&g_pendingSaveReleaseTasks, &pending, desired,
                                            false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                if (desired == 0u && __atomic_load_n(&g_transitionKind, __ATOMIC_ACQUIRE) == 1u) {
                    __atomic_store_n(&g_transitionKind, 0u, __ATOMIC_RELEASE);
                    request_resume_after(kSaveSettleMs);
                }
                return;
            }
        }
    }

    void Dispose() override
    {
        // Static singleton task: SKSE must not delete it.
    }
};

static SaveGuardReleaseTask g_saveGuardReleaseTask{};

// Opaque admission records. The key is a one-way mix of FormID, scene
// generation and the current NiLight/BSLight addresses. Addresses are never
// stored separately and are never dereferenced after the hook that observed
// them. A fresh ProcessLight observation is always required before conversion.
enum AdmissionState : u32
{
    kAdmissionQueued = 1u,
    kAdmissionSettling = 2u,
    kAdmissionReady = 3u,
    kAdmissionAbandoned = 4u
};

struct AdmissionEntry
{
    volatile u64 key;
    volatile u64 firstSeenTick;
    volatile u64 lastSeenTick;
    volatile u64 eligibleTick;
    volatile u32 generation;
    volatile u32 state;
    volatile u32 attempts;
    u32 pad;
};

static constexpr u32 kAdmissionCapacity = 32768u;
static_assert((kAdmissionCapacity & (kAdmissionCapacity - 1u)) == 0u,
              "admission capacity must be a power of two");
static AdmissionEntry g_admission[kAdmissionCapacity]{};

struct DeferredAdmissionRequest
{
    u64 key;
    u32 generation;
    u32 reserved;
};

static constexpr u32 kDeferredQueueCapacity = 512u;
static DeferredAdmissionRequest g_deferredQueue[kDeferredQueueCapacity]{};
static u32 g_deferredHead = 0u;
static u32 g_deferredTail = 0u;
static volatile long g_deferredQueueLock = 0;
static volatile long g_deferredTaskScheduled = 0;

static u64 mix_identity(u64 value)
{
    value ^= value >> 33u;
    value *= 0xFF51AFD7ED558CCDull;
    value ^= value >> 33u;
    value *= 0xC4CEB9FE1A85EC53ull;
    value ^= value >> 33u;
    return value != 0u ? value : 1u;
}

static u64 make_runtime_instance_key(u32 formID, const void* niLight, const void* bsLight, u32 generation)
{
    u64 value = static_cast<u64>(reinterpret_cast<uptr>(niLight));
    value ^= static_cast<u64>(reinterpret_cast<uptr>(bsLight)) * 0x9E3779B185EBCA87ull;
    value ^= static_cast<u64>(formID) << 17u;
    value ^= static_cast<u64>(generation) << 49u;
    return mix_identity(value);
}

static u32 admission_hash(u64 key)
{
    return static_cast<u32>(key ^ (key >> 32u));
}

static AdmissionEntry* find_admission_entry(u64 key, bool create, u32 generation, u64 now)
{
    const u32 start = admission_hash(key) & (kAdmissionCapacity - 1u);
    for (u32 probe = 0u; probe < kAdmissionCapacity; ++probe) {
        AdmissionEntry& entry = g_admission[(start + probe) & (kAdmissionCapacity - 1u)];
        u64 current = __atomic_load_n(&entry.key, __ATOMIC_ACQUIRE);
        if (current == key) return &entry;
        if (current == 0u && create) {
            u64 expected = 0u;
            if (__atomic_compare_exchange_n(&entry.key, &expected, key,
                                            false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                __atomic_store_n(&entry.firstSeenTick, now, __ATOMIC_RELAXED);
                __atomic_store_n(&entry.lastSeenTick, now, __ATOMIC_RELAXED);
                __atomic_store_n(&entry.eligibleTick, 0u, __ATOMIC_RELAXED);
                __atomic_store_n(&entry.generation, generation, __ATOMIC_RELAXED);
                __atomic_store_n(&entry.attempts, 0u, __ATOMIC_RELAXED);
                __atomic_store_n(&entry.state, kAdmissionQueued, __ATOMIC_RELEASE);
                return &entry;
            }
            if (expected == key) return &entry;
        }
        if (current == 0u && !create) return nullptr;
    }
    return nullptr;
}

static bool try_lock_deferred_queue()
{
    for (u32 spin = 0u; spin < 64u; ++spin) {
        long expected = 0l;
        if (__atomic_compare_exchange_n(&g_deferredQueueLock, &expected, 1l,
                                        false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            return true;
        }
    }
    return false;
}

static void unlock_deferred_queue()
{
    __atomic_store_n(&g_deferredQueueLock, 0l, __ATOMIC_RELEASE);
}

static bool deferred_queue_empty()
{
    if (!try_lock_deferred_queue()) return false;
    const bool empty = g_deferredHead == g_deferredTail;
    unlock_deferred_queue();
    return empty;
}

static bool pop_deferred_request(DeferredAdmissionRequest& out)
{
    if (!try_lock_deferred_queue()) return false;
    if (g_deferredHead == g_deferredTail) {
        unlock_deferred_queue();
        return false;
    }
    out = g_deferredQueue[g_deferredHead % kDeferredQueueCapacity];
    ++g_deferredHead;
    unlock_deferred_queue();
    return true;
}

class DeferredAdmissionTask final : public TaskDelegate
{
public:
    void Run() override;
    void Dispose() override {}
};

static DeferredAdmissionTask g_deferredAdmissionTask{};

static void schedule_deferred_task()
{
    if (!g_taskInterface || !g_taskInterface->AddTask) return;
    long expected = 0l;
    if (__atomic_compare_exchange_n(&g_deferredTaskScheduled, &expected, 1l,
                                    false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        g_taskInterface->AddTask(&g_deferredAdmissionTask);
    }
}

static bool enqueue_deferred_admission(u64 key, u32 generation)
{
    if (!g_taskInterface || !g_taskInterface->AddTask) return false;
    if (!try_lock_deferred_queue()) return false;
    const u32 count = g_deferredTail - g_deferredHead;
    if (count >= kDeferredQueueCapacity) {
        unlock_deferred_queue();
        return false;
    }
    DeferredAdmissionRequest& slot = g_deferredQueue[g_deferredTail % kDeferredQueueCapacity];
    slot.key = key;
    slot.generation = generation;
    slot.reserved = 0u;
    ++g_deferredTail;
    unlock_deferred_queue();
    schedule_deferred_task();
    return true;
}


static void reset_admission_state_while_suspended()
{
    // Called only from SKSE lifecycle messages while the renderer hooks are
    // globally suspended. Runtime admission is intentionally session-state only.
    memset(g_admission, 0, sizeof(g_admission));
    if (try_lock_deferred_queue()) {
        g_deferredHead = 0u;
        g_deferredTail = 0u;
        unlock_deferred_queue();
    }
}
void DeferredAdmissionTask::Run()
{
    // Deferred admission never advances while a save/load transition or its
    // settle window is active. The queue remains intact and is scheduled again
    // when processing_suspended() observes the resume boundary.
    if (__atomic_load_n(&g_lifecycleSuspendActive, __ATOMIC_ACQUIRE) != 0 ||
        __atomic_load_n(&g_resumeNotBeforeTick, __ATOMIC_ACQUIRE) != 0u) {
        __atomic_store_n(&g_deferredTaskScheduled, 0l, __ATOMIC_RELEASE);
        return;
    }

    const u64 now = current_tick_ms();
    const u32 currentGeneration = __atomic_load_n(&g_sceneGeneration, __ATOMIC_ACQUIRE);
    DeferredAdmissionRequest request{};
    while (pop_deferred_request(request)) {
        if (request.generation != currentGeneration) continue;
        AdmissionEntry* entry = find_admission_entry(request.key, false, request.generation, now);
        if (!entry) continue;
        if (__atomic_load_n(&entry->generation, __ATOMIC_ACQUIRE) != request.generation) continue;
        if (__atomic_load_n(&entry->state, __ATOMIC_ACQUIRE) != kAdmissionQueued) continue;
        __atomic_store_n(&entry->eligibleTick, now + kNormalSettleMs, __ATOMIC_RELAXED);
        __atomic_store_n(&entry->state, kAdmissionSettling, __ATOMIC_RELEASE);
    }

    __atomic_store_n(&g_deferredTaskScheduled, 0l, __ATOMIC_RELEASE);
    if (!deferred_queue_empty()) schedule_deferred_task();
}

static bool runtime_instance_admitted(u32 formID, const void* niLight, const void* bsLight)
{
    if (formID == 0u || !niLight || !bsLight) return false;
    const u32 generation = __atomic_load_n(&g_sceneGeneration, __ATOMIC_ACQUIRE);
    const u64 now = current_tick_ms();
    const u64 key = make_runtime_instance_key(formID, niLight, bsLight, generation);
    AdmissionEntry* entry = find_admission_entry(key, true, generation, now);
    if (!entry) return false;

    const u64 previousSeen = __atomic_exchange_n(&entry->lastSeenTick, now, __ATOMIC_ACQ_REL);
    u32 state = __atomic_load_n(&entry->state, __ATOMIC_ACQUIRE);

    // A light that disappeared for several seconds must re-prove readiness.
    // This protects cell unload/reload and allocator reuse without retaining or
    // dereferencing any stale scene pointer.
    if (previousSeen != 0u && now > previousSeen && now - previousSeen > kRediscoveryGapMs) {
        __atomic_store_n(&entry->firstSeenTick, now, __ATOMIC_RELAXED);
        __atomic_store_n(&entry->eligibleTick, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&entry->attempts, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&entry->state, kAdmissionQueued, __ATOMIC_RELEASE);
        enqueue_deferred_admission(key, generation);
        return false;
    }

    if (state == kAdmissionReady) return true;
    if (state == kAdmissionSettling) {
        const u64 eligible = __atomic_load_n(&entry->eligibleTick, __ATOMIC_ACQUIRE);
        if (eligible != 0u && now >= eligible) {
            u32 expected = kAdmissionSettling;
            __atomic_compare_exchange_n(&entry->state, &expected, kAdmissionReady,
                                        false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
            return __atomic_load_n(&entry->state, __ATOMIC_ACQUIRE) == kAdmissionReady;
        }
        return false;
    }
    if (state == kAdmissionAbandoned) return false;

    const u64 firstSeen = __atomic_load_n(&entry->firstSeenTick, __ATOMIC_ACQUIRE);
    u32 attempts = __atomic_load_n(&entry->attempts, __ATOMIC_ACQUIRE);
    if (attempts == 0u || (now > firstSeen && now - firstSeen >= kPendingRetryMs)) {
        if (attempts < kMaxAdmissionAttempts) {
            __atomic_add_fetch(&entry->attempts, 1u, __ATOMIC_ACQ_REL);
            enqueue_deferred_admission(key, generation);
        } else if (now > firstSeen && now - firstSeen >= kAbandonAfterMs) {
            __atomic_store_n(&entry->state, kAdmissionAbandoned, __ATOMIC_RELEASE);
        }
    }
    return false;
}

struct KeywordEntry
{
    char text[64];
};

struct ExactFormEntry
{
    char plugin[260];
    u32 localFormID = 0u;
};

static constexpr u32 kMaxWhitelistKeywords = 32u;
static constexpr u32 kMaxWhitelistForms = 64u;
static constexpr u32 kMaxForceCapKeywords = 32u;
static constexpr u32 kMaxMagicRuleKeywords = 8u;
static constexpr u32 kMaxFireColorKeywords = 32u;
static constexpr u32 kMaxFireColorExcludeKeywords = 32u;
static constexpr u32 kMaxFireColorForms = 64u;
static KeywordEntry g_whitelistKeywords[kMaxWhitelistKeywords]{};
static ExactFormEntry g_whitelistForms[kMaxWhitelistForms]{};
static KeywordEntry g_forceCapKeywords[kMaxForceCapKeywords]{};
static KeywordEntry g_magicHandKeywords[kMaxMagicRuleKeywords]{};
static KeywordEntry g_magicHandAnyKeywords[kMaxMagicRuleKeywords]{};
static ExactFormEntry g_magicHandForms[kMaxWhitelistForms]{};
static KeywordEntry g_magicImpactKeywords[kMaxMagicRuleKeywords]{};
static KeywordEntry g_magicImpactAnyKeywords[kMaxMagicRuleKeywords]{};
static ExactFormEntry g_magicImpactForms[kMaxWhitelistForms]{};
static KeywordEntry g_fireColorKeywords[kMaxFireColorKeywords]{};
static KeywordEntry g_fireColorExcludeKeywords[kMaxFireColorExcludeKeywords]{};
static ExactFormEntry g_fireColorForms[kMaxFireColorForms]{};
static u32 g_whitelistKeywordCount = 0u;
static u32 g_whitelistFormCount = 0u;
static u32 g_forceCapKeywordCount = 0u;
static u32 g_magicHandKeywordCount = 0u;
static u32 g_magicHandAnyKeywordCount = 0u;
static u32 g_magicHandFormCount = 0u;
static u32 g_magicImpactKeywordCount = 0u;
static u32 g_magicImpactAnyKeywordCount = 0u;
static u32 g_magicImpactFormCount = 0u;
static u32 g_fireColorKeywordCount = 0u;
static u32 g_fireColorExcludeKeywordCount = 0u;
static u32 g_fireColorFormCount = 0u;

static u32 string_length(const char* s)
{
    u32 n = 0u;
    if (s) {
        while (s[n] != '\0') {
            ++n;
        }
    }
    return n;
}

static void ensure_log(bool force)
{
    if ((!g_loggingEnabled && !force) || g_log != nullptr) {
        return;
    }
    g_log = CreateFileA(kLogPath, GENERIC_WRITE_, FILE_SHARE_READ_, nullptr,
                        CREATE_ALWAYS_, FILE_ATTRIBUTE_NORMAL_, nullptr);
}

static void write_raw_log_line(const char* text)
{
    if (!text || g_log == nullptr || g_log == invalid_handle_value()) {
        return;
    }
    DWORD written = 0u;
    const u32 len = string_length(text);
    if (len != 0u) {
        WriteFile(g_log, text, len, &written, nullptr);
    }
    static constexpr char newline[2] = {'\r', '\n'};
    WriteFile(g_log, newline, 2u, &written, nullptr);
}

static void write_log_line(const char* text, bool force)
{
    if ((!g_loggingEnabled && !force) || !text) {
        return;
    }
    const bool creatingFailureOnlyLog = force && !g_loggingEnabled && g_log == nullptr;
    ensure_log(force);
    if (g_log == nullptr || g_log == invalid_handle_value()) {
        return;
    }
    if (creatingFailureOnlyLog) {
        write_raw_log_line("=== Global Point Light ISL 1.5-test22 ===");
        write_raw_log_line("Normal logging is disabled; this log was created because startup failed.");
    }
    write_raw_log_line(text);
}

static void log_line(const char* text)
{
    write_log_line(text, false);
}

static void log_error(const char* text)
{
    write_log_line(text, true);
}

static char hex_digit(u32 value)
{
    return value < 10u ? static_cast<char>('0' + value)
                       : static_cast<char>('A' + (value - 10u));
}

static void write_hex32(const char* prefix, u32 value, bool force)
{
    if ((!g_loggingEnabled && !force)) {
        return;
    }
    char buffer[96] = {};
    u32 pos = 0u;
    if (prefix) {
        while (prefix[pos] != '\0' && pos < 80u) {
            buffer[pos] = prefix[pos];
            ++pos;
        }
    }
    if (pos + 10u >= sizeof(buffer)) {
        return;
    }
    buffer[pos++] = '0';
    buffer[pos++] = 'x';
    for (int shift = 28; shift >= 0; shift -= 4) {
        buffer[pos++] = hex_digit((value >> shift) & 0xFu);
    }
    buffer[pos] = '\0';
    write_log_line(buffer, force);
}

static void log_hex32(const char* prefix, u32 value)
{
    write_hex32(prefix, value, false);
}

static void log_error_hex32(const char* prefix, u32 value)
{
    write_hex32(prefix, value, true);
}

static bool parse_float(const char* text, float& out)
{
    if (!text) {
        return false;
    }
    u32 i = 0u;
    while (text[i] == ' ' || text[i] == '\t') {
        ++i;
    }
    bool negative = false;
    if (text[i] == '-') {
        negative = true;
        ++i;
    } else if (text[i] == '+') {
        ++i;
    }

    bool anyDigit = false;
    float value = 0.0f;
    while (text[i] >= '0' && text[i] <= '9') {
        anyDigit = true;
        value = value * 10.0f + static_cast<float>(text[i] - '0');
        ++i;
    }
    if (text[i] == '.') {
        ++i;
        float place = 0.1f;
        while (text[i] >= '0' && text[i] <= '9') {
            anyDigit = true;
            value += static_cast<float>(text[i] - '0') * place;
            place *= 0.1f;
            ++i;
        }
    }
    while (text[i] == ' ' || text[i] == '\t') {
        ++i;
    }
    if (!anyDigit || text[i] != '\0') {
        return false;
    }
    out = negative ? -value : value;
    return true;
}

static float clamp_float(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void load_debug_setting()
{
    char value[16] = {};
    GetPrivateProfileStringA("Debug", "EnableLogging", "0", value, sizeof(value), kIniPath);
    g_loggingEnabled = value[0] == '1';
}

static void load_settings()
{
    load_debug_setting();

    char value[64] = {};
    GetPrivateProfileStringA("General", "Enabled", "1", value, sizeof(value), kIniPath);
    g_enabled = value[0] != '0';

    // New generic suppression section, with test16's legacy flora section as
    // the default so existing INIs keep the same behavior.
    char legacySuppression[16] = {};
    GetPrivateProfileStringA("FloraLightSuppression", "Deathbell", "1",
                             legacySuppression, sizeof(legacySuppression), kIniPath);
    GetPrivateProfileStringA("LightSuppression", "Deathbell", legacySuppression,
                             value, sizeof(value), kIniPath);
    g_disableDeathbellAttachedLights = value[0] != '0';

    float parsed = 0.0f;
    GetPrivateProfileStringA("InverseSquare", "MaxRadius", "0", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) {
        // 0 disables the ordinary global radius cap. Dedicated hand/impact caps are separate.
        g_maxRadius = parsed <= 0.0f ? 0.0f : clamp_float(parsed, 1.0f, 100000.0f);
    }

    GetPrivateProfileStringA("InverseSquare", "FadeStartFraction", "0.10", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) {
        // Applies only to ordinary lights whose authored radius exceeds MaxRadius.
        // 0.10 = explicit smooth fade begins at 10% of the capped radius.
        g_fadeStartFraction = clamp_float(parsed, 0.0f, 0.95f);
    }

    GetPrivateProfileStringA("EnvironmentalWhitelist", "Enabled", "1", value, sizeof(value), kIniPath);
    g_environmentWhitelistEnabled = value[0] != '0';

    GetPrivateProfileStringA("Shadows", "ForcePromoteNormalLights", "1", value, sizeof(value), kIniPath);
    g_forceShadowPromotion = value[0] != '0';

    GetPrivateProfileStringA("ShadowBiasCorrection", "Enabled", "1", value, sizeof(value), kIniPath);
    g_shadowBiasCorrectionEnabled = value[0] != '0';

    GetPrivateProfileStringA("ShadowBiasCorrection", "MaximumBias", "1.0", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) {
        // Negative depth-bias caps are not meaningful for this safety feature.
        // Zero is allowed for deliberate testing; malformed values keep default.
        g_shadowBiasMaximum = clamp_float(parsed, 0.0f, 100000.0f);
    }

    GetPrivateProfileStringA("ContinuousSpellShadows", "Enabled", "1", value, sizeof(value), kIniPath);
    g_continuousShadowLimiterEnabled = value[0] != '0';

    GetPrivateProfileStringA("ContinuousSpellShadows", "BurstWindowMs", "250", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) {
        g_continuousBurstWindowMs = static_cast<u32>(clamp_float(parsed, 50.0f, 2000.0f));
    }
    GetPrivateProfileStringA("ContinuousSpellShadows", "BurstThreshold", "4", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) {
        g_continuousBurstThreshold = static_cast<u32>(clamp_float(parsed, 2.0f, 16.0f));
    }
    GetPrivateProfileStringA("ContinuousSpellShadows", "ShadowIntervalMs", "125", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) {
        g_continuousShadowIntervalMs = static_cast<u32>(clamp_float(parsed, 16.0f, 1000.0f));
    }
    GetPrivateProfileStringA("ContinuousSpellShadows", "HoldMs", "1200", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) {
        g_continuousHoldMs = static_cast<u32>(clamp_float(parsed, 250.0f, 5000.0f));
    }

    GetPrivateProfileStringA("MagicHandLights", "Enabled", "1", value, sizeof(value), kIniPath);
    g_magicHandLightsEnabled = value[0] != '0';

    GetPrivateProfileStringA("MagicHandLights", "MaxRadius", "40.0", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) {
        g_magicHandMaxRadius = parsed <= 0.0f ? 0.0f : clamp_float(parsed, 1.0f, 100000.0f);
    }

    GetPrivateProfileStringA("MagicHandLights", "CastShadows", "0", value, sizeof(value), kIniPath);
    g_magicHandCastShadows = value[0] != '0';

    GetPrivateProfileStringA("MagicImpactLights", "Enabled", "1", value, sizeof(value), kIniPath);
    g_magicImpactLightsEnabled = value[0] != '0';

    GetPrivateProfileStringA("MagicImpactLights", "MaxRadius", "20.0", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) {
        g_magicImpactMaxRadius = parsed <= 0.0f ? 0.0f : clamp_float(parsed, 1.0f, 100000.0f);
    }

    GetPrivateProfileStringA("FireColor", "Enabled", "1", value, sizeof(value), kIniPath);
    g_fireColorEnabled = value[0] != '0';

    GetPrivateProfileStringA("FireColor", "PreserveIntensity", "1", value, sizeof(value), kIniPath);
    g_fireColorPreserveIntensity = value[0] != '0';

    GetPrivateProfileStringA("FireColor", "Authoritative", "1", value, sizeof(value), kIniPath);
    g_fireColorAuthoritative = value[0] != '0';

    GetPrivateProfileStringA("FireColor", "RequireWarm", "1", value, sizeof(value), kIniPath);
    g_fireColorRequireWarm = value[0] != '0';

    GetPrivateProfileStringA("FireColor", "Red", "255", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) g_fireColorRed = clamp_float(parsed, 0.0f, 255.0f) / 255.0f;
    GetPrivateProfileStringA("FireColor", "Green", "140", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) g_fireColorGreen = clamp_float(parsed, 0.0f, 255.0f) / 255.0f;
    GetPrivateProfileStringA("FireColor", "Blue", "60", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) g_fireColorBlue = clamp_float(parsed, 0.0f, 255.0f) / 255.0f;

    GetPrivateProfileStringA("FireColor", "PairParticleLights", "1", value, sizeof(value), kIniPath);
    g_fireColorPairParticleLights = value[0] != '0';

    GetPrivateProfileStringA("FireColor", "ParticleRequireWarm", "1", value, sizeof(value), kIniPath);
    g_fireColorParticleRequireWarm = value[0] != '0';

    GetPrivateProfileStringA("FireColor", "ParticlePairDistance", "128.0", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) {
        g_fireColorParticlePairDistance = parsed <= 0.0f ? 0.0f : clamp_float(parsed, 1.0f, 2048.0f);
    }

    GetPrivateProfileStringA("FireColor", "PairDetachedEngineBulbs", "1", value, sizeof(value), kIniPath);
    g_fireColorPairDetachedBulbs = value[0] != '0';

    GetPrivateProfileStringA("FireColor", "DetachedRequireWarm", "1", value, sizeof(value), kIniPath);
    g_fireColorDetachedRequireWarm = value[0] != '0';

    GetPrivateProfileStringA("FireColor", "DetachedPairDistance", "128.0", value, sizeof(value), kIniPath);
    if (parse_float(value, parsed)) {
        g_fireColorDetachedPairDistance = parsed <= 0.0f ? 0.0f : clamp_float(parsed, 1.0f, 512.0f);
    }
}

// -----------------------------------------------------------------------------
// Renderer LightData ABI / startup semantic resolution
// -----------------------------------------------------------------------------

// Community Shaders 1.8 uses an 80-byte LightData. Open Shaders 2.12 through the 2.17 family use a
// 96-byte LightData with the same attenuation fields at 0x0C..0x1C but moves
// lightFlags from 0x40 to 0x50. We never assume either layout before the
// vector stride proves it. Unknown layouts pass through untouched.
static constexpr u32 kLightFlagShadow = 1u << 1;
static constexpr u32 kLightFlagSimple = 1u << 2;
static constexpr u32 kLightFlagDisabled = 1u << 9;
static constexpr u32 kLightFlagInverseSquare = 1u << 10;

// CommonLibSSE SE 1.5.97 NiAVObject layout. Open Shaders' IsValidLight()
// rejects kHidden NiLights before they enter its clustered/strict builders.
// Use this only for exact no-light owners already encountered in ProcessLight;
// there is no scene scan or periodic traversal.
static constexpr u32 kNiAVObjectFlagsOffsetSE1597 = 0xF4u;
static constexpr u32 kNiAVObjectHiddenFlag = 1u << 0;

// Skyrim SE 1.5.97 + Open Shaders 2.12-2.17-family provenance layout.
// RuntimeLightDataExt overlays NiLight::LIGHT_RUNTIME_DATA at +0x110.
// Exact Open Shaders source layout:
//   +00 flags, +04 cutoffOverride, +08 lighFormId, +0C diffuse,
//   +18 radius, +20 size, +24 fade.
// NiAVObject::world.translate is read by the exact supplied OS binary at +0xA0.
static constexpr u32 kNiLightRuntimeDataOffsetSE1597 = 0x110u;
static constexpr u32 kRuntimeFormIdOffset = 0x08u;

// ISL constants shared by the renderer implementations targeted here.
static constexpr float kFadeZoneBase = 252.0f;       // 4.5 * 0.8 * 70
static constexpr float kInvFadeZoneBase = 1.0f / kFadeZoneBase;
static constexpr float kCutoff = 0.05f;
static constexpr float kScaledUnitsSq = 3920.0f;      // 0.8 * 70^2
static constexpr float kSizeBiasFactor = kCutoff / (1.0f - kCutoff);
static constexpr float kFadeRadiusSqFactor = kSizeBiasFactor / kScaledUnitsSq;

static constexpr u32 kOffsetColorR = 0x00u;
static constexpr u32 kOffsetColorG = 0x04u;
static constexpr u32 kOffsetColorB = 0x08u;
static constexpr u32 kOffsetFade = 0x0Cu;
static constexpr u32 kOffsetRadius = 0x10u;
static constexpr u32 kOffsetInvRadius = 0x14u;
static constexpr u32 kOffsetFadeZone = 0x18u;
static constexpr u32 kOffsetSizeBias = 0x1Cu;
// positionWS remains immediately after the attenuation block in the supported
// LightData layouts. Particle pairing uses only this transient current-batch
// position and never stores an engine object pointer.
static constexpr u32 kOffsetPositionX = 0x20u;
static constexpr u32 kOffsetPositionY = 0x24u;
static constexpr u32 kOffsetPositionZ = 0x28u;

enum class LightDataABI : u32 {
    Unknown = 0u,
    CommunityShaders80 = 80u,
    OpenShaders96 = 96u
};

struct LightDataVectorRaw {
    u8* begin;
    u8* end;
    u8* capacity;
};

using PushLightFn = void (*)(LightDataVectorRaw* lights, const void* light);
using ProcessLightFn = void (*)(void* isl, void* light, void* bsLight, void* niLight);
using CreatePointLightFn = void* (*)(void* ligh, void* refr, void* root,
                                     bool forceDynamic, bool useLightRadius,
                                     bool affectRequesterOnly);
using Effects11OverridePointLightColorFn = void (*)(void* effects11, void* color);

static LightDataABI g_lightDataABI = LightDataABI::Unknown;

static u32 light_data_size(LightDataABI abi)
{
    return static_cast<u32>(abi);
}

static u32 light_flags_offset(LightDataABI abi)
{
    return abi == LightDataABI::CommunityShaders80 ? 0x40u :
           abi == LightDataABI::OpenShaders96 ? 0x50u : 0u;
}

// Current-family vector<LightData>::push_back prologue. The final short-branch
// displacement is wildcarded because it can move when the linker/compiler
// changes surrounding code placement.
static constexpr u8 kPushTargetPattern[] = {
    0x40,0x53,0x55,0x57,0x48,0x83,0xEC,0x20,
    0x48,0x8B,0x59,0x08,0x48,0x8B,0xEA,0x48,
    0x8B,0xF9,0x48,0x3B,0x59,0x10,0x73,0x00
};
static constexpr char kPushTargetMask[] = "xxxxxxxxxxxxxxxxxxxxxxx?";
static constexpr u32 kPushOverwriteSize = 8u; // complete instructions only

// Current-family InverseSquareLighting::ProcessLight prologue. The stack-frame
// immediate is wildcarded. The first seven bytes are complete, RIP-independent
// instructions and are safe to replay in the trampoline.
static constexpr u8 kProcessLightPattern[] = {
    0x48,0x8B,0xC4,0x48,0x89,0x48,0x08,0x55,
    0x56,0x57,0x41,0x56,0x48,0x81,0xEC,
    0x00,0x00,0x00,0x00,0x44,0x0F,0x29,0x48,0x88
};
static constexpr char kProcessLightMask[] = "xxxxxxxxxxxxxxx????xxxxx";
static constexpr u32 kProcessOverwriteSize = 7u; // complete instructions only

static_assert(sizeof(kPushTargetMask) - 1u == sizeof(kPushTargetPattern), "push signature mask mismatch");
static_assert(sizeof(kProcessLightMask) - 1u == sizeof(kProcessLightPattern), "ProcessLight signature mask mismatch");

// Open Shaders 2.12-2.17-family InverseSquareLighting::CreatePointLight thunk. This is the
// point where the winning runtime TESObjectLIGH* is still available. The thunk
// is already the target of Open Shaders' Skyrim CreatePointLight detour. Hooking
// its entry lets us resolve sourceFiles[0] and lazily scan the defining plugin's
// LIGH EditorIDs without guessing load order or hardcoding plugin/FormID lists.
static constexpr u8 kCreatePointLightPattern[] = {
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
    0x57,0x48,0x83,0xEC,0x40,0x0F,0xB6,0x44,0x24,0x78,
    0x49,0x8B,0xF0,0x4C,0x8B,0x15,0x00,0x00,0x00,0x00,
    0x48,0x8B,0xF9,0x88,0x44,0x24,0x28,0x0F,0xB6,0x44,
    0x24,0x70,0x88,0x44,0x24,0x20,0x41,0xFF,0xD2,0x48,
    0x8B,0xD8
};
static constexpr char kCreatePointLightMask[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxx";
static constexpr u32 kCreatePointLightOverwriteSize = 10u; // two complete MOVs
static_assert(sizeof(kCreatePointLightMask) - 1u == sizeof(kCreatePointLightPattern),
              "CreatePointLight signature mask mismatch");

// Open Shaders 2.12-2.17-family ShadowCasterManager::Hook_ConvertLights_Add promotion gate.
// The first conditional jump skips native normal->shadow promotion when
// s_settings.PromoteNormalToShadow is false. With the continuous-light limiter
// enabled, test19 replaces ONLY that branch with a fail-open relay: ordinary
// lights continue into Open Shaders' native promotion code while classified
// rapid dynamic streams can take Open Shaders' own non-promoted branch. The
// existing already-shadow test, creation parameters, tracking, descriptor
// repair, scheduling, atlas allocation, and teardown remain Open Shaders-owned.
static constexpr u8 kShadowPromotionGatePattern[] = {
    0x80,0x3D,0x00,0x00,0x00,0x00,0x00,
    0x0F,0x84,0x00,0x00,0x00,0x00,
    0x80,0x7F,0x01,0x00,
    0x0F,0x85,0x00,0x00,0x00,0x00,
    0x66,0xC7,0x07,0x01,0x01,
    0xC7,0x47,0x08,0xDB,0x0F,0xC9,0x40
};
static constexpr char kShadowPromotionGateMask[] = "xx????xxx????xxxxxx????xxxxxxxxxxxx";
static_assert(sizeof(kShadowPromotionGateMask) - 1u == sizeof(kShadowPromotionGatePattern),
              "shadow promotion signature mask mismatch");

static u16 read_u16(const u8* p)
{
    return static_cast<u16>(static_cast<u16>(p[0]) |
                            (static_cast<u16>(p[1]) << 8));
}

static u32 read_u32(const u8* p)
{
    return static_cast<u32>(p[0]) |
           (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

static u64 read_u64(const u8* p)
{
    u64 value = 0u;
    for (u32 i = 0u; i < 8u; ++i) {
        value |= static_cast<u64>(p[i]) << (i * 8u);
    }
    return value;
}

static void write_u32(u8* p, u32 value)
{
    for (u32 i = 0u; i < 4u; ++i) {
        p[i] = static_cast<u8>((value >> (i * 8u)) & 0xFFu);
    }
}

static void write_u64(u8* p, u64 value)
{
    for (u32 i = 0u; i < 8u; ++i) {
        p[i] = static_cast<u8>((value >> (i * 8u)) & 0xFFu);
    }
}

union FloatBits {
    float f;
    u32 u;
};

static float read_f32(const u8* p)
{
    FloatBits value{};
    value.u = read_u32(p);
    return value.f;
}

static void write_f32(u8* p, float value)
{
    FloatBits bits{};
    bits.f = value;
    write_u32(p, bits.u);
}

static bool finite_f32(float value)
{
    FloatBits bits{};
    bits.f = value;
    return (bits.u & 0x7F800000u) != 0x7F800000u;
}

// -----------------------------------------------------------------------------
// Global runtime EditorID whitelist / Open Shaders provenance bridge
// -----------------------------------------------------------------------------

// TESObjectLIGH does not retain its EDID as a member and its base
// TESForm::GetFormEditorID virtual returns an empty string. Instead, use the
// live form's sourceFiles[0] pointer (the defining/owner plugin), read that
// plugin's own LIGH records on first encounter, and cache classifications by
// the final runtime FormID. This survives later Lux/MLO/patch overrides because
// sourceFiles[0] and the owning FormID stay attached to the original record.
static constexpr u32 kTESFormSourceFilesOffset = 0x08u;
static constexpr u32 kTESFormFormIDOffset = 0x14u;
// CommonLibSSE SE 1.5.97 TESObjectREFR layout: OBJ_REFR data starts at +0x40
// and its first member is TESBoundObject* objectReference.
static constexpr u32 kTESObjectREFRBaseObjectOffsetSE1597 = 0x40u;
// CommonLibSSE SE NiAVObject layout: parent is NiNode* at +0x30 and
// userData is TESObjectREFR* at +0xF8. CommonLib GetUserData() walks the
// parent chain when a child node has no direct userData; mirror that here so
// runtime-attached child lights inherit the owning placed reference.
static constexpr u32 kNiAVObjectParentOffsetSE1597 = 0x30u;
static constexpr u32 kNiAVObjectUserDataOffsetSE1597 = 0xF8u;
static constexpr u32 kDeathbellBaseForm01 = 0x000BB94Bu; // TreeFloraDeathBell01
static constexpr u32 kDeathbellBaseForm02 = 0x000BB94Cu; // TreeFloraDeathBell02
static constexpr u32 kDeathbellBaseFormBYOH = 0x03010462u; // BYOHHouseIngrdDeathbell01

static bool is_no_light_base_form(u32 baseFormID)
{
    // Extensible exact-owner table. Deathbell is the first offender; future
    // objects can be added here without introducing a scanner or broad keyword
    // rule. Keep object-specific toggles so each family can be disabled safely.
    if (g_disableDeathbellAttachedLights) {
        if (baseFormID == kDeathbellBaseForm01 ||
            baseFormID == kDeathbellBaseForm02 ||
            baseFormID == kDeathbellBaseFormBYOH) {
            return true;
        }
    }
    return false;
}

static bool is_no_light_owner_ref(const void* refr)
{
    if (!refr) return false;

    const auto* refBytes = static_cast<const u8*>(refr);
    const uptr baseObjectAddress = static_cast<uptr>(
        read_u64(refBytes + kTESObjectREFRBaseObjectOffsetSE1597));
    // Runtime form pointers live far above the low numeric FormID range.
    // Fail closed rather than dereferencing a null/obviously-invalid value.
    if (baseObjectAddress < 0x100000000ull) return false;

    const auto* baseObject = reinterpret_cast<const u8*>(baseObjectAddress);
    return is_no_light_base_form(read_u32(baseObject + kTESFormFormIDOffset));
}

static const void* owner_ref_from_ni_avobject(const void* object)
{
    // Match CommonLibSSE NiAVObject::GetUserData(): child scene objects often
    // leave userData null and rely on an ancestor/root node to carry the owning
    // TESObjectREFR. Runtime-attached Light Placer/mesh child lights are one
    // such case. Keep the walk bounded so corrupt/cyclic scene data fails closed
    // instead of becoming an unbounded renderer-thread traversal.
    const auto* current = static_cast<const u8*>(object);
    for (u32 depth = 0u; current && depth < 32u; ++depth) {
        const uptr ownerAddress = static_cast<uptr>(
            read_u64(current + kNiAVObjectUserDataOffsetSE1597));
        if (ownerAddress >= 0x100000000ull) {
            return reinterpret_cast<const void*>(ownerAddress);
        }

        const uptr parentAddress = static_cast<uptr>(
            read_u64(current + kNiAVObjectParentOffsetSE1597));
        if (parentAddress < 0x100000000ull ||
            parentAddress == reinterpret_cast<uptr>(current)) {
            break;
        }
        current = reinterpret_cast<const u8*>(parentAddress);
    }
    return nullptr;
}

static bool is_no_light_point_light_owner(const void* refr, const void* root)
{
    if (is_no_light_owner_ref(refr)) return true;
    return is_no_light_owner_ref(owner_ref_from_ni_avobject(root));
}

static bool is_no_light_ni_light_owner(const void* niLight)
{
    return is_no_light_owner_ref(owner_ref_from_ni_avobject(niLight));
}

// -----------------------------------------------------------------------------
// Continuous/spray dynamic-light shadow classifier
// -----------------------------------------------------------------------------
// The expensive case we want to distinguish is not "all magic" or "all dynamic
// lights". Concentration sprays and breath attacks repeatedly create the same
// dynamic light definition in a tight burst; ordinary fire-and-forget projectiles
// do not. Track only lights already passing through CreatePointLight and classify
// that runtime behavior. No cells, actors, effects, projectiles, or scene objects
// are scanned.
struct ContinuousLightInstance
{
    uptr niLight = 0u;
    u64 streamKey = 0u;
    u64 createdTick = 0u;
};

struct ContinuousLightStream
{
    u64 key = 0u;
    u64 windowStart = 0u;
    u64 lastSeen = 0u;
    u64 sprayUntil = 0u;
    u64 lastShadowGrant = 0u;
    u32 burstCount = 0u;
};

// CreatePointLight can call ShadowSceneNode::AddLight before returning the newly
// created NiPointLight. Keep a tiny per-thread active-context stack so the AddLight
// promotion hook can classify that in-flight light before we know its pointer.
// This is bounded, event-driven state only; it is not a scene/world scanner.
struct ActiveDynamicCreationContext
{
    volatile u32 threadId = 0u;
    u32 depth = 0u;
    u64 streamKeys[4]{};
};

static constexpr u32 kContinuousInstanceCapacity = 256u;
static constexpr u32 kContinuousStreamCapacity = 128u;
static constexpr u32 kActiveDynamicContextCapacity = 4u;
static ContinuousLightInstance g_continuousInstances[kContinuousInstanceCapacity]{};
static ContinuousLightStream g_continuousStreams[kContinuousStreamCapacity]{};
static ActiveDynamicCreationContext g_activeDynamicContexts[kActiveDynamicContextCapacity]{};
static volatile u32 g_continuousStateLock = 0u;

static bool try_lock_continuous_state()
{
    // Renderer/game-thread contention should never become a stall. If another
    // light is updating the tiny cache, fail open and preserve its shadow.
    return __atomic_exchange_n(&g_continuousStateLock, 1u, __ATOMIC_ACQUIRE) == 0u;
}

static void unlock_continuous_state()
{
    __atomic_store_n(&g_continuousStateLock, 0u, __ATOMIC_RELEASE);
}

static u32 hash_u64_to_u32(u64 value)
{
    value ^= value >> 33u;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33u;
    value *= 0xc4ceb9fe1a85ec53ull;
    value ^= value >> 33u;
    return static_cast<u32>(value ^ (value >> 32u));
}

static u32 owner_form_id_for_dynamic_light(const void* refr, const void* root)
{
    const void* owner = owner_ref_from_ni_avobject(root);
    if (!owner) owner = refr;
    if (!owner) return 0u;
    return read_u32(static_cast<const u8*>(owner) + kTESFormFormIDOffset);
}

static ContinuousLightStream* find_or_claim_continuous_stream(u64 key, u64 now)
{
    if (key == 0u) return nullptr;
    const u32 start = hash_u64_to_u32(key) & (kContinuousStreamCapacity - 1u);
    ContinuousLightStream* oldest = nullptr;
    for (u32 probe = 0u; probe < 8u; ++probe) {
        auto& entry = g_continuousStreams[(start + probe) & (kContinuousStreamCapacity - 1u)];
        if (entry.key == key) return &entry;
        if (entry.key == 0u || now - entry.lastSeen > 10000u) {
            entry = {};
            entry.key = key;
            return &entry;
        }
        if (!oldest || entry.lastSeen < oldest->lastSeen) oldest = &entry;
    }
    if (!oldest) return nullptr;
    *oldest = {};
    oldest->key = key;
    return oldest;
}

static ContinuousLightStream* find_continuous_stream(u64 key)
{
    if (key == 0u) return nullptr;
    const u32 start = hash_u64_to_u32(key) & (kContinuousStreamCapacity - 1u);
    for (u32 probe = 0u; probe < 8u; ++probe) {
        auto& entry = g_continuousStreams[(start + probe) & (kContinuousStreamCapacity - 1u)];
        if (entry.key == key) return &entry;
        if (entry.key == 0u) return nullptr;
    }
    return nullptr;
}

static void remember_continuous_instance(uptr niLight, u64 streamKey, u64 now)
{
    if (niLight == 0u || streamKey == 0u) return;
    const u32 start = hash_u64_to_u32(static_cast<u64>(niLight)) & (kContinuousInstanceCapacity - 1u);
    ContinuousLightInstance* oldest = nullptr;
    for (u32 probe = 0u; probe < 8u; ++probe) {
        auto& entry = g_continuousInstances[(start + probe) & (kContinuousInstanceCapacity - 1u)];
        if (entry.niLight == niLight || entry.niLight == 0u || now - entry.createdTick > 10000u) {
            entry.niLight = niLight;
            entry.streamKey = streamKey;
            entry.createdTick = now;
            return;
        }
        if (!oldest || entry.createdTick < oldest->createdTick) oldest = &entry;
    }
    if (oldest) {
        oldest->niLight = niLight;
        oldest->streamKey = streamKey;
        oldest->createdTick = now;
    }
}

static const ContinuousLightInstance* find_continuous_instance(uptr niLight, u64 now)
{
    if (niLight == 0u) return nullptr;
    const u32 start = hash_u64_to_u32(static_cast<u64>(niLight)) & (kContinuousInstanceCapacity - 1u);
    for (u32 probe = 0u; probe < 8u; ++probe) {
        const auto& entry = g_continuousInstances[(start + probe) & (kContinuousInstanceCapacity - 1u)];
        if (entry.niLight == niLight) {
            return now - entry.createdTick <= 10000u ? &entry : nullptr;
        }
        if (entry.niLight == 0u) return nullptr;
    }
    return nullptr;
}

static u64 note_dynamic_stream_event(u32 baseFormID, u32 ownerFormID)
{
    if (!g_continuousShadowLimiterEnabled || baseFormID == 0u) return 0u;
    if (!try_lock_continuous_state()) return 0u;

    const u64 now = current_tick_ms();
    const u64 streamKey = (static_cast<u64>(ownerFormID) << 32u) | static_cast<u64>(baseFormID);
    ContinuousLightStream* stream = find_or_claim_continuous_stream(streamKey, now);
    if (stream) {
        if (stream->windowStart == 0u || now - stream->windowStart > g_continuousBurstWindowMs) {
            stream->windowStart = now;
            stream->burstCount = 1u;
        } else if (stream->burstCount < 0xFFFFFFFFu) {
            ++stream->burstCount;
        }
        stream->lastSeen = now;
        if (stream->burstCount >= g_continuousBurstThreshold) {
            // The first BurstThreshold-1 rapid lights promote normally. The
            // threshold-crossing light is the first rate-limited candidate.
            if (stream->sprayUntil < now) stream->lastShadowGrant = now;
            stream->sprayUntil = now + static_cast<u64>(g_continuousHoldMs);
        }
    }
    unlock_continuous_state();
    return stream ? streamKey : 0u;
}

static void remember_dynamic_light_instance(void* niLight, u64 streamKey)
{
    if (!g_continuousShadowLimiterEnabled || !niLight || streamKey == 0u) return;
    if (!try_lock_continuous_state()) return;
    remember_continuous_instance(reinterpret_cast<uptr>(niLight), streamKey, current_tick_ms());
    unlock_continuous_state();
}

static ActiveDynamicCreationContext* push_active_dynamic_context(u64 streamKey)
{
    if (streamKey == 0u) return nullptr;
    const u32 tid = GetCurrentThreadId();
    if (tid == 0u) return nullptr;

    // Prefer an existing slot owned by this thread, otherwise claim an empty one.
    for (u32 pass = 0u; pass < 2u; ++pass) {
        for (u32 i = 0u; i < kActiveDynamicContextCapacity; ++i) {
            auto& ctx = g_activeDynamicContexts[i];
            u32 owner = __atomic_load_n(&ctx.threadId, __ATOMIC_ACQUIRE);
            if (pass == 0u) {
                if (owner != tid) continue;
            } else {
                if (owner != 0u) continue;
                u32 expected = 0u;
                if (!__atomic_compare_exchange_n(&ctx.threadId, &expected, tid, false,
                                                 __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                    continue;
                }
            }

            if (ctx.depth >= 4u) return nullptr;
            ctx.streamKeys[ctx.depth++] = streamKey;
            __atomic_thread_fence(__ATOMIC_RELEASE);
            return &ctx;
        }
    }
    return nullptr;
}

static void pop_active_dynamic_context(ActiveDynamicCreationContext* ctx)
{
    if (!ctx) return;
    const u32 tid = GetCurrentThreadId();
    if (__atomic_load_n(&ctx->threadId, __ATOMIC_ACQUIRE) != tid || ctx->depth == 0u) return;
    ctx->streamKeys[--ctx->depth] = 0u;
    if (ctx->depth == 0u) {
        __atomic_store_n(&ctx->threadId, 0u, __ATOMIC_RELEASE);
    }
}

static u64 active_dynamic_stream_key_for_current_thread()
{
    const u32 tid = GetCurrentThreadId();
    if (tid == 0u) return 0u;
    for (u32 i = 0u; i < kActiveDynamicContextCapacity; ++i) {
        auto& ctx = g_activeDynamicContexts[i];
        if (__atomic_load_n(&ctx.threadId, __ATOMIC_ACQUIRE) != tid) continue;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (ctx.depth == 0u || ctx.depth > 4u) return 0u;
        return ctx.streamKeys[ctx.depth - 1u];
    }
    return 0u;
}

static bool should_skip_continuous_shadow_promotion(const void* niLight)
{
    if (!g_continuousShadowLimiterEnabled || !niLight || processing_suspended()) return false;
    if (!try_lock_continuous_state()) return false;

    const u64 now = current_tick_ms();
    bool skip = false;
    u64 streamKey = active_dynamic_stream_key_for_current_thread();
    if (streamKey == 0u) {
        const ContinuousLightInstance* instance = find_continuous_instance(reinterpret_cast<uptr>(niLight), now);
        if (instance) streamKey = instance->streamKey;
    }
    if (streamKey != 0u) {
        ContinuousLightStream* stream = find_continuous_stream(streamKey);
        if (stream && stream->sprayUntil >= now) {
            if (now - stream->lastShadowGrant < static_cast<u64>(g_continuousShadowIntervalMs)) {
                skip = true;
            } else {
                stream->lastShadowGrant = now;
            }
        }
    }

    unlock_continuous_state();
    return skip;
}

static void hard_disable_existing_ni_light(void* niLight)
{
    if (!niLight) return;
    auto* bytes = static_cast<u8*>(niLight);

    // Turn the live scene object itself off. Open Shaders' IsValidLight() checks
    // this exact kHidden bit before considering a BSLight for clustered/strict
    // submission on subsequent passes.
    const u32 objectFlags = read_u32(bytes + kNiAVObjectFlagsOffsetSE1597);
    write_u32(bytes + kNiAVObjectFlagsOffsetSE1597,
              objectFlags | kNiAVObjectHiddenFlag);

    // Also mark Open Shaders' per-NiLight runtime flags Disabled and clear its
    // transient Shadow bit. This makes luminance queries return zero and keeps
    // the instance semantically disabled even before the hidden-state filter is
    // observed on the next renderer pass.
    auto* runtime = bytes + kNiLightRuntimeDataOffsetSE1597;
    const u32 runtimeFlags = read_u32(runtime);
    write_u32(runtime, (runtimeFlags | kLightFlagDisabled) & ~kLightFlagShadow);
}

static void zero_no_light_payload(void* lightVoid)
{
    if (!lightVoid) return;
    auto* light = static_cast<u8*>(lightVoid);
    write_f32(light + kOffsetColorR, 0.0f);
    write_f32(light + kOffsetColorG, 0.0f);
    write_f32(light + kOffsetColorB, 0.0f);
    write_f32(light + kOffsetFade, 0.0f);
    write_f32(light + kOffsetRadius, 0.0f);
    write_f32(light + kOffsetInvRadius, 0.0f);
    write_f32(light + kOffsetFadeZone, 0.0f);
    write_f32(light + kOffsetSizeBias, 0.0f);
    // ProcessLight is an Open Shaders current-family hook, so its LightData is
    // the 96-byte layout with lightFlags at +0x50.
    const u32 flags = read_u32(light + 0x50u);
    write_u32(light + 0x50u, (flags | kLightFlagDisabled) & ~kLightFlagShadow);
}

// CommonLibSSE SE layout, verified for the hard-supported 1.5.97 runtime.
static constexpr u32 kTESFileArrayDataOffset = 0x00u;
static constexpr u32 kTESFileArraySizeOffset = 0x08u;
static constexpr u32 kTESFileNameOffset = 0x58u;
static constexpr u32 kTESFileCompileIndexOffset = 0x478u;
static constexpr u32 kTESFileSmallIndexOffset = 0x47Au;
static constexpr u32 kTESFileNameCapacity = 260u;

static constexpr u32 kSigTES4 = 0x34534554u; // "TES4"
static constexpr u32 kSigGRUP = 0x50555247u; // "GRUP"
static constexpr u32 kSigLIGH = 0x4847494Cu; // "LIGH"
static constexpr u32 kSigHAZD = 0x445A4148u; // "HAZD"
static constexpr u32 kSigSTAT = 0x54415453u; // "STAT"
static constexpr u32 kSigMSTT = 0x5454534Du; // "MSTT"
static constexpr u32 kSigCELL = 0x4C4C4543u; // "CELL"
static constexpr u32 kSigWRLD = 0x444C5257u; // "WRLD"
static constexpr u32 kSigREFR = 0x52464552u; // "REFR"
static constexpr u32 kSigNAME = 0x454D414Eu; // "NAME"
static constexpr u32 kSigDATA = 0x41544144u; // "DATA"
static constexpr u32 kSigMAST = 0x5453414Du; // "MAST"
static constexpr u32 kSigEDID = 0x44494445u; // "EDID"
static constexpr u32 kSigXXXX = 0x58585858u; // "XXXX"
static constexpr u32 kRecordFlagCompressed = 0x00040000u;
static constexpr u32 kTopGroupType = 0u;
static constexpr u32 kRecordHeaderSize = 24u;
static constexpr u32 kSubrecordHeaderSize = 6u;

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

static u32 bounded_string_length(const char* s, u32 limit)
{
    if (!s) return 0u;
    u32 n = 0u;
    while (n < limit && s[n] != '\0') ++n;
    return n;
}

static bool contains_ascii_ci(const char* text, u32 textLen, const char* needle)
{
    if (!text || !needle) return false;
    const u32 needleLen = string_length(needle);
    if (needleLen == 0u || needleLen > textLen) return false;
    for (u32 i = 0u; i + needleLen <= textLen; ++i) {
        bool match = true;
        for (u32 j = 0u; j < needleLen; ++j) {
            if (ascii_lower(text[i + j]) != ascii_lower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

static bool is_keyword_separator(char c)
{
    return c == ',' || c == ';' || c == '|';
}

static u32 parse_keyword_list(const char* source, KeywordEntry* out, u32 capacity)
{
    if (!source || !out || capacity == 0u) return 0u;
    u32 count = 0u;
    u32 pos = 0u;

    while (source[pos] != '\0' && count < capacity) {
        while (source[pos] == ' ' || source[pos] == '\t' || is_keyword_separator(source[pos])) ++pos;
        if (source[pos] == '\0') break;

        char token[64] = {};
        u32 len = 0u;
        while (source[pos] != '\0' && !is_keyword_separator(source[pos])) {
            const char c = source[pos++];
            // '*' is accepted as friendly wildcard notation. Matching is
            // substring-based already, so '*' is stripped from the needle.
            if (c == '*') continue;
            if (len + 1u < sizeof(token)) token[len++] = c;
        }

        u32 begin = 0u;
        while (begin < len && (token[begin] == ' ' || token[begin] == '\t')) ++begin;
        while (len > begin && (token[len - 1u] == ' ' || token[len - 1u] == '\t')) --len;
        if (len <= begin) continue;

        u32 dst = 0u;
        for (u32 i = begin; i < len && dst + 1u < sizeof(out[count].text); ++i) {
            out[count].text[dst++] = token[i];
        }
        out[count].text[dst] = '\0';
        if (dst != 0u) ++count;
    }
    return count;
}

static bool ascii_equals_ci(const char* a, const char* b)
{
    if (!a || !b) return false;
    u32 i = 0u;
    while (a[i] != '\0' && b[i] != '\0') {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static bool parse_hex_u32(const char* text, u32& valueOut)
{
    valueOut = 0u;
    if (!text) return false;
    u32 i = 0u;
    while (text[i] == ' ' || text[i] == '\t') ++i;
    if (text[i] == '0' && (text[i + 1u] == 'x' || text[i + 1u] == 'X')) i += 2u;

    bool any = false;
    u32 value = 0u;
    for (; text[i] != '\0'; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t') {
            while (text[i] == ' ' || text[i] == '\t') ++i;
            if (text[i] != '\0') return false;
            break;
        }
        u32 digit = 0u;
        if (c >= '0' && c <= '9') digit = static_cast<u32>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = 10u + static_cast<u32>(c - 'a');
        else if (c >= 'A' && c <= 'F') digit = 10u + static_cast<u32>(c - 'A');
        else return false;
        if (value > 0x0FFFFFFFu) return false;
        value = (value << 4u) | digit;
        any = true;
    }
    if (!any || value > 0x00FFFFFFu) return false;
    valueOut = value;
    return true;
}

// Exact environmental exceptions use load-order-independent owner/local IDs:
//   Forms=Lux.esp|00280E41,SomePatch.esl|0ABC
// Comma/semicolon separate entries; '|' separates plugin name from local FormID.
static u32 parse_exact_form_list(const char* source, ExactFormEntry* out, u32 capacity)
{
    if (!source || !out || capacity == 0u) return 0u;
    u32 count = 0u;
    u32 pos = 0u;
    while (source[pos] != '\0' && count < capacity) {
        while (source[pos] == ' ' || source[pos] == '\t' || source[pos] == ',' || source[pos] == ';') ++pos;
        if (source[pos] == '\0') break;

        char token[320] = {};
        u32 len = 0u;
        while (source[pos] != '\0' && source[pos] != ',' && source[pos] != ';') {
            if (len + 1u < sizeof(token)) token[len++] = source[pos];
            ++pos;
        }
        token[len] = '\0';

        u32 begin = 0u;
        while (begin < len && (token[begin] == ' ' || token[begin] == '\t')) ++begin;
        while (len > begin && (token[len - 1u] == ' ' || token[len - 1u] == '\t')) --len;
        if (len <= begin) continue;

        u32 pipe = begin;
        while (pipe < len && token[pipe] != '|') ++pipe;
        if (pipe == begin || pipe >= len) continue;

        u32 pluginEnd = pipe;
        while (pluginEnd > begin && (token[pluginEnd - 1u] == ' ' || token[pluginEnd - 1u] == '\t')) --pluginEnd;
        u32 formBegin = pipe + 1u;
        while (formBegin < len && (token[formBegin] == ' ' || token[formBegin] == '\t')) ++formBegin;
        if (pluginEnd <= begin || formBegin >= len) continue;

        char formText[32] = {};
        u32 formLen = 0u;
        for (u32 i = formBegin; i < len && formLen + 1u < sizeof(formText); ++i) formText[formLen++] = token[i];
        formText[formLen] = '\0';
        u32 localFormID = 0u;
        if (!parse_hex_u32(formText, localFormID)) continue;

        u32 dst = 0u;
        for (u32 i = begin; i < pluginEnd && dst + 1u < sizeof(out[count].plugin); ++i) {
            out[count].plugin[dst++] = token[i];
        }
        out[count].plugin[dst] = '\0';
        if (dst == 0u) continue;
        out[count].localFormID = localFormID;
        ++count;
    }
    return count;
}

static bool exact_form_matches(const char* pluginName, u32 localFormID,
                               const ExactFormEntry* forms, u32 count)
{
    if (!pluginName || !forms || count == 0u) return false;
    for (u32 i = 0u; i < count; ++i) {
        if (forms[i].localFormID == localFormID && ascii_equals_ci(pluginName, forms[i].plugin)) return true;
    }
    return false;
}

static bool editor_id_matches_any(const char* editorID, const KeywordEntry* keywords, u32 count)
{
    if (!editorID || !keywords || count == 0u) return false;
    const u32 len = bounded_string_length(editorID, 512u);
    if (len == 0u || len == 512u) return false;
    for (u32 i = 0u; i < count; ++i) {
        if (keywords[i].text[0] != '\0' && contains_ascii_ci(editorID, len, keywords[i].text)) {
            return true;
        }
    }
    return false;
}

static bool editor_id_matches_all(const char* editorID, const KeywordEntry* keywords, u32 count)
{
    if (!editorID || !keywords || count == 0u) return false;
    const u32 len = bounded_string_length(editorID, 512u);
    if (len == 0u || len == 512u) return false;
    for (u32 i = 0u; i < count; ++i) {
        if (keywords[i].text[0] == '\0' || !contains_ascii_ci(editorID, len, keywords[i].text)) {
            return false;
        }
    }
    return true;
}

static bool any_policy_scan_enabled()
{
    // HAZD->LIGH detection is structural and does not require an EditorID keyword.
    return (g_environmentWhitelistEnabled && (g_whitelistKeywordCount != 0u || g_whitelistFormCount != 0u || g_forceCapKeywordCount != 0u)) ||
           (g_magicHandLightsEnabled && (g_magicHandKeywordCount != 0u || g_magicHandAnyKeywordCount != 0u || g_magicHandFormCount != 0u)) ||
           g_magicImpactLightsEnabled ||
           (g_fireColorEnabled && (g_fireColorKeywordCount != 0u || g_fireColorFormCount != 0u));
}

static void load_editor_id_policy_settings()
{
    memset(g_whitelistKeywords, 0, sizeof(g_whitelistKeywords));
    memset(g_whitelistForms, 0, sizeof(g_whitelistForms));
    memset(g_forceCapKeywords, 0, sizeof(g_forceCapKeywords));
    memset(g_magicHandKeywords, 0, sizeof(g_magicHandKeywords));
    memset(g_magicHandAnyKeywords, 0, sizeof(g_magicHandAnyKeywords));
    memset(g_magicHandForms, 0, sizeof(g_magicHandForms));
    memset(g_magicImpactKeywords, 0, sizeof(g_magicImpactKeywords));
    memset(g_magicImpactAnyKeywords, 0, sizeof(g_magicImpactAnyKeywords));
    memset(g_magicImpactForms, 0, sizeof(g_magicImpactForms));
    memset(g_fireColorKeywords, 0, sizeof(g_fireColorKeywords));
    memset(g_fireColorExcludeKeywords, 0, sizeof(g_fireColorExcludeKeywords));
    memset(g_fireColorForms, 0, sizeof(g_fireColorForms));
    g_whitelistKeywordCount = 0u;
    g_whitelistFormCount = 0u;
    g_forceCapKeywordCount = 0u;
    g_magicHandKeywordCount = 0u;
    g_magicHandAnyKeywordCount = 0u;
    g_magicHandFormCount = 0u;
    g_magicImpactKeywordCount = 0u;
    g_magicImpactAnyKeywordCount = 0u;
    g_magicImpactFormCount = 0u;
    g_fireColorKeywordCount = 0u;
    g_fireColorExcludeKeywordCount = 0u;
    g_fireColorFormCount = 0u;

    char text[2048] = {};
    if (g_environmentWhitelistEnabled) {
        GetPrivateProfileStringA(
            "EnvironmentalWhitelist", "Keywords",
            "Sunlight,SunBounce,FakeSun,FakeWindow,FakeMoon,NightLight",
            text, sizeof(text), kIniPath);
        g_whitelistKeywordCount = parse_keyword_list(text, g_whitelistKeywords, kMaxWhitelistKeywords);

        memset(text, 0, sizeof(text));
        GetPrivateProfileStringA(
            "EnvironmentalWhitelist", "Forms", "",
            text, sizeof(text), kIniPath);
        g_whitelistFormCount = parse_exact_form_list(text, g_whitelistForms, kMaxWhitelistForms);

        memset(text, 0, sizeof(text));
        GetPrivateProfileStringA(
            "EnvironmentalWhitelist", "ForceCapKeywords", "",
            text, sizeof(text), kIniPath);
        g_forceCapKeywordCount = parse_keyword_list(text, g_forceCapKeywords, kMaxForceCapKeywords);
    } else {
        log_line("Global environmental EditorID whitelist disabled by INI.");
    }

    if (g_magicHandLightsEnabled) {
        memset(text, 0, sizeof(text));
        // Primary AND rule plus narrow OR/exact exceptions. Vanilla Conjuration
        // uses MagicLightSummon01, which is a casting-hand light but contains no
        // "Hand" substring, so the old Magic+Hand-only rule missed it.
        GetPrivateProfileStringA(
            "MagicHandLights", "RequiredKeywords", "Magic,Hand",
            text, sizeof(text), kIniPath);
        g_magicHandKeywordCount = parse_keyword_list(text, g_magicHandKeywords, kMaxMagicRuleKeywords);

        memset(text, 0, sizeof(text));
        GetPrivateProfileStringA(
            "MagicHandLights", "AnyKeywords", "MagicLightSummon",
            text, sizeof(text), kIniPath);
        g_magicHandAnyKeywordCount = parse_keyword_list(text, g_magicHandAnyKeywords, kMaxMagicRuleKeywords);

        memset(text, 0, sizeof(text));
        GetPrivateProfileStringA(
            "MagicHandLights", "Forms", "Skyrim.esm|0003BA90,Skyrim.esm|00075347",
            text, sizeof(text), kIniPath);
        g_magicHandFormCount = parse_exact_form_list(text, g_magicHandForms, kMaxWhitelistForms);
    }

    if (g_magicImpactLightsEnabled) {
        memset(text, 0, sizeof(text));
        // Structural HAZD->LIGH detection remains primary. Keep the MAG+Impact
        // AND rule and add narrow OR/exact fallbacks for custom hit-light names.
        GetPrivateProfileStringA(
            "MagicImpactLights", "RequiredKeywords", "MAG,Impact",
            text, sizeof(text), kIniPath);
        g_magicImpactKeywordCount = parse_keyword_list(text, g_magicImpactKeywords, kMaxMagicRuleKeywords);

        memset(text, 0, sizeof(text));
        GetPrivateProfileStringA(
            "MagicImpactLights", "AnyKeywords", "SpellImpact,MagicImpact",
            text, sizeof(text), kIniPath);
        g_magicImpactAnyKeywordCount = parse_keyword_list(text, g_magicImpactAnyKeywords, kMaxMagicRuleKeywords);

        memset(text, 0, sizeof(text));
        GetPrivateProfileStringA(
            "MagicImpactLights", "Forms", "",
            text, sizeof(text), kIniPath);
        g_magicImpactFormCount = parse_exact_form_list(text, g_magicImpactForms, kMaxWhitelistForms);
    }

    if (g_fireColorEnabled) {
        memset(text, 0, sizeof(text));
        GetPrivateProfileStringA(
            "FireColor", "Keywords",
            "Candle,Torch,Fire,Firepit,Fireplace,Brazier,Bazier,Ember,Forge,Hearth,Campfire,Lantern,Chandelier,Candelabre,Sconce,FakeFlicker",
            text, sizeof(text), kIniPath);
        g_fireColorKeywordCount = parse_keyword_list(text, g_fireColorKeywords, kMaxFireColorKeywords);

        memset(text, 0, sizeof(text));
        GetPrivateProfileStringA(
            "FireColor", "ExcludeKeywords",
            "Magic,Spell,Projectile,Impact,Cold,Blue,Frost,Shock,Sunlight,SunBounce,FakeSun,FakeWindow,FakeMoon,NightLight",
            text, sizeof(text), kIniPath);
        g_fireColorExcludeKeywordCount = parse_keyword_list(text, g_fireColorExcludeKeywords, kMaxFireColorExcludeKeywords);

        memset(text, 0, sizeof(text));
        GetPrivateProfileStringA(
            "FireColor", "Forms", "",
            text, sizeof(text), kIniPath);
        g_fireColorFormCount = parse_exact_form_list(text, g_fireColorForms, kMaxFireColorForms);
    }

    log_hex32("Environmental whitelist keyword count: ", g_whitelistKeywordCount);
    log_hex32("Environmental whitelist exact-form count: ", g_whitelistFormCount);
    log_hex32("Environmental force-cap keyword count: ", g_forceCapKeywordCount);
    log_hex32("Magic hand AND-keyword count: ", g_magicHandKeywordCount);
    log_hex32("Magic hand OR-keyword count: ", g_magicHandAnyKeywordCount);
    log_hex32("Magic hand exact-form count: ", g_magicHandFormCount);
    log_hex32("Magic impact AND-keyword count: ", g_magicImpactKeywordCount);
    log_hex32("Magic impact OR-keyword count: ", g_magicImpactAnyKeywordCount);
    log_hex32("Magic impact exact-form count: ", g_magicImpactFormCount);
    log_hex32("Fire color keyword count: ", g_fireColorKeywordCount);
    log_hex32("Fire color exclusion keyword count: ", g_fireColorExcludeKeywordCount);
    log_hex32("Fire color exact-form count: ", g_fireColorFormCount);
}

enum RuntimeLightPolicyBits : u32
{
    kPolicyClassified = 1u << 0,
    kPolicyEnvironmental = 1u << 1,
    kPolicyForceCapped = 1u << 2,
    kPolicyMagicHand = 1u << 3,
    kPolicyMagicImpact = 1u << 4,
    kPolicyFireColor = 1u << 5,
    // Transient, instance-only bit. Never cached by TESObjectLIGH FormID because
    // suppression belongs to the owning placed object (for example Deathbell),
    // not to a reusable LIGH definition that may be valid elsewhere.
    kPolicySuppressLight = 1u << 6
};

// Fixed lock-free cache keyed by full runtime FormID. The high 32 bits hold a
// policy bitmask. kPolicyClassified is always set for known ordinary forms so
// packed value zero remains the unambiguous "unknown/not scanned yet" sentinel.
static constexpr u32 kRuntimeClassCacheCapacity = 8192u;
static_assert((kRuntimeClassCacheCapacity & (kRuntimeClassCacheCapacity - 1u)) == 0u,
              "runtime class cache capacity must be a power of two");
static u64 g_runtimeClassCache[kRuntimeClassCacheCapacity]{};

static u32 hash_form_id(u32 value)
{
    value ^= value >> 16u;
    value *= 0x7FEB352Du;
    value ^= value >> 15u;
    value *= 0x846CA68Bu;
    value ^= value >> 16u;
    return value;
}

static u64 pack_runtime_policy(u32 formID, u32 policy)
{
    return (static_cast<u64>(policy) << 32u) | static_cast<u64>(formID);
}

static u32 unpack_runtime_policy(u64 packed)
{
    return static_cast<u32>(packed >> 32u);
}

static u32 find_cached_runtime_policy(u32 formID)
{
    const u32 start = hash_form_id(formID) & (kRuntimeClassCacheCapacity - 1u);
    for (u32 probe = 0u; probe < kRuntimeClassCacheCapacity; ++probe) {
        const u32 index = (start + probe) & (kRuntimeClassCacheCapacity - 1u);
        const u64 packed = __atomic_load_n(&g_runtimeClassCache[index], __ATOMIC_ACQUIRE);
        if (packed == 0u) return 0u;
        if (static_cast<u32>(packed) == formID) return unpack_runtime_policy(packed);
    }
    return 0u;
}

static bool cache_runtime_policy(u32 formID, u32 policy)
{
    if ((policy & kPolicyClassified) == 0u) return false;
    const u32 start = hash_form_id(formID) & (kRuntimeClassCacheCapacity - 1u);
    for (u32 probe = 0u; probe < kRuntimeClassCacheCapacity; ++probe) {
        const u32 index = (start + probe) & (kRuntimeClassCacheCapacity - 1u);
        for (;;) {
            u64 current = __atomic_load_n(&g_runtimeClassCache[index], __ATOMIC_ACQUIRE);
            if (current == 0u) {
                const u64 desired = pack_runtime_policy(formID, policy);
                u64 expected = 0u;
                if (__atomic_compare_exchange_n(&g_runtimeClassCache[index], &expected, desired,
                                                false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
                    return true;
                }
                current = expected;
            }
            if (static_cast<u32>(current) != formID) break;

            // Multiple structural classifiers can discover the same LIGH in
            // one owner scan (for example an EditorID rule plus HAZD->LIGH).
            // Merge policy bits instead of letting the first discovery win.
            const u32 mergedPolicy = unpack_runtime_policy(current) | policy | kPolicyClassified;
            const u64 desired = pack_runtime_policy(formID, mergedPolicy);
            if (desired == current) return true;
            u64 expected = current;
            if (__atomic_compare_exchange_n(&g_runtimeClassCache[index], &expected, desired,
                                            false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
                return true;
            }
        }
    }
    return false;
}


// Exact placed-reference cache for detached fire bulbs discovered by the
// conservative plugin-file placement scan. These are TESObjectREFR runtime
// FormIDs, not LIGH base FormIDs, so one use of a shared Lux bulb can be marked
// as fire without globally recoloring every use of that bulb definition.
static constexpr u32 kDetachedFireRefCacheCapacity = 8192u;
static_assert((kDetachedFireRefCacheCapacity & (kDetachedFireRefCacheCapacity - 1u)) == 0u,
              "detached fire ref cache capacity must be a power of two");
static u32 g_detachedFireRefCache[kDetachedFireRefCacheCapacity]{};

static bool cache_detached_fire_ref(u32 runtimeRefID)
{
    if (runtimeRefID == 0u || runtimeRefID >= 0xFF000000u) return false;
    const u32 start = hash_form_id(runtimeRefID) & (kDetachedFireRefCacheCapacity - 1u);
    for (u32 probe = 0u; probe < kDetachedFireRefCacheCapacity; ++probe) {
        const u32 index = (start + probe) & (kDetachedFireRefCacheCapacity - 1u);
        u32 current = __atomic_load_n(&g_detachedFireRefCache[index], __ATOMIC_ACQUIRE);
        if (current == runtimeRefID) return true;
        if (current == 0u) {
            u32 expected = 0u;
            if (__atomic_compare_exchange_n(&g_detachedFireRefCache[index], &expected, runtimeRefID,
                                            false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
                return true;
            }
            if (expected == runtimeRefID) return true;
        }
    }
    return false;
}

static bool is_detached_fire_ref(u32 runtimeRefID)
{
    if (runtimeRefID == 0u || runtimeRefID >= 0xFF000000u) return false;
    const u32 start = hash_form_id(runtimeRefID) & (kDetachedFireRefCacheCapacity - 1u);
    for (u32 probe = 0u; probe < kDetachedFireRefCacheCapacity; ++probe) {
        const u32 index = (start + probe) & (kDetachedFireRefCacheCapacity - 1u);
        const u32 current = __atomic_load_n(&g_detachedFireRefCache[index], __ATOMIC_ACQUIRE);
        if (current == 0u) return false;
        if (current == runtimeRefID) return true;
    }
    return false;
}

// Instance bridge from CreatePointLight(refr) to ProcessLight(niLight). Only an
// opaque hash is retained; no TESObjectREFR/NiLight pointer is dereferenced
// after the hook call that supplied it. Both fire and non-fire creations are
// cached so pointer reuse overwrites an old fire association.
static constexpr u32 kDetachedFireInstanceCacheCapacity = 16384u;
static_assert((kDetachedFireInstanceCacheCapacity & (kDetachedFireInstanceCacheCapacity - 1u)) == 0u,
              "detached fire instance cache capacity must be a power of two");
static u64 g_detachedFireInstanceCache[kDetachedFireInstanceCacheCapacity]{};

static u64 make_detached_fire_instance_key(u32 baseFormID, const void* niLight, u32 generation)
{
    u64 value = static_cast<u64>(reinterpret_cast<uptr>(niLight));
    value ^= static_cast<u64>(baseFormID) << 19u;
    value ^= static_cast<u64>(generation) * 0xD6E8FEB86659FD93ull;
    u64 key = mix_identity(value) & ~0x3ull;
    return key != 0u ? key : 0x4ull;
}

static void cache_detached_fire_instance(u32 baseFormID, const void* niLight, bool isFire)
{
    if (baseFormID == 0u || !niLight) return;
    const u32 generation = __atomic_load_n(&g_sceneGeneration, __ATOMIC_ACQUIRE);
    const u64 key = make_detached_fire_instance_key(baseFormID, niLight, generation);
    const u64 desired = key | 0x1ull | (isFire ? 0x2ull : 0ull);
    const u32 start = static_cast<u32>(key ^ (key >> 32u)) &
                      (kDetachedFireInstanceCacheCapacity - 1u);

    for (u32 probe = 0u; probe < kDetachedFireInstanceCacheCapacity; ++probe) {
        const u32 index = (start + probe) & (kDetachedFireInstanceCacheCapacity - 1u);
        u64 current = __atomic_load_n(&g_detachedFireInstanceCache[index], __ATOMIC_ACQUIRE);
        if ((current & ~0x3ull) == key || current == 0u) {
            __atomic_store_n(&g_detachedFireInstanceCache[index], desired, __ATOMIC_RELEASE);
            return;
        }
    }

    // Fail closed under pathological saturation: replace only the home slot.
    // At worst a previous association is forgotten and that light keeps its
    // authored colour.
    __atomic_store_n(&g_detachedFireInstanceCache[start], desired, __ATOMIC_RELEASE);
}

static bool detached_fire_instance_is_marked(u32 baseFormID, const void* niLight)
{
    if (baseFormID == 0u || !niLight) return false;
    const u32 generation = __atomic_load_n(&g_sceneGeneration, __ATOMIC_ACQUIRE);
    const u64 key = make_detached_fire_instance_key(baseFormID, niLight, generation);
    const u32 start = static_cast<u32>(key ^ (key >> 32u)) &
                      (kDetachedFireInstanceCacheCapacity - 1u);

    for (u32 probe = 0u; probe < kDetachedFireInstanceCacheCapacity; ++probe) {
        const u32 index = (start + probe) & (kDetachedFireInstanceCacheCapacity - 1u);
        const u64 current = __atomic_load_n(&g_detachedFireInstanceCache[index], __ATOMIC_ACQUIRE);
        if (current == 0u) return false;
        if ((current & ~0x3ull) == key) return (current & 0x2ull) != 0u;
    }
    return false;
}

static void append_text(char* buffer, u32 capacity, u32& pos, const char* text)
{
    if (!buffer || !text || capacity == 0u) return;
    for (u32 i = 0u; text[i] != '\0' && pos + 1u < capacity; ++i) buffer[pos++] = text[i];
}

static void log_classification(u32 formID, const char* editorID, u32 policy)
{
    if (!g_loggingEnabled || !editorID || (policy & ~kPolicyClassified) == 0u) return;
    char buffer[320] = {};
    u32 pos = 0u;
    append_text(buffer, sizeof(buffer), pos, "POLICY form=0x");
    for (int shift = 28; shift >= 0 && pos + 1u < sizeof(buffer); shift -= 4) {
        buffer[pos++] = hex_digit((formID >> shift) & 0xFu);
    }
    append_text(buffer, sizeof(buffer), pos, " [");
    bool any = false;
    auto add_policy = [&](const char* label) {
        if (any) append_text(buffer, sizeof(buffer), pos, ",");
        append_text(buffer, sizeof(buffer), pos, label);
        any = true;
    };
    if ((policy & kPolicyEnvironmental) != 0u) add_policy("ENV");
    if ((policy & kPolicyForceCapped) != 0u) add_policy("FORCECAP");
    if ((policy & kPolicyMagicHand) != 0u) add_policy("MAGIC_HAND");
    if ((policy & kPolicyMagicImpact) != 0u) add_policy("MAGIC_IMPACT");
    if ((policy & kPolicyFireColor) != 0u) add_policy("FIRE_COLOR");
    if ((policy & kPolicySuppressLight) != 0u) add_policy("NO_LIGHT");
    append_text(buffer, sizeof(buffer), pos, "] editorID=");
    const u32 idLen = bounded_string_length(editorID, 180u);
    for (u32 i = 0u; i < idLen && pos + 1u < sizeof(buffer); ++i) buffer[pos++] = editorID[i];
    buffer[pos] = '\0';
    log_line(buffer);
}

static bool editor_id_is_fire_family(const char* editorID)
{
    return editorID && g_fireColorEnabled && g_fireColorKeywordCount != 0u &&
           editor_id_matches_any(editorID, g_fireColorKeywords, g_fireColorKeywordCount) &&
           !editor_id_matches_any(editorID, g_fireColorExcludeKeywords, g_fireColorExcludeKeywordCount);
}

static u32 classify_editor_id(const char* editorID)
{
    u32 policy = kPolicyClassified;
    if (!editorID) return policy;

    if (g_environmentWhitelistEnabled) {
        if (editor_id_matches_any(editorID, g_forceCapKeywords, g_forceCapKeywordCount)) {
            policy |= kPolicyForceCapped;
        } else if (editor_id_matches_any(editorID, g_whitelistKeywords, g_whitelistKeywordCount)) {
            policy |= kPolicyEnvironmental;
        }
    }

    if (g_magicHandLightsEnabled &&
        ((g_magicHandKeywordCount != 0u &&
          editor_id_matches_all(editorID, g_magicHandKeywords, g_magicHandKeywordCount)) ||
         (g_magicHandAnyKeywordCount != 0u &&
          editor_id_matches_any(editorID, g_magicHandAnyKeywords, g_magicHandAnyKeywordCount)))) {
        policy |= kPolicyMagicHand;
    }

    if (g_magicImpactLightsEnabled &&
        ((g_magicImpactKeywordCount != 0u &&
          editor_id_matches_all(editorID, g_magicImpactKeywords, g_magicImpactKeywordCount)) ||
         (g_magicImpactAnyKeywordCount != 0u &&
          editor_id_matches_any(editorID, g_magicImpactAnyKeywords, g_magicImpactAnyKeywordCount)))) {
        policy |= kPolicyMagicImpact;
    }

    if (editor_id_is_fire_family(editorID)) {
        policy |= kPolicyFireColor;
    }

    return policy;
}

static bool build_data_path(char* out, u32 cap, const char* fileName)
{
    if (!out || cap < 7u || !fileName || fileName[0] == '\0') return false;
    u32 pos = 0u;
    static constexpr char prefix[] = "Data/";
    for (u32 i = 0u; prefix[i] != '\0'; ++i) {
        if (pos + 1u >= cap) return false;
        out[pos++] = prefix[i];
    }
    for (u32 i = 0u; fileName[i] != '\0' && i < kTESFileNameCapacity; ++i) {
        if (pos + 1u >= cap) return false;
        out[pos++] = fileName[i];
    }
    out[pos] = '\0';
    return true;
}

static bool read_exact_at(HANDLE file, u64 offset, void* destination, u32 size)
{
    if (!file || file == invalid_handle_value() || !destination) return false;
    LARGE_INTEGER_ position{};
    position.QuadPart = static_cast<i64>(offset);
    if (position.QuadPart < 0 || !SetFilePointerEx(file, position, nullptr, FILE_BEGIN_)) return false;
    DWORD bytesRead = 0u;
    return ReadFile(file, destination, size, &bytesRead, nullptr) && bytesRead == size;
}

static bool count_plugin_masters(HANDLE file, u64 fileSize, u32 tes4Size, u32& masterCount)
{
    masterCount = 0u;
    const u64 end = 24ull + static_cast<u64>(tes4Size);
    if (end > fileSize) return false;
    u64 pos = 24u;
    u32 extendedSize = 0u;
    while (pos + kSubrecordHeaderSize <= end) {
        u8 header[kSubrecordHeaderSize] = {};
        if (!read_exact_at(file, pos, header, sizeof(header))) return false;
        const u32 sig = read_u32(header);
        const u16 shortSize = read_u16(header + 4u);
        pos += kSubrecordHeaderSize;
        if (sig == kSigXXXX) {
            if (shortSize != 4u || pos + 4u > end) return false;
            u8 ext[4] = {};
            if (!read_exact_at(file, pos, ext, sizeof(ext))) return false;
            extendedSize = read_u32(ext);
            pos += 4u;
            continue;
        }
        const u32 size = extendedSize != 0u ? extendedSize : static_cast<u32>(shortSize);
        extendedSize = 0u;
        if (static_cast<u64>(size) > end - pos) return false;
        if (sig == kSigMAST) ++masterCount;
        pos += size;
    }
    return pos == end;
}

static bool read_ligh_editor_id(HANDLE file, u64 payloadPos, u32 payloadSize,
                                char* editorID, u32 editorCap)
{
    if (!editorID || editorCap < 2u) return false;
    editorID[0] = '\0';
    const u64 end = payloadPos + static_cast<u64>(payloadSize);
    u64 pos = payloadPos;
    u32 extendedSize = 0u;
    while (pos + kSubrecordHeaderSize <= end) {
        u8 header[kSubrecordHeaderSize] = {};
        if (!read_exact_at(file, pos, header, sizeof(header))) return false;
        const u32 sig = read_u32(header);
        const u16 shortSize = read_u16(header + 4u);
        pos += kSubrecordHeaderSize;
        if (sig == kSigXXXX) {
            if (shortSize != 4u || pos + 4u > end) return false;
            u8 ext[4] = {};
            if (!read_exact_at(file, pos, ext, sizeof(ext))) return false;
            extendedSize = read_u32(ext);
            pos += 4u;
            continue;
        }
        const u32 size = extendedSize != 0u ? extendedSize : static_cast<u32>(shortSize);
        extendedSize = 0u;
        if (static_cast<u64>(size) > end - pos) return false;
        if (sig == kSigEDID) {
            const u32 copySize = size < editorCap - 1u ? size : editorCap - 1u;
            if (copySize != 0u && !read_exact_at(file, pos, editorID, copySize)) return false;
            editorID[copySize] = '\0';
            return copySize != 0u;
        }
        pos += size;
    }
    return false;
}

// Skyrim HAZD DATA is 40 bytes. The embedded LIGH FormID is the eighth
// 32-bit field (offset 0x1C). Immersive Spell Effects Emit Light 1.01 is a
// clean reference: it adds dedicated LIGH records and assigns them through
// this exact HAZD field for persistent fire/frost/shock ground hazards.
static bool read_hazd_light_form_id(HANDLE file, u64 payloadPos, u32 payloadSize, u32& lightFormID)
{
    lightFormID = 0u;
    const u64 end = payloadPos + static_cast<u64>(payloadSize);
    u64 pos = payloadPos;
    u32 extendedSize = 0u;
    while (pos + kSubrecordHeaderSize <= end) {
        u8 header[kSubrecordHeaderSize] = {};
        if (!read_exact_at(file, pos, header, sizeof(header))) return false;
        const u32 sig = read_u32(header);
        const u16 shortSize = read_u16(header + 4u);
        pos += kSubrecordHeaderSize;
        if (sig == kSigXXXX) {
            if (shortSize != 4u || pos + 4u > end) return false;
            u8 ext[4] = {};
            if (!read_exact_at(file, pos, ext, sizeof(ext))) return false;
            extendedSize = read_u32(ext);
            pos += 4u;
            continue;
        }
        const u32 size = extendedSize != 0u ? extendedSize : static_cast<u32>(shortSize);
        extendedSize = 0u;
        if (static_cast<u64>(size) > end - pos) return false;
        if (sig == kSigDATA) {
            if (size < 32u) return true;
            u8 lightBytes[4] = {};
            if (!read_exact_at(file, pos + 28u, lightBytes, sizeof(lightBytes))) return false;
            lightFormID = read_u32(lightBytes);
            return true;
        }
        pos += size;
    }
    return true;
}

static u32 make_runtime_form_id(const u8* tesFile, u32 localID)
{
    if (!tesFile) return 0u;
    const u8 compileIndex = tesFile[kTESFileCompileIndexOffset];
    const u16 smallIndex = read_u16(tesFile + kTESFileSmallIndexOffset);
    if (compileIndex == 0xFFu) return 0u;
    if (compileIndex == 0xFEu) {
        return 0xFE000000u | ((static_cast<u32>(smallIndex) & 0x0FFFu) << 12u) |
               (localID & 0x00000FFFu);
    }
    return (static_cast<u32>(compileIndex) << 24u) | (localID & 0x00FFFFFFu);
}


struct DetachedFireLocalLight
{
    u32 localFormID = 0u;
    bool eligible = false;
};

struct DetachedFireSourcePlacement
{
    u32 cellRawFormID = 0u;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float bestDistanceSq = 0.0f;
    u32 bestLightRefLocalID = 0u;
};

static constexpr u32 kMaxDetachedFireSourceBases = 512u;
static constexpr u32 kMaxDetachedFireLocalLights = 2048u;
static constexpr u32 kMaxDetachedFireSourcePlacements = 4096u;

struct DetachedFirePluginScanState
{
    u32 sourceBaseLocalIDs[kMaxDetachedFireSourceBases]{};
    u32 sourceBaseCount = 0u;
    DetachedFireLocalLight localLights[kMaxDetachedFireLocalLights]{};
    u32 localLightCount = 0u;
    DetachedFireSourcePlacement sourcePlacements[kMaxDetachedFireSourcePlacements]{};
    u32 sourcePlacementCount = 0u;
    bool sourceBaseOverflow = false;
    bool localLightOverflow = false;
    bool sourcePlacementOverflow = false;
};

static bool detached_source_base_known(const DetachedFirePluginScanState* state, u32 localFormID)
{
    if (!state || localFormID == 0u) return false;
    for (u32 i = 0u; i < state->sourceBaseCount; ++i) {
        if (state->sourceBaseLocalIDs[i] == localFormID) return true;
    }
    return false;
}

static void detached_add_source_base(DetachedFirePluginScanState* state, u32 localFormID)
{
    if (!state || localFormID == 0u || detached_source_base_known(state, localFormID)) return;
    if (state->sourceBaseCount >= kMaxDetachedFireSourceBases) {
        state->sourceBaseOverflow = true;
        return;
    }
    state->sourceBaseLocalIDs[state->sourceBaseCount++] = localFormID;
}

static bool detached_local_light_eligible(const DetachedFirePluginScanState* state, u32 localFormID)
{
    if (!state || localFormID == 0u) return false;
    for (u32 i = 0u; i < state->localLightCount; ++i) {
        if (state->localLights[i].localFormID == localFormID) {
            return state->localLights[i].eligible;
        }
    }
    return false;
}

static void detached_add_local_light(DetachedFirePluginScanState* state, u32 localFormID, bool eligible)
{
    if (!state || localFormID == 0u) return;
    for (u32 i = 0u; i < state->localLightCount; ++i) {
        if (state->localLights[i].localFormID == localFormID) {
            state->localLights[i].eligible = eligible;
            return;
        }
    }
    if (state->localLightCount >= kMaxDetachedFireLocalLights) {
        state->localLightOverflow = true;
        return;
    }
    state->localLights[state->localLightCount].localFormID = localFormID;
    state->localLights[state->localLightCount].eligible = eligible;
    ++state->localLightCount;
}

static bool read_ligh_authored_warm(HANDLE file, u64 payloadPos, u32 payloadSize, bool& warm)
{
    warm = false;
    const u64 end = payloadPos + static_cast<u64>(payloadSize);
    u64 pos = payloadPos;
    u32 extendedSize = 0u;
    while (pos + kSubrecordHeaderSize <= end) {
        u8 header[kSubrecordHeaderSize] = {};
        if (!read_exact_at(file, pos, header, sizeof(header))) return false;
        const u32 sig = read_u32(header);
        const u16 shortSize = read_u16(header + 4u);
        pos += kSubrecordHeaderSize;
        if (sig == kSigXXXX) {
            if (shortSize != 4u || pos + 4u > end) return false;
            u8 ext[4] = {};
            if (!read_exact_at(file, pos, ext, sizeof(ext))) return false;
            extendedSize = read_u32(ext);
            pos += 4u;
            continue;
        }
        const u32 size = extendedSize != 0u ? extendedSize : static_cast<u32>(shortSize);
        extendedSize = 0u;
        if (static_cast<u64>(size) > end - pos) return false;
        if (sig == kSigDATA) {
            if (size < 12u) return false;
            u8 rgb[3] = {};
            if (!read_exact_at(file, pos + 8u, rgb, sizeof(rgb))) return false;
            const u32 r = rgb[0];
            const u32 g = rgb[1];
            const u32 b = rgb[2];
            warm = (r * 100u >= g * 95u) && (g * 100u > b * 110u);
            return true;
        }
        pos += size;
    }
    // A LIGH without a readable DATA RGB cannot safely participate in any
    // colour inference. Fail closed even when the warm requirement is disabled.
    return false;
}

static bool scan_fire_source_top_group(HANDLE file, u64 fileSize, u64 groupPos, u32 groupSize,
                                       u32 expectedSig, u32 masterCount,
                                       DetachedFirePluginScanState* state,
                                       u32& compressedSkipped)
{
    if (!state || groupSize < kRecordHeaderSize ||
        groupPos + static_cast<u64>(groupSize) > fileSize) return false;
    const u64 end = groupPos + static_cast<u64>(groupSize);
    u64 pos = groupPos + kRecordHeaderSize;

    while (pos + kRecordHeaderSize <= end) {
        u8 header[kRecordHeaderSize] = {};
        if (!read_exact_at(file, pos, header, sizeof(header))) return false;
        const u32 sig = read_u32(header);
        const u32 size = read_u32(header + 4u);

        if (sig == kSigGRUP) {
            if (size < kRecordHeaderSize || static_cast<u64>(size) > end - pos) return false;
            pos += size;
            continue;
        }

        const u64 recordBytes = static_cast<u64>(kRecordHeaderSize) + static_cast<u64>(size);
        if (recordBytes > end - pos) return false;
        if (sig == expectedSig) {
            const u32 flags = read_u32(header + 8u);
            const u32 rawFormID = read_u32(header + 12u);
            const u32 ownerIndex = (rawFormID >> 24u) & 0xFFu;
            if (ownerIndex == masterCount) {
                if ((flags & kRecordFlagCompressed) != 0u) {
                    ++compressedSkipped;
                } else {
                    char editorID[256] = {};
                    if (read_ligh_editor_id(file, pos + kRecordHeaderSize, size,
                                            editorID, sizeof(editorID)) &&
                        editor_id_is_fire_family(editorID)) {
                        detached_add_source_base(state, rawFormID & 0x00FFFFFFu);
                    }
                }
            }
        }
        pos += recordBytes;
    }
    return pos == end;
}

static bool read_refr_base_and_position(HANDLE file, u64 payloadPos, u32 payloadSize,
                                        u32& rawBaseFormID, float& x, float& y, float& z)
{
    rawBaseFormID = 0u;
    bool havePosition = false;
    const u64 end = payloadPos + static_cast<u64>(payloadSize);
    u64 pos = payloadPos;
    u32 extendedSize = 0u;

    while (pos + kSubrecordHeaderSize <= end) {
        u8 header[kSubrecordHeaderSize] = {};
        if (!read_exact_at(file, pos, header, sizeof(header))) return false;
        const u32 sig = read_u32(header);
        const u16 shortSize = read_u16(header + 4u);
        pos += kSubrecordHeaderSize;
        if (sig == kSigXXXX) {
            if (shortSize != 4u || pos + 4u > end) return false;
            u8 ext[4] = {};
            if (!read_exact_at(file, pos, ext, sizeof(ext))) return false;
            extendedSize = read_u32(ext);
            pos += 4u;
            continue;
        }
        const u32 size = extendedSize != 0u ? extendedSize : static_cast<u32>(shortSize);
        extendedSize = 0u;
        if (static_cast<u64>(size) > end - pos) return false;

        if (sig == kSigNAME && size >= 4u) {
            u8 value[4] = {};
            if (!read_exact_at(file, pos, value, sizeof(value))) return false;
            rawBaseFormID = read_u32(value);
        } else if (sig == kSigDATA && size >= 12u) {
            u8 value[12] = {};
            if (!read_exact_at(file, pos, value, sizeof(value))) return false;
            x = read_f32(value + 0u);
            y = read_f32(value + 4u);
            z = read_f32(value + 8u);
            havePosition =
                (x > -10000000.0f && x < 10000000.0f) &&
                (y > -10000000.0f && y < 10000000.0f) &&
                (z > -10000000.0f && z < 10000000.0f);
        }
        pos += size;
    }
    return rawBaseFormID != 0u && havePosition;
}

enum class DetachedReferenceScanPhase : u32
{
    CollectSources = 0u,
    MatchNearestLights = 1u
};

static bool scan_detached_reference_group(HANDLE file, u64 fileSize,
                                          u64 groupPos, u32 groupSize,
                                          u32 currentCellRawFormID, u32 depth,
                                          u32 masterCount,
                                          DetachedFirePluginScanState* state,
                                          DetachedReferenceScanPhase phase,
                                          u32& compressedSkipped)
{
    if (!state || depth > 16u || groupSize < kRecordHeaderSize ||
        groupPos + static_cast<u64>(groupSize) > fileSize) return false;

    const u64 end = groupPos + static_cast<u64>(groupSize);
    u64 pos = groupPos + kRecordHeaderSize;
    while (pos + kRecordHeaderSize <= end) {
        u8 header[kRecordHeaderSize] = {};
        if (!read_exact_at(file, pos, header, sizeof(header))) return false;
        const u32 sig = read_u32(header);
        const u32 size = read_u32(header + 4u);

        if (sig == kSigGRUP) {
            if (size < kRecordHeaderSize || static_cast<u64>(size) > end - pos) return false;
            const u32 label = read_u32(header + 8u);
            const u32 groupType = read_u32(header + 12u);
            u32 childCell = currentCellRawFormID;
            if (groupType == 6u || groupType == 8u || groupType == 9u || groupType == 10u) {
                childCell = label;
            }
            if (!scan_detached_reference_group(file, fileSize, pos, size, childCell,
                                               depth + 1u, masterCount, state,
                                               phase, compressedSkipped)) {
                return false;
            }
            pos += size;
            continue;
        }

        const u64 recordBytes = static_cast<u64>(kRecordHeaderSize) + static_cast<u64>(size);
        if (recordBytes > end - pos) return false;
        if (sig == kSigREFR && currentCellRawFormID != 0u) {
            const u32 flags = read_u32(header + 8u);
            if ((flags & kRecordFlagCompressed) != 0u) {
                ++compressedSkipped;
            } else {
                const u32 rawRefFormID = read_u32(header + 12u);
                u32 rawBaseFormID = 0u;
                float x = 0.0f, y = 0.0f, z = 0.0f;
                if (read_refr_base_and_position(file, pos + kRecordHeaderSize, size,
                                                rawBaseFormID, x, y, z)) {
                    const u32 baseOwnerIndex = (rawBaseFormID >> 24u) & 0xFFu;
                    const u32 baseLocalID = rawBaseFormID & 0x00FFFFFFu;

                    if (phase == DetachedReferenceScanPhase::CollectSources) {
                        if (baseOwnerIndex == masterCount &&
                            detached_source_base_known(state, baseLocalID)) {
                            if (state->sourcePlacementCount < kMaxDetachedFireSourcePlacements) {
                                auto& source = state->sourcePlacements[state->sourcePlacementCount++];
                                source.cellRawFormID = currentCellRawFormID;
                                source.x = x;
                                source.y = y;
                                source.z = z;
                                source.bestDistanceSq = 0.0f;
                                source.bestLightRefLocalID = 0u;
                            } else {
                                state->sourcePlacementOverflow = true;
                            }
                        }
                    } else if (baseOwnerIndex == masterCount &&
                               detached_local_light_eligible(state, baseLocalID) &&
                               ((rawRefFormID >> 24u) & 0xFFu) == masterCount) {
                        const float maxDistSq =
                            g_fireColorDetachedPairDistance * g_fireColorDetachedPairDistance;
                        const u32 refLocalID = rawRefFormID & 0x00FFFFFFu;
                        for (u32 i = 0u; i < state->sourcePlacementCount; ++i) {
                            auto& source = state->sourcePlacements[i];
                            if (source.cellRawFormID != currentCellRawFormID) continue;
                            const float dx = x - source.x;
                            const float dy = y - source.y;
                            const float dz = z - source.z;
                            const float distSq = dx * dx + dy * dy + dz * dz;
                            if (distSq > maxDistSq) continue;
                            if (source.bestLightRefLocalID == 0u || distSq < source.bestDistanceSq) {
                                source.bestDistanceSq = distSq;
                                source.bestLightRefLocalID = refLocalID;
                            }
                        }
                    }
                }
            }
        }
        pos += recordBytes;
    }
    return pos == end;
}

static bool scan_detached_reference_top_groups(HANDLE file, u64 fileSize, u32 tes4Size,
                                               u32 masterCount,
                                               DetachedFirePluginScanState* state,
                                               DetachedReferenceScanPhase phase,
                                               u32& compressedSkipped)
{
    u64 pos = kRecordHeaderSize + static_cast<u64>(tes4Size);
    while (pos + kRecordHeaderSize <= fileSize) {
        u8 header[kRecordHeaderSize] = {};
        if (!read_exact_at(file, pos, header, sizeof(header))) return false;
        const u32 sig = read_u32(header);
        const u32 size = read_u32(header + 4u);

        if (sig == kSigGRUP) {
            if (size < kRecordHeaderSize || static_cast<u64>(size) > fileSize - pos) return false;
            const u32 label = read_u32(header + 8u);
            const u32 groupType = read_u32(header + 12u);
            if (groupType == kTopGroupType && (label == kSigCELL || label == kSigWRLD)) {
                if (!scan_detached_reference_group(file, fileSize, pos, size, 0u, 0u,
                                                   masterCount, state, phase,
                                                   compressedSkipped)) {
                    return false;
                }
            }
            pos += size;
            continue;
        }

        const u64 recordBytes = static_cast<u64>(kRecordHeaderSize) + static_cast<u64>(size);
        if (recordBytes > fileSize - pos) return false;
        pos += recordBytes;
    }
    return pos == fileSize;
}

static bool scan_ligh_top_group(HANDLE file, u64 fileSize, u64 groupPos, u32 groupSize,
                                u32 masterCount, const u8* tesFile,
                                DetachedFirePluginScanState* detachedState,
                                u32& envCount, u32& forceCount,
                                u32& handCount, u32& impactCount, u32& fireCount,
                                u32& compressedSkipped)
{
    if (groupSize < kRecordHeaderSize || groupPos + static_cast<u64>(groupSize) > fileSize) return false;
    const u64 end = groupPos + static_cast<u64>(groupSize);
    u64 pos = groupPos + kRecordHeaderSize;

    while (pos + kRecordHeaderSize <= end) {
        u8 header[kRecordHeaderSize] = {};
        if (!read_exact_at(file, pos, header, sizeof(header))) return false;
        const u32 sig = read_u32(header);
        const u32 size = read_u32(header + 4u);

        if (sig == kSigGRUP) {
            // LIGH top groups are flat in Skyrim plugins. If a tool ever emits
            // a nested group here, skip it safely rather than misreading bytes.
            if (size < kRecordHeaderSize || static_cast<u64>(size) > end - pos) return false;
            pos += size;
            continue;
        }

        const u64 recordBytes = static_cast<u64>(kRecordHeaderSize) + static_cast<u64>(size);
        if (recordBytes > end - pos) return false;
        if (sig == kSigLIGH) {
            const u32 flags = read_u32(header + 8u);
            const u32 rawFormID = read_u32(header + 12u);
            const u32 ownerIndex = (rawFormID >> 24u) & 0xFFu;
            if (ownerIndex == masterCount) {
                const u32 localFormID = rawFormID & 0x00FFFFFFu;
                const char* ownerName = reinterpret_cast<const char*>(tesFile + kTESFileNameOffset);
                const bool exactEnvironmental = g_environmentWhitelistEnabled &&
                    exact_form_matches(ownerName, localFormID, g_whitelistForms, g_whitelistFormCount);
                const bool exactMagicHand = g_magicHandLightsEnabled &&
                    exact_form_matches(ownerName, localFormID, g_magicHandForms, g_magicHandFormCount);
                const bool exactMagicImpact = g_magicImpactLightsEnabled &&
                    exact_form_matches(ownerName, localFormID, g_magicImpactForms, g_magicImpactFormCount);
                const bool exactFireColor = g_fireColorEnabled &&
                    exact_form_matches(ownerName, localFormID, g_fireColorForms, g_fireColorFormCount);

                u32 policy = kPolicyClassified;
                char editorID[256] = {};
                bool haveEditorID = false;
                if ((flags & kRecordFlagCompressed) != 0u) {
                    ++compressedSkipped;
                } else {
                    haveEditorID = read_ligh_editor_id(file, pos + kRecordHeaderSize, size,
                                                       editorID, sizeof(editorID));
                    if (haveEditorID) policy = classify_editor_id(editorID);
                }

                // ForceCapKeywords deliberately remains the highest-precedence
                // environmental rule. Otherwise an exact Plugin|LocalFormID
                // exception can mark even a compressed LIGH as environmental.
                if (exactEnvironmental && (policy & kPolicyForceCapped) == 0u) {
                    policy |= kPolicyEnvironmental;
                }
                if (exactMagicHand) {
                    policy |= kPolicyMagicHand;
                }
                if (exactMagicImpact) {
                    policy |= kPolicyMagicImpact;
                }
                if (exactFireColor) {
                    policy |= kPolicyFireColor;
                }

                bool authoredWarm = false;
                bool haveAuthoredWarm = false;
                if ((flags & kRecordFlagCompressed) == 0u && haveEditorID &&
                    (detachedState || (g_fireColorRequireWarm &&
                     (policy & kPolicyFireColor) != 0u && !exactFireColor))) {
                    haveAuthoredWarm = read_ligh_authored_warm(
                        file, pos + kRecordHeaderSize, size, authoredWarm);
                }

                // EDID fire names are only accepted when the authored light is
                // already warm. This keeps intentionally blue/neutral specials
                // (for example Lux NocturnalFire/DwemerLantern) untouched.
                // An explicit FireColor.Forms entry deliberately bypasses this gate.
                if (g_fireColorRequireWarm && !exactFireColor &&
                    (policy & kPolicyFireColor) != 0u &&
                    (!haveAuthoredWarm || !authoredWarm)) {
                    policy &= ~kPolicyFireColor;
                }

                if (detachedState && (flags & kRecordFlagCompressed) == 0u && haveEditorID) {
                    const bool excluded =
                        editor_id_matches_any(editorID, g_fireColorExcludeKeywords,
                                              g_fireColorExcludeKeywordCount);
                    // Detached pairing is fallback inference only. A light already
                    // classified as fire does not need a spatial association.
                    const bool structuralVeto =
                        (policy & (kPolicyEnvironmental | kPolicyMagicHand |
                                   kPolicyMagicImpact | kPolicyFireColor)) != 0u;
                    const bool warmEnough = !g_fireColorDetachedRequireWarm || authoredWarm;
                    const bool eligible = haveAuthoredWarm && warmEnough && !excluded && !structuralVeto;
                    detached_add_local_light(detachedState, localFormID, eligible);
                }

                if ((policy & ~kPolicyClassified) != 0u) {
                    const u32 runtimeFormID = make_runtime_form_id(tesFile, localFormID);
                    if (runtimeFormID != 0u && cache_runtime_policy(runtimeFormID, policy)) {
                        if ((policy & kPolicyEnvironmental) != 0u) ++envCount;
                        if ((policy & kPolicyForceCapped) != 0u) ++forceCount;
                        if ((policy & kPolicyMagicHand) != 0u) ++handCount;
                        if ((policy & kPolicyMagicImpact) != 0u) ++impactCount;
                        if ((policy & kPolicyFireColor) != 0u) ++fireCount;
                        log_classification(runtimeFormID, haveEditorID ? editorID : "<exact-form>", policy);
                    }
                }
            }
        }
        pos += recordBytes;
    }
    return pos == end;
}

static bool scan_hazd_top_group(HANDLE file, u64 fileSize, u64 groupPos, u32 groupSize,
                                u32 masterCount, const u8* tesFile,
                                u32& hazardLightCount, u32& compressedSkipped)
{
    if (groupSize < kRecordHeaderSize || groupPos + static_cast<u64>(groupSize) > fileSize) return false;
    const u64 end = groupPos + static_cast<u64>(groupSize);
    u64 pos = groupPos + kRecordHeaderSize;

    while (pos + kRecordHeaderSize <= end) {
        u8 header[kRecordHeaderSize] = {};
        if (!read_exact_at(file, pos, header, sizeof(header))) return false;
        const u32 sig = read_u32(header);
        const u32 size = read_u32(header + 4u);

        if (sig == kSigGRUP) {
            if (size < kRecordHeaderSize || static_cast<u64>(size) > end - pos) return false;
            pos += size;
            continue;
        }

        const u64 recordBytes = static_cast<u64>(kRecordHeaderSize) + static_cast<u64>(size);
        if (recordBytes > end - pos) return false;
        if (sig == kSigHAZD && g_magicImpactLightsEnabled) {
            const u32 flags = read_u32(header + 8u);
            if ((flags & kRecordFlagCompressed) != 0u) {
                ++compressedSkipped;
            } else {
                u32 rawLightFormID = 0u;
                if (!read_hazd_light_form_id(file, pos + kRecordHeaderSize, size, rawLightFormID)) return false;
                if (rawLightFormID != 0u) {
                    // A plugin-file FormID's high byte selects one of its masters;
                    // masterCount selects the plugin itself. We can translate local
                    // references without depending on any additional TESFile layout.
                    // Master-owned references are intentionally not translated here
                    // because doing that would require extra loaded-master ABI assumptions;
                    // the existing MAG+Impact fallback remains available for those cases.
                    const u32 ownerIndex = (rawLightFormID >> 24u) & 0xFFu;
                    if (ownerIndex == masterCount) {
                        const u32 runtimeFormID = make_runtime_form_id(tesFile, rawLightFormID & 0x00FFFFFFu);
                        if (runtimeFormID != 0u &&
                            cache_runtime_policy(runtimeFormID, kPolicyClassified | kPolicyMagicImpact)) {
                            ++hazardLightCount;
                        }
                    }
                }
            }
        }
        pos += recordBytes;
    }
    return pos == end;
}

static bool scan_owner_plugin(const u8* tesFile)
{
    if (!tesFile || !any_policy_scan_enabled()) return false;
    const char* fileName = reinterpret_cast<const char*>(tesFile + kTESFileNameOffset);
    const u32 nameLen = bounded_string_length(fileName, kTESFileNameCapacity);
    if (nameLen == 0u || nameLen >= kTESFileNameCapacity) return false;

    char path[320] = {};
    if (!build_data_path(path, sizeof(path), fileName)) return false;
    HANDLE file = CreateFileA(path, GENERIC_READ_, FILE_SHARE_READ_, nullptr,
                              OPEN_EXISTING_, FILE_ATTRIBUTE_NORMAL_, nullptr);
    if (file == nullptr || file == invalid_handle_value()) return false;

    LARGE_INTEGER_ sizeValue{};
    if (!GetFileSizeEx(file, &sizeValue) || sizeValue.QuadPart < 24) {
        CloseHandle(file);
        return false;
    }
    const u64 fileSize = static_cast<u64>(sizeValue.QuadPart);

    u8 tes4Header[kRecordHeaderSize] = {};
    if (!read_exact_at(file, 0u, tes4Header, sizeof(tes4Header)) || read_u32(tes4Header) != kSigTES4) {
        CloseHandle(file);
        return false;
    }
    const u32 tes4Size = read_u32(tes4Header + 4u);
    if (static_cast<u64>(tes4Size) + kRecordHeaderSize > fileSize) {
        CloseHandle(file);
        return false;
    }

    u32 masterCount = 0u;
    if (!count_plugin_masters(file, fileSize, tes4Size, masterCount) || masterCount > 0xFDu) {
        CloseHandle(file);
        return false;
    }

    DetachedFirePluginScanState* detachedState = nullptr;
    if (g_fireColorEnabled && g_fireColorPairDetachedBulbs &&
        g_fireColorDetachedPairDistance > 0.0f) {
        detachedState = static_cast<DetachedFirePluginScanState*>(
            VirtualAlloc(nullptr, sizeof(DetachedFirePluginScanState),
                         MEM_RESERVE_ | MEM_COMMIT_, PAGE_READWRITE_));
        if (detachedState) {
            memset(detachedState, 0, sizeof(DetachedFirePluginScanState));
        }
    }

    bool foundLighGroup = false;
    bool parsedLighGroup = false;
    bool parsedHazdGroup = true;
    bool detachedDefinitionsValid = true;
    u32 envCount = 0u;
    u32 forceCount = 0u;
    u32 handCount = 0u;
    u32 impactCount = 0u;
    u32 fireCount = 0u;
    u32 hazardLightCount = 0u;
    u32 compressedSkipped = 0u;
    u32 detachedAssociatedCount = 0u;

    u64 pos = kRecordHeaderSize + static_cast<u64>(tes4Size);
    while (pos + kRecordHeaderSize <= fileSize) {
        u8 header[kRecordHeaderSize] = {};
        if (!read_exact_at(file, pos, header, sizeof(header))) break;
        const u32 sig = read_u32(header);
        const u32 size = read_u32(header + 4u);
        if (sig == kSigGRUP) {
            if (size < kRecordHeaderSize || static_cast<u64>(size) > fileSize - pos) break;
            const u32 label = read_u32(header + 8u);
            const u32 groupType = read_u32(header + 12u);
            if (groupType == kTopGroupType) {
                if (label == kSigLIGH) {
                    foundLighGroup = true;
                    parsedLighGroup = scan_ligh_top_group(
                        file, fileSize, pos, size, masterCount, tesFile, detachedState,
                        envCount, forceCount, handCount, impactCount, fireCount, compressedSkipped);
                    if (!parsedLighGroup) break;
                } else if (label == kSigHAZD && g_magicImpactLightsEnabled) {
                    parsedHazdGroup = scan_hazd_top_group(
                        file, fileSize, pos, size, masterCount, tesFile,
                        hazardLightCount, compressedSkipped);
                    if (!parsedHazdGroup) break;
                } else if (detachedState && (label == kSigSTAT || label == kSigMSTT)) {
                    if (!scan_fire_source_top_group(
                            file, fileSize, pos, size, label, masterCount,
                            detachedState, compressedSkipped)) {
                        detachedDefinitionsValid = false;
                    }
                }
            }
            pos += size;
            continue;
        }
        const u64 recordBytes = static_cast<u64>(kRecordHeaderSize) + static_cast<u64>(size);
        if (recordBytes > fileSize - pos) break;
        pos += recordBytes;
    }

    if (detachedState && detachedDefinitionsValid &&
        detachedState->sourceBaseCount != 0u && detachedState->localLightCount != 0u) {
        const bool sourcesParsed = scan_detached_reference_top_groups(
            file, fileSize, tes4Size, masterCount, detachedState,
            DetachedReferenceScanPhase::CollectSources, compressedSkipped);
        bool matchesParsed = false;
        if (sourcesParsed && detachedState->sourcePlacementCount != 0u) {
            matchesParsed = scan_detached_reference_top_groups(
                file, fileSize, tes4Size, masterCount, detachedState,
                DetachedReferenceScanPhase::MatchNearestLights, compressedSkipped);
        }

        if (sourcesParsed && (detachedState->sourcePlacementCount == 0u || matchesParsed)) {
            for (u32 i = 0u; i < detachedState->sourcePlacementCount; ++i) {
                const u32 localRefID = detachedState->sourcePlacements[i].bestLightRefLocalID;
                if (localRefID == 0u) continue;
                const u32 runtimeRefID = make_runtime_form_id(tesFile, localRefID);
                if (runtimeRefID != 0u && cache_detached_fire_ref(runtimeRefID)) {
                    ++detachedAssociatedCount;
                }
            }
        } else {
            detachedDefinitionsValid = false;
        }
    }

    CloseHandle(file);

    if (g_loggingEnabled && foundLighGroup) {
        char buffer[320] = {};
        u32 out = 0u;
        static constexpr char prefix[] = "POLICY scanned owner=";
        for (u32 i = 0u; prefix[i] != '\0' && out + 1u < sizeof(buffer); ++i) buffer[out++] = prefix[i];
        for (u32 i = 0u; i < nameLen && out + 1u < sizeof(buffer); ++i) buffer[out++] = fileName[i];
        buffer[out] = '\0';
        log_line(buffer);
        log_hex32("POLICY owner environmental matches: ", envCount);
        log_hex32("POLICY owner force-cap matches: ", forceCount);
        log_hex32("POLICY owner magic-hand matches: ", handCount);
        log_hex32("POLICY owner magic-impact EDID matches: ", impactCount);
        log_hex32("POLICY owner fire-color matches: ", fireCount);
        log_hex32("POLICY owner HAZD-linked local lights: ", hazardLightCount);
        if (detachedState) {
            log_hex32("POLICY detached fire source base definitions: ", detachedState->sourceBaseCount);
            log_hex32("POLICY detached local light definitions indexed: ", detachedState->localLightCount);
            log_hex32("POLICY detached placed fire sources: ", detachedState->sourcePlacementCount);
            log_hex32("POLICY detached fire-source/light associations: ", detachedAssociatedCount);
            if (!detachedDefinitionsValid) {
                log_line("WARNING: detached fire placement scan failed closed; normal LIGH fire classification remains active.");
            }
            if (detachedState->sourceBaseOverflow ||
                detachedState->localLightOverflow ||
                detachedState->sourcePlacementOverflow) {
                log_line("WARNING: detached fire scan scratch capacity reached; excess placements were ignored.");
            }
        }
        if (compressedSkipped != 0u) {
            log_hex32("POLICY compressed LIGH/HAZD/source/REFR records safely skipped: ", compressedSkipped);
        }
    }

    if (detachedState) {
        VirtualFree(detachedState, 0u, MEM_RELEASE_);
    }

    return foundLighGroup && parsedLighGroup && parsedHazdGroup;
}

// Per-TESFile scan state packed into one atomic u64. TESFile pointers are at
// least 4-byte aligned, leaving the low 2 bits for state: 1=scanning,
// 2=complete, 3=failed. This prevents repeated disk scans when many instances
// share the same defining plugin.
static constexpr u32 kPluginScanCacheCapacity = 2048u;
static_assert((kPluginScanCacheCapacity & (kPluginScanCacheCapacity - 1u)) == 0u,
              "plugin scan cache capacity must be a power of two");
static u64 g_pluginScanCache[kPluginScanCacheCapacity]{};
static constexpr u64 kPluginScanStateMask = 0x3ull;
static constexpr u64 kPluginScanPointerMask = ~kPluginScanStateMask;

static u32 hash_pointer(uptr pointer)
{
    u64 value = static_cast<u64>(pointer >> 2u);
    value ^= value >> 33u;
    value *= 0xFF51AFD7ED558CCDull;
    value ^= value >> 33u;
    return static_cast<u32>(value ^ (value >> 32u));
}

enum class PluginScanAction : u32
{
    None = 0u,
    Scan = 1u,
    AlreadyComplete = 2u,
    Busy = 3u,
    Failed = 4u
};

static PluginScanAction claim_plugin_scan(const u8* tesFile, u32& slotOut)
{
    slotOut = 0xFFFFFFFFu;
    if (!tesFile) return PluginScanAction::None;
    const uptr rawPointer = reinterpret_cast<uptr>(tesFile);
    if ((rawPointer & static_cast<uptr>(kPluginScanStateMask)) != 0u) return PluginScanAction::None;
    const u64 pointer = static_cast<u64>(rawPointer) & kPluginScanPointerMask;
    if (pointer == 0u) return PluginScanAction::None;
    const u32 start = hash_pointer(reinterpret_cast<uptr>(tesFile)) & (kPluginScanCacheCapacity - 1u);
    for (u32 probe = 0u; probe < kPluginScanCacheCapacity; ++probe) {
        const u32 index = (start + probe) & (kPluginScanCacheCapacity - 1u);
        u64 current = __atomic_load_n(&g_pluginScanCache[index], __ATOMIC_ACQUIRE);
        if ((current & kPluginScanPointerMask) == pointer) {
            slotOut = index;
            const u32 state = static_cast<u32>(current & kPluginScanStateMask);
            if (state == 2u) return PluginScanAction::AlreadyComplete;
            if (state == 3u) return PluginScanAction::Failed;
            return PluginScanAction::Busy;
        }
        if (current == 0u) {
            const u64 desired = pointer | 1u;
            u64 expected = 0u;
            if (__atomic_compare_exchange_n(&g_pluginScanCache[index], &expected, desired,
                                            false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                slotOut = index;
                return PluginScanAction::Scan;
            }
            if ((expected & kPluginScanPointerMask) == pointer) {
                slotOut = index;
                return PluginScanAction::Busy;
            }
        }
    }
    return PluginScanAction::None;
}

static void finish_plugin_scan(u32 slot, const u8* tesFile, bool success)
{
    if (slot >= kPluginScanCacheCapacity || !tesFile) return;
    const u64 pointer = static_cast<u64>(reinterpret_cast<uptr>(tesFile)) & kPluginScanPointerMask;
    __atomic_store_n(&g_pluginScanCache[slot], pointer | (success ? 2ull : 3ull), __ATOMIC_RELEASE);
}

static const u8* get_defining_tes_file(const u8* ligh)
{
    if (!ligh) return nullptr;
    const auto* sourceArray = *reinterpret_cast<u8* const*>(ligh + kTESFormSourceFilesOffset);
    if (!sourceArray) return nullptr;
    const auto* data = *reinterpret_cast<u8* const*>(sourceArray + kTESFileArrayDataOffset);
    const u32 count = read_u32(sourceArray + kTESFileArraySizeOffset);
    if (!data || count == 0u || count > 256u) return nullptr;
    return *reinterpret_cast<u8* const*>(data); // sourceFiles[0] = defining/owner file
}

static u32 classify_runtime_ligh(void* lighVoid)
{
    if (!lighVoid || !any_policy_scan_enabled()) return kPolicyClassified;

    const auto* ligh = static_cast<const u8*>(lighVoid);
    const u32 formID = read_u32(ligh + kTESFormFormIDOffset);
    if (formID == 0u || formID >= 0xFF000000u) {
        // Dynamic/runtime-only forms do not have a stable plugin EDID to scan.
        // Fail safe to the ordinary-light policy.
        return kPolicyClassified;
    }

    u32 policy = find_cached_runtime_policy(formID);
    if (policy != 0u) return policy;

    const u8* tesFile = get_defining_tes_file(ligh);
    if (!tesFile) return kPolicyClassified;

    u32 slot = 0xFFFFFFFFu;
    const PluginScanAction action = claim_plugin_scan(tesFile, slot);
    if (action == PluginScanAction::Scan) {
        const bool success = scan_owner_plugin(tesFile);
        finish_plugin_scan(slot, tesFile, success);

        policy = find_cached_runtime_policy(formID);
        if (policy == 0u) {
            // A successful owner scan with no special match is a definitive
            // ordinary-light classification. A failed scan also fails safe.
            cache_runtime_policy(formID, kPolicyClassified);
            policy = kPolicyClassified;
        }
    } else if (action == PluginScanAction::AlreadyComplete || action == PluginScanAction::Failed) {
        cache_runtime_policy(formID, kPolicyClassified);
        policy = kPolicyClassified;
    } else {
        // Another thread may currently be scanning the same plugin. Do not cache
        // an ordinary result prematurely; just fail safe for this creation.
        policy = kPolicyClassified;
    }

    return policy;
}

static u32 runtime_policy_from_ni_light(const void* niLight)
{
    if (!niLight) return 0u;
    const auto* runtime = static_cast<const u8*>(niLight) + kNiLightRuntimeDataOffsetSE1597;
    const u32 formID = read_u32(runtime + kRuntimeFormIdOffset);
    return formID != 0u ? find_cached_runtime_policy(formID) : 0u;
}

struct PendingLightPolicy
{
    const void* lightData = nullptr;
    u32 policy = 0u;
    bool lifecycleReady = false;
    bool effectsReasserted = false;
};

// Open Shaders' clustered engine-light path constructs one LightData, calls
// ProcessLight on that exact object, fills its position, then passes the same
// LightData address to vector<LightData>::push_back. Keep only the immediately
// active association for policies that alter final LightData attenuation.
// Renderer-generated particle lights never receive this TESObjectLIGH tag.
static PendingLightPolicy g_pendingLightPolicy{};

static void clear_pending_after_transition_on_renderer_thread()
{
    if (__atomic_exchange_n(&g_clearPendingAfterTransition, 0l, __ATOMIC_ACQ_REL) != 0) {
        g_pendingLightPolicy = {};
    }
}

// CreatePointLight gives us the defining TESObjectLIGH before the renderer builds
// the final LightData. Pre-scan/cache its defining plugin here so later
// ProcessLight classification can resolve the runtime FormID without any
// per-frame string work.

static bool bytes_equal(const u8* a, const u8* b, u32 count)
{
    if (!a || !b) return false;
    for (u32 i = 0u; i < count; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static void copy_bytes(u8* destination, const u8* source, u32 count)
{
    for (u32 i = 0u; i < count; ++i) {
        destination[i] = source[i];
    }
}

static void fill_bytes(u8* destination, u8 value, u32 count)
{
    for (u32 i = 0u; i < count; ++i) {
        destination[i] = value;
    }
}

struct ModuleInfo {
    u32 timeDateStamp;
    u8* imageBase;
    u32 imageSize;
    u8* textBase;
    u32 textSize;
    u8* rdataBase;
    u32 rdataSize;
};

static bool section_name_is_text(const u8* section)
{
    static constexpr char name[5] = {'.','t','e','x','t'};
    for (u32 i = 0u; i < 5u; ++i) {
        if (section[i] != static_cast<u8>(name[i])) return false;
    }
    return section[5] == 0u || section[5] == '$';
}

static bool section_name_is_rdata(const u8* section)
{
    static constexpr char name[6] = {'.','r','d','a','t','a'};
    for (u32 i = 0u; i < 6u; ++i) {
        if (section[i] != static_cast<u8>(name[i])) return false;
    }
    return section[6] == 0u || section[6] == '$';
}

static bool get_module_info(HMODULE module, ModuleInfo& info)
{
    if (!module) return false;
    u8* base = static_cast<u8*>(module);
    if (read_u16(base) != 0x5A4Du) return false;

    const u32 peOffset = read_u32(base + 0x3Cu);
    if (peOffset < 0x40u || peOffset > 0x1000u) return false;
    u8* pe = base + peOffset;
    if (read_u32(pe) != 0x00004550u) return false;

    const u16 sectionCount = read_u16(pe + 6u);
    const u16 optionalSize = read_u16(pe + 20u);
    if (sectionCount == 0u || sectionCount > 96u || optionalSize < 64u) return false;

    u8* optional = pe + 24u;
    if (read_u16(optional) != 0x020Bu) return false; // PE32+

    const u32 sizeOfImage = read_u32(optional + 56u);
    if (sizeOfImage < 0x1000u || sizeOfImage > 0x40000000u) return false;

    u8* sections = optional + optionalSize;
    const uptr sectionOffset = reinterpret_cast<uptr>(sections) - reinterpret_cast<uptr>(base);
    const u64 sectionBytes = static_cast<u64>(sectionCount) * 40u;
    if (sectionOffset >= sizeOfImage || sectionBytes > static_cast<u64>(sizeOfImage - sectionOffset)) {
        return false;
    }

    u8* textBase = nullptr;
    u32 textSize = 0u;
    u8* rdataBase = nullptr;
    u32 rdataSize = 0u;
    for (u32 i = 0u; i < static_cast<u32>(sectionCount); ++i) {
        u8* section = sections + i * 40u;
        const bool isText = section_name_is_text(section);
        const bool isRdata = section_name_is_rdata(section);
        if (!isText && !isRdata) continue;
        const u32 virtualSize = read_u32(section + 8u);
        const u32 virtualAddress = read_u32(section + 12u);
        const u32 rawSize = read_u32(section + 16u);
        const u32 size = virtualSize != 0u ? virtualSize : rawSize;
        if (size == 0u || virtualAddress >= sizeOfImage || size > sizeOfImage - virtualAddress) {
            return false;
        }
        if (isText && !textBase) {
            textBase = base + virtualAddress;
            textSize = size;
        }
        if (isRdata && !rdataBase) {
            rdataBase = base + virtualAddress;
            rdataSize = size;
        }
    }
    if (!textBase || textSize < 0x1000u) return false;

    info.timeDateStamp = read_u32(pe + 8u);
    info.imageBase = base;
    info.imageSize = sizeOfImage;
    info.textBase = textBase;
    info.textSize = textSize;
    info.rdataBase = rdataBase;
    info.rdataSize = rdataSize;
    return true;
}

static bool pattern_matches(const u8* p, const u8* pattern, const char* mask, u32 length)
{
    if (!p || !pattern || !mask) return false;
    for (u32 i = 0u; i < length; ++i) {
        if (mask[i] == 'x' && p[i] != pattern[i]) return false;
        if (mask[i] != 'x' && mask[i] != '?') return false;
    }
    return true;
}

static u8* find_unique_pattern(const ModuleInfo& info,
                               const u8* pattern, const char* mask, u32 length,
                               u32& matchCount)
{
    matchCount = 0u;
    u8* match = nullptr;
    if (!info.textBase || length == 0u || length > info.textSize) return nullptr;

    const u32 last = info.textSize - length;
    for (u32 i = 0u; i <= last; ++i) {
        u8* candidate = info.textBase + i;
        if (pattern_matches(candidate, pattern, mask, length)) {
            ++matchCount;
            match = candidate;
            if (matchCount > 1u) return nullptr;
        }
    }
    return matchCount == 1u ? match : nullptr;
}

static u8* decode_rel32_call_target(u8* callsite)
{
    if (!callsite || callsite[0] != 0xE8u) return nullptr;
    const i32 displacement = static_cast<i32>(read_u32(callsite + 1u));
    const i64 next = static_cast<i64>(reinterpret_cast<uptr>(callsite + 5u));
    const i64 target = next + static_cast<i64>(displacement);
    if (target <= 0) return nullptr;
    return reinterpret_cast<u8*>(static_cast<uptr>(target));
}

static u32 count_direct_calls_to(const ModuleInfo& info, const u8* target)
{
    if (!info.textBase || !target || info.textSize < 5u) return 0u;
    u32 count = 0u;
    for (u32 i = 0u; i + 5u <= info.textSize; ++i) {
        u8* callsite = info.textBase + i;
        if (callsite[0] == 0xE8u && decode_rel32_call_target(callsite) == target) {
            ++count;
        }
    }
    return count;
}

static bool rel32_displacement(const u8* sourceAfterInstruction, const u8* target, i32& out);

static const u8* find_unique_ascii_literal(const ModuleInfo& info, const char* text, u32& matchCount)
{
    matchCount = 0u;
    if (!info.rdataBase || !text || info.rdataSize == 0u) return nullptr;
    const u32 len = string_length(text);
    if (len == 0u || len + 1u > info.rdataSize) return nullptr;

    const u8* match = nullptr;
    for (u32 i = 0u; i + len + 1u <= info.rdataSize; ++i) {
        const u8* candidate = info.rdataBase + i;
        bool same = candidate[len] == 0u;
        for (u32 j = 0u; same && j < len; ++j) {
            if (candidate[j] != static_cast<u8>(text[j])) same = false;
        }
        if (!same) continue;
        ++matchCount;
        match = candidate;
        if (matchCount > 1u) return nullptr;
    }
    return matchCount == 1u ? match : nullptr;
}

static bool text_address_in_module(const ModuleInfo& info, const u8* address)
{
    if (!info.textBase || !address || info.textSize == 0u) return false;
    const uptr begin = reinterpret_cast<uptr>(info.textBase);
    const uptr end = begin + static_cast<uptr>(info.textSize);
    const uptr value = reinterpret_cast<uptr>(address);
    return value >= begin && value < end;
}

// Effects11's current-family OverridePointLightColor performs three setting
// lookups using the unique PointLighting* string literals. Depending on MSVC
// optimization those literals can be addressed by MOVUPS/MOVZX/MOVSD rather
// than LEA, so do not assume a specific opcode. A RIP-relative disp32 is the
// final four bytes of all of those loads; requiring all three unique literals
// makes this semantic check highly specific without relying on a prologue.
static bool function_references_relative_literal(const ModuleInfo& info, const u8* function,
                                                 const u8* literal, u32 maxBytes)
{
    if (!text_address_in_module(info, function) || !literal || maxBytes < 4u) return false;
    const uptr textEnd = reinterpret_cast<uptr>(info.textBase) + info.textSize;
    const uptr functionAddress = reinterpret_cast<uptr>(function);
    u32 available = static_cast<u32>(textEnd - functionAddress);
    if (available > maxBytes) available = maxBytes;
    if (available < 4u) return false;

    const i64 wanted = static_cast<i64>(reinterpret_cast<uptr>(literal));
    for (u32 i = 0u; i + 4u <= available; ++i) {
        const i32 displacement = static_cast<i32>(read_u32(function + i));
        const i64 next = static_cast<i64>(reinterpret_cast<uptr>(function + i + 4u));
        if (next + static_cast<i64>(displacement) == wanted) return true;
    }
    return false;
}

struct NearbyCallCandidate
{
    u8* target = nullptr;
    u32 processNeighborhoods = 0u;
};

// Resolve only the Effects11 calls that are structurally tied to
// InverseSquareLighting::ProcessLight. This deliberately avoids redirecting
// Effects11 calls made by particle lights or unrelated systems. In the current
// Open Shaders family the strict and clustered engine-light builders call
// ProcessLight and, when Effects11 is enabled, OverridePointLightColor within
// the next small block of code.
static u8* resolve_effects11_point_light_color_target(const ModuleInfo& info,
                                                       const u8* processTarget,
                                                       u8** outCallsites, u32 capacity,
                                                       u32& callsiteCount)
{
    callsiteCount = 0u;
    if (!g_fireColorAuthoritative || !g_fireColorEnabled || !processTarget ||
        !outCallsites || capacity == 0u) {
        return nullptr;
    }

    u32 curveCount = 0u;
    u32 desatCount = 0u;
    u32 intensityCount = 0u;
    const u8* curve = find_unique_ascii_literal(info, "PointLightingCurve", curveCount);
    const u8* desat = find_unique_ascii_literal(info, "PointLightingDesaturation", desatCount);
    const u8* intensity = find_unique_ascii_literal(info, "PointLightingIntensity", intensityCount);
    if (!curve || !desat || !intensity || curveCount != 1u || desatCount != 1u || intensityCount != 1u) {
        return nullptr;
    }

    static constexpr u32 kNeighborhoodBytes = 0x200u;
    static constexpr u32 kMaxCandidates = 32u;
    NearbyCallCandidate candidates[kMaxCandidates] = {};
    u32 candidateCount = 0u;

    // First pass: collect call targets that recur shortly after distinct
    // ProcessLight callsites. Count at most once per target per neighborhood.
    for (u32 i = 0u; i + 5u <= info.textSize; ++i) {
        u8* processCall = info.textBase + i;
        if (processCall[0] != 0xE8u || decode_rel32_call_target(processCall) != processTarget) continue;

        u8* seen[kMaxCandidates] = {};
        u32 seenCount = 0u;
        const u32 begin = i + 5u;
        u32 end = begin + kNeighborhoodBytes;
        if (end > info.textSize) end = info.textSize;
        for (u32 j = begin; j + 5u <= end; ++j) {
            u8* callsite = info.textBase + j;
            if (callsite[0] != 0xE8u) continue;
            u8* target = decode_rel32_call_target(callsite);
            if (!text_address_in_module(info, target) || target == processTarget) continue;

            bool alreadySeen = false;
            for (u32 k = 0u; k < seenCount; ++k) {
                if (seen[k] == target) {
                    alreadySeen = true;
                    break;
                }
            }
            if (alreadySeen) continue;
            if (seenCount < kMaxCandidates) seen[seenCount++] = target;

            u32 slot = candidateCount;
            for (u32 k = 0u; k < candidateCount; ++k) {
                if (candidates[k].target == target) {
                    slot = k;
                    break;
                }
            }
            if (slot == candidateCount) {
                if (candidateCount >= kMaxCandidates) continue;
                candidates[candidateCount].target = target;
                candidates[candidateCount].processNeighborhoods = 0u;
                ++candidateCount;
            }
            ++candidates[slot].processNeighborhoods;
        }
    }

    u8* resolvedTarget = nullptr;
    for (u32 i = 0u; i < candidateCount; ++i) {
        u8* target = candidates[i].target;
        // Requiring recurrence in at least two independent ProcessLight blocks
        // prevents an incidental nearby helper call from becoming authoritative.
        if (!target || candidates[i].processNeighborhoods < 2u) continue;
        if (!function_references_relative_literal(info, target, curve, 0x600u) ||
            !function_references_relative_literal(info, target, desat, 0x600u) ||
            !function_references_relative_literal(info, target, intensity, 0x600u)) {
            continue;
        }
        if (resolvedTarget && resolvedTarget != target) return nullptr;
        resolvedTarget = target;
    }
    if (!resolvedTarget) return nullptr;

    // Second pass: return only the matching callsites in ProcessLight
    // neighborhoods, not every use of OverridePointLightColor in the module.
    for (u32 i = 0u; i + 5u <= info.textSize; ++i) {
        u8* processCall = info.textBase + i;
        if (processCall[0] != 0xE8u || decode_rel32_call_target(processCall) != processTarget) continue;
        const u32 begin = i + 5u;
        u32 end = begin + kNeighborhoodBytes;
        if (end > info.textSize) end = info.textSize;
        for (u32 j = begin; j + 5u <= end; ++j) {
            u8* callsite = info.textBase + j;
            if (callsite[0] != 0xE8u || decode_rel32_call_target(callsite) != resolvedTarget) continue;

            bool duplicate = false;
            for (u32 k = 0u; k < callsiteCount; ++k) {
                if (outCallsites[k] == callsite) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            if (callsiteCount >= capacity) {
                callsiteCount = 0u;
                return nullptr;
            }
            outCallsites[callsiteCount++] = callsite;
        }
    }
    return callsiteCount >= 2u ? resolvedTarget : nullptr;
}

static bool build_rel32_call(u8 out[5], u8* callsite, u8* relay)
{
    if (!out || !callsite || !relay) return false;
    i32 displacement = 0;
    if (!rel32_displacement(callsite + 5u, relay, displacement)) return false;
    out[0] = 0xE8u;
    write_u32(out + 1u, static_cast<u32>(displacement));
    return true;
}

// -----------------------------------------------------------------------------
// ISL conversion hot path
// -----------------------------------------------------------------------------

static PushLightFn g_originalPushLight = nullptr;
static ProcessLightFn g_originalProcessLight = nullptr;
static CreatePointLightFn g_originalCreatePointLight = nullptr;
static Effects11OverridePointLightColorFn g_originalEffects11OverridePointLightColor = nullptr;
static bool g_effects11AuthorityInstalled = false;
static bool g_hookInstalled = false;
static bool g_permanentFailure = false;

static bool vector_candidate_valid(const LightDataVectorRaw* lights, u32 stride)
{
    if (!lights || stride == 0u) return false;
    const uptr begin = reinterpret_cast<uptr>(lights->begin);
    const uptr end = reinterpret_cast<uptr>(lights->end);
    const uptr capacity = reinterpret_cast<uptr>(lights->capacity);

    // An entirely empty, unallocated vector does not contain enough information.
    if (begin == 0u && end == 0u && capacity == 0u) return false;
    if (begin == 0u || end < begin || capacity < end) return false;

    const uptr usedBytes = end - begin;
    const uptr capacityBytes = capacity - begin;
    if (capacityBytes > 16u * 1024u * 1024u) return false;
    if ((usedBytes % stride) != 0u || (capacityBytes % stride) != 0u) return false;

    const uptr usedCount = usedBytes / stride;
    const uptr capacityCount = capacityBytes / stride;
    return usedCount <= capacityCount && capacityCount <= 65536u;
}

static LightDataABI resolve_vector_abi(const LightDataVectorRaw* lights)
{
    const bool cs80 = vector_candidate_valid(lights, 80u);
    const bool os96 = vector_candidate_valid(lights, 96u);

    if (g_lightDataABI == LightDataABI::CommunityShaders80) {
        return cs80 ? g_lightDataABI : LightDataABI::Unknown;
    }
    if (g_lightDataABI == LightDataABI::OpenShaders96) {
        return os96 ? g_lightDataABI : LightDataABI::Unknown;
    }

    if (cs80 != os96) {
        g_lightDataABI = cs80 ? LightDataABI::CommunityShaders80 : LightDataABI::OpenShaders96;
        return g_lightDataABI;
    }

    if (cs80 && os96) {
        const uptr begin = reinterpret_cast<uptr>(lights->begin);
        const uptr capacity = reinterpret_cast<uptr>(lights->capacity);
        const uptr capacityBytes = capacity - begin;

        // Both strides can divide a few unusual vector sizes. UpdateLights reserves
        // MAX_LIGHTS=1024 in both supported renderer families, which uniquely
        // disambiguates the two layouts without inspecting renderer branding.
        if (capacityBytes == 80u * 1024u) {
            g_lightDataABI = LightDataABI::CommunityShaders80;
            return g_lightDataABI;
        }
        if (capacityBytes == 96u * 1024u) {
            g_lightDataABI = LightDataABI::OpenShaders96;
            return g_lightDataABI;
        }
    }

    return LightDataABI::Unknown;
}

static bool point_light_needs_normalization_raw(const u8* light, LightDataABI abi,
                                                 float effectiveMaxRadius)
{
    const u32 flagsOffset = light_flags_offset(abi);
    if (!light || flagsOffset == 0u) return false;

    const float radius = read_f32(light + kOffsetRadius);
    if (!(radius > 0.001f) || radius > 100000.0f) return false;

    const u32 flags = read_u32(light + flagsOffset);
    if ((flags & kLightFlagInverseSquare) == 0u) return true;
    return effectiveMaxRadius > 0.0f && radius > effectiveMaxRadius;
}

static float capped_fade_zone(float radius)
{
    // Open Shaders: t = saturate((radius - distance) * fadeZone).
    // t remains 1.0 before FadeStartFraction * radius, then smoothsteps to 0
    // at radius. This reshapes only the outer smooth fade; inverse-square
    // attenuation itself still begins at the light source.
    const float fadeSpan = radius * (1.0f - g_fadeStartFraction);
    return fadeSpan > 0.001f ? (1.0f / fadeSpan) : (1.0f / radius);
}

static bool normalize_point_light_raw(u8* light, LightDataABI abi, float effectiveMaxRadius, bool rebuildCappedCore)
{
    const u32 flagsOffset = light_flags_offset(abi);
    if (!light || flagsOffset == 0u) return false;

    const float sourceRadius = read_f32(light + kOffsetRadius);
    if (!(sourceRadius > 0.001f) || sourceRadius > 100000.0f) return false;

    const bool radiusCapped = effectiveMaxRadius > 0.0f && sourceRadius > effectiveMaxRadius;
    const float radius = radiusCapped ? effectiveMaxRadius : sourceRadius;
    const u32 sourceFlags = read_u32(light + flagsOffset);
    const bool alreadyISL = (sourceFlags & kLightFlagInverseSquare) != 0u;

    if (alreadyISL) {
        if (!(radius < sourceRadius)) return false;

        // Preserve authored intensity/source-size and every non-attenuation field.
        // This includes Open Shaders' second-eye position, uint128 room mask,
        // Particle/Spot/Omni flags, Shadow flag, and shadow-map index.
        write_f32(light + kOffsetRadius, radius);
        const float invRadius = 1.0f / radius;
        write_f32(light + kOffsetInvRadius, invRadius);
        const float fadeZone = radiusCapped
            ? capped_fade_zone(radius)
            : (radius < kFadeZoneBase ? invRadius : kInvFadeZoneBase);
        write_f32(light + kOffsetFadeZone, fadeZone);
        if (radiusCapped && rebuildCappedCore) {
            // Open Shaders may already have marked spell lights ISL with a much
            // larger source-size bias. A 40-unit radius paired with that old
            // center size looks almost flat rather than inverse-square. Rebuild
            // the ISL core size from the dedicated magic radius and compensate
            // fade so d=0 intensity stays approximately unchanged:
            //   fade * 3920 / sizeBias = constant.
            const float oldSizeBias = read_f32(light + kOffsetSizeBias);
            const float newSizeBias = radius * radius * kSizeBiasFactor;
            if (oldSizeBias > 0.0001f && oldSizeBias < 100000000.0f) {
                const float oldFade = read_f32(light + kOffsetFade);
                write_f32(light + kOffsetFade, oldFade * (newSizeBias / oldSizeBias));
            }
            write_f32(light + kOffsetSizeBias, newSizeBias);
        }
        return true;
    }

    const float radiusSq = radius * radius;
    const float invRadius = 1.0f / radius;
    const float sourceFade = read_f32(light + kOffsetFade);

    write_f32(light + kOffsetRadius, radius);
    write_f32(light + kOffsetInvRadius, invRadius);
    const float fadeZone = radiusCapped
        ? capped_fade_zone(radius)
        : (radius < kFadeZoneBase ? invRadius : kInvFadeZoneBase);
    write_f32(light + kOffsetFadeZone, fadeZone);
    write_f32(light + kOffsetSizeBias, radiusSq * kSizeBiasFactor);
    write_f32(light + kOffsetFade, sourceFade * radiusSq * kFadeRadiusSqFactor);
    write_u32(light + flagsOffset, sourceFlags | kLightFlagInverseSquare);
    return true;
}

static bool apply_consistent_fire_color(u8* light)
{
    if (!light || !g_fireColorEnabled) return false;

    const float sourceR = read_f32(light + kOffsetColorR);
    const float sourceG = read_f32(light + kOffsetColorG);
    const float sourceB = read_f32(light + kOffsetColorB);
    // NaNs fail these comparisons. Extreme/negative values are treated as
    // unsafe renderer state and left untouched.
    if (!(sourceR >= 0.0f && sourceR < 1000.0f) ||
        !(sourceG >= 0.0f && sourceG < 1000.0f) ||
        !(sourceB >= 0.0f && sourceB < 1000.0f)) {
        return false;
    }

    float scale = 1.0f;
    if (g_fireColorPreserveIntensity) {
        // Open Shaders' own BSLight luminance path uses the arithmetic mean of
        // RGB, so preserve that same scalar while making chromaticity uniform.
        const float sourceMean = (sourceR + sourceG + sourceB) * (1.0f / 3.0f);
        const float targetMean = (g_fireColorRed + g_fireColorGreen + g_fireColorBlue) * (1.0f / 3.0f);
        if (!(sourceMean > 0.000001f) || !(targetMean > 0.000001f)) return false;
        scale = sourceMean / targetMean;
    }

    write_f32(light + kOffsetColorR, g_fireColorRed * scale);
    write_f32(light + kOffsetColorG, g_fireColorGreen * scale);
    write_f32(light + kOffsetColorB, g_fireColorBlue * scale);
    return true;
}

struct FireColorAnchor
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

static constexpr u32 kMaxFireColorAnchors = 256u;
static FireColorAnchor g_fireColorAnchors[kMaxFireColorAnchors]{};
static u32 g_fireColorAnchorCount = 0u;
static const LightDataVectorRaw* g_fireColorAnchorVector = nullptr;

static bool safe_light_position(const u8* light, FireColorAnchor& out)
{
    if (!light) return false;
    const float x = read_f32(light + kOffsetPositionX);
    const float y = read_f32(light + kOffsetPositionY);
    const float z = read_f32(light + kOffsetPositionZ);
    // NaNs fail the ordered comparisons. Camera-relative light positions should
    // never need values remotely this large; fail closed on corrupted layouts.
    if (!(x > -10000000.0f && x < 10000000.0f) ||
        !(y > -10000000.0f && y < 10000000.0f) ||
        !(z > -10000000.0f && z < 10000000.0f)) {
        return false;
    }
    out.x = x;
    out.y = y;
    out.z = z;
    return true;
}

static void begin_fire_color_batch_if_needed(const LightDataVectorRaw* lights)
{
    if (!lights) return;
    // push_back is intercepted before the vector grows. An empty vector is the
    // deterministic start of a fresh Open Shaders light batch. Also fail closed
    // if a different vector appears without an empty first observation.
    if (lights->begin == lights->end || g_fireColorAnchorVector != lights) {
        g_fireColorAnchorVector = lights;
        g_fireColorAnchorCount = 0u;
    }
}

static void remember_fire_color_anchor(const u8* light)
{
    if (!g_fireColorPairParticleLights || g_fireColorParticlePairDistance <= 0.0f ||
        g_fireColorAnchorCount >= kMaxFireColorAnchors) {
        return;
    }
    FireColorAnchor anchor{};
    if (!safe_light_position(light, anchor)) return;
    g_fireColorAnchors[g_fireColorAnchorCount++] = anchor;
}

static bool particle_color_is_warm(const u8* light)
{
    if (!light) return false;
    const float r = read_f32(light + kOffsetColorR);
    const float g = read_f32(light + kOffsetColorG);
    const float b = read_f32(light + kOffsetColorB);
    if (!(r >= 0.0f && r < 1000.0f) ||
        !(g >= 0.0f && g < 1000.0f) ||
        !(b >= 0.0f && b < 1000.0f)) {
        return false;
    }
    if (!g_fireColorParticleRequireWarm) return true;
    // Do not let a nearby classified fireplace recolor blue/cold or neutral
    // particle lights. Genuine flame/ember lights can be yellow through orange,
    // but they should still have substantially less blue than red/green.
    return r >= g * 0.95f && g > b * 1.10f;
}

static bool particle_is_near_fire_anchor(const u8* light)
{
    if (!g_fireColorPairParticleLights || g_fireColorParticlePairDistance <= 0.0f ||
        g_fireColorAnchorCount == 0u || !particle_color_is_warm(light)) {
        return false;
    }
    FireColorAnchor p{};
    if (!safe_light_position(light, p)) return false;
    const float maxDistSq = g_fireColorParticlePairDistance * g_fireColorParticlePairDistance;
    for (u32 i = 0u; i < g_fireColorAnchorCount; ++i) {
        const float dx = p.x - g_fireColorAnchors[i].x;
        const float dy = p.y - g_fireColorAnchors[i].y;
        const float dz = p.z - g_fireColorAnchors[i].z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq <= maxDistSq) return true;
    }
    return false;
}

static float effective_radius_cap_for_policy(u32 policy)
{
    // Explicit local-magic policies take precedence over the environmental
    // bypass. If a record somehow matches multiple magic rules, use the tighter
    // positive cap so it cannot become a room-scale projector.
    float magicCap = 0.0f;
    if (g_magicHandLightsEnabled && (policy & kPolicyMagicHand) != 0u) {
        magicCap = g_magicHandMaxRadius;
    }
    if (g_magicImpactLightsEnabled && (policy & kPolicyMagicImpact) != 0u) {
        if (magicCap <= 0.0f || (g_magicImpactMaxRadius > 0.0f && g_magicImpactMaxRadius < magicCap)) {
            magicCap = g_magicImpactMaxRadius;
        }
    }
    if (magicCap > 0.0f) return magicCap;

    if (g_environmentWhitelistEnabled &&
        (policy & kPolicyEnvironmental) != 0u &&
        (policy & kPolicyForceCapped) == 0u) {
        return 0.0f;
    }
    return g_maxRadius;
}

static void universal_point_light_push_hook(LightDataVectorRaw* lights, const void* sourceVoid)
{
    PushLightFn original = g_originalPushLight;
    if (!original) return;
    if (!lights || !sourceVoid) return;

    // Exact no-light owners are dropped before every other ISL/fire/radius
    // operation, including during lifecycle suspension. This is just the
    // already-associated in-flight payload from ProcessLight; no lookup/scan.
    if (sourceVoid == g_pendingLightPolicy.lightData &&
        (g_pendingLightPolicy.policy & kPolicySuppressLight) != 0u) {
        g_pendingLightPolicy = {};
        return;
    }

    if (processing_suspended()) {
        // During save/load and the short post-transition settle window, Global
        // ISL is a strict observer: never manufacture a converted LightData.
        original(lights, sourceVoid);
        return;
    }
    clear_pending_after_transition_on_renderer_thread();

    const LightDataABI abi = resolve_vector_abi(lights);
    if (abi == LightDataABI::Unknown) {
        original(lights, sourceVoid);
        return;
    }

    const auto* source = static_cast<const u8*>(sourceVoid);
    begin_fire_color_batch_if_needed(lights);
    const bool hasPendingPolicy = sourceVoid == g_pendingLightPolicy.lightData;
    if (hasPendingPolicy && !g_pendingLightPolicy.lifecycleReady) {
        // This engine-backed NiLight has not yet survived the main-thread
        // admission handoff and settle delay. Leave Open Shaders' freshly
        // produced LightData completely untouched for this pass.
        original(lights, sourceVoid);
        g_pendingLightPolicy = {};
        return;
    }

    const u32 policy = hasPendingPolicy ? g_pendingLightPolicy.policy : 0u;
    const float effectiveMaxRadius = effective_radius_cap_for_policy(policy);

    const u32 flagsOffset = light_flags_offset(abi);
    const bool suppressHandShadow =
        abi == LightDataABI::OpenShaders96 &&
        g_magicHandLightsEnabled && !g_magicHandCastShadows &&
        (policy & kPolicyMagicHand) != 0u && flagsOffset != 0u &&
        (read_u32(source + flagsOffset) & kLightFlagShadow) != 0u;
    const bool needsNormalization = point_light_needs_normalization_raw(source, abi, effectiveMaxRadius);
    const bool applyEngineFireColor = g_fireColorEnabled && (policy & kPolicyFireColor) != 0u;
    const bool generatedSimpleLight = !hasPendingPolicy && flagsOffset != 0u &&
        (read_u32(source + flagsOffset) & kLightFlagSimple) != 0u;
    const bool applyPairedParticleFireColor = g_fireColorEnabled && generatedSimpleLight &&
        particle_is_near_fire_anchor(source);
    const bool applyFireColor = applyEngineFireColor || applyPairedParticleFireColor;

    if (!needsNormalization && !suppressHandShadow && !applyFireColor) {
        original(lights, sourceVoid);
        if (hasPendingPolicy) g_pendingLightPolicy = {};
        return;
    }

    alignas(16) u8 converted[96] = {};
    const u32 size = light_data_size(abi);
    copy_bytes(converted, source, size);
    if (needsNormalization) {
        const bool rebuildCappedCore = (policy & (kPolicyMagicHand | kPolicyMagicImpact)) != 0u;
        normalize_point_light_raw(converted, abi, effectiveMaxRadius, rebuildCappedCore);
    }
    if (suppressHandShadow) {
        // Hand lights keep their illumination, ISL attenuation, local radius,
        // and every other Open Shaders flag/field. Only the transient Shadow
        // bit is removed so finger/hand motion cannot project a real shadow.
        // Open Shaders still owns promotion, scheduling, budgets and teardown.
        const u32 flags = read_u32(converted + flagsOffset);
        write_u32(converted + flagsOffset, flags & ~kLightFlagShadow);
    }
    if (applyFireColor) {
        // Final-stage override: engine fire lights are classified from their
        // TESObjectLIGH provenance. Open Shaders generated Simple particle lights
        // can inherit the same chromaticity only when spatially paired to one of
        // those classified fire lights in this exact current light batch.
        apply_consistent_fire_color(converted);
    }
    if (applyEngineFireColor) {
        // Record the already-built camera-relative position only for this batch.
        // Particle lights are appended after engine lights by Open Shaders, so
        // this anchor is available without retaining any NiLight/BSLight pointer.
        remember_fire_color_anchor(converted);
    }
    original(lights, converted);
    if (hasPendingPolicy) g_pendingLightPolicy = {};
}

static void universal_strict_process_light_hook(void* isl, void* lightVoid,
                                                void* bsLight, void* niLight)
{
    ProcessLightFn original = g_originalProcessLight;
    if (!original) return;

    // Existing-instance fallback for exact no-light owners. This runs only when
    // Open Shaders is already processing a light; it adds no scan. Do not call
    // ProcessLight for the offender (so it is not converted). Zero/disable the
    // in-flight payload, tag it so Effects11 cannot repaint it and so the
    // clustered push hook drops it, then hide/disable the NiLight itself so
    // Open Shaders rejects it on subsequent passes.
    if (is_no_light_ni_light_owner(niLight)) {
        g_pendingLightPolicy = {};
        zero_no_light_payload(lightVoid);
        g_pendingLightPolicy.lightData = lightVoid;
        g_pendingLightPolicy.policy = kPolicySuppressLight;
        g_pendingLightPolicy.lifecycleReady = true;
        // Preserve the established save/load hardening: transient LightData can
        // be suppressed safely while suspended, but defer mutation of the live
        // NiLight object until the renderer is outside that lifecycle window.
        if (!processing_suspended()) {
            hard_disable_existing_ni_light(niLight);
        }
        return;
    }

    if (processing_suspended()) {
        original(isl, lightVoid, bsLight, niLight);
        return;
    }
    clear_pending_after_transition_on_renderer_thread();

    // A new ProcessLight call deterministically supersedes an unmatched
    // association from the previous call.
    g_pendingLightPolicy = {};

    // Open Shaders computes its LightData first. We do not touch the Skyrim
    // scene object here. The safety layer only observes a stable FormID plus
    // opaque current-instance addresses, then requires a later fresh call after
    // a main-thread admission handoff before final LightData mutation is allowed.
    original(isl, lightVoid, bsLight, niLight);
    // Fire chromaticity lives in the first 12 bytes of every supported LightData
    // layout, so this authority stage does not need to wait for the vector-push
    // ABI detector. This is important for strict lights that can be rendered
    // before the first clustered-light vector is submitted after a cell load.
    if (!lightVoid) return;

    u32 formID = 0u;
    if (niLight) {
        const auto* runtime = static_cast<const u8*>(niLight) + kNiLightRuntimeDataOffsetSE1597;
        formID = read_u32(runtime + kRuntimeFormIdOffset);
    }

    const bool lifecycleReady = runtime_instance_admitted(formID, niLight, bsLight);
    u32 policy = lifecycleReady ? runtime_policy_from_ni_light(niLight) : 0u;
    if (lifecycleReady && g_fireColorEnabled && g_fireColorPairDetachedBulbs &&
        detached_fire_instance_is_marked(formID, niLight)) {
        policy |= kPolicyClassified | kPolicyFireColor;
    }

    // Authoritative fire stage #1: ProcessLight is shared by Open Shaders'
    // clustered, shadow-clustered, and strict-light builders. Reasserting the
    // chromaticity here closes the strict-path hole that never reaches the
    // vector push hook. Effects11 may run immediately after this call, so a
    // second identity-checked reassertion is installed at its callsites below.
    if (g_fireColorAuthoritative && lifecycleReady && lightVoid &&
        g_fireColorEnabled && (policy & kPolicyFireColor) != 0u) {
        apply_consistent_fire_color(static_cast<u8*>(lightVoid));
    }

    // Associate every engine-backed ProcessLight payload, including ordinary
    // lights. This lets the final push hook pass through incomplete instances
    // without changing the existing policy/radius behavior once admitted.
    g_pendingLightPolicy.lightData = lightVoid;
    g_pendingLightPolicy.policy = policy;
    g_pendingLightPolicy.lifecycleReady = lifecycleReady;
}

static void universal_effects11_point_light_color_hook(void* effects11, void* colorVoid)
{
    Effects11OverridePointLightColorFn original = g_originalEffects11OverridePointLightColor;
    if (!original) return;

    // A no-light payload is already zeroed and belongs to an exact suppressed
    // owner. Do not let Effects11 manufacture any contribution from it.
    if (colorVoid && colorVoid == g_pendingLightPolicy.lightData &&
        (g_pendingLightPolicy.policy & kPolicySuppressLight) != 0u) {
        return;
    }

    // Preserve Open Shaders/Effects11 intensity, curve and desaturation work
    // first, then make fire chromaticity authoritative. Pointer identity is
    // exact: LightData::color begins at offset 0, so the float3 reference passed
    // by Effects11 equals the LightData address tagged by ProcessLight.
    original(effects11, colorVoid);

    if (!g_fireColorAuthoritative || !g_fireColorEnabled || !colorVoid ||
        processing_suspended()) {
        return;
    }
    if (colorVoid != g_pendingLightPolicy.lightData ||
        !g_pendingLightPolicy.lifecycleReady ||
        g_pendingLightPolicy.effectsReasserted ||
        (g_pendingLightPolicy.policy & kPolicyFireColor) == 0u) {
        return;
    }

    if (apply_consistent_fire_color(static_cast<u8*>(colorVoid))) {
        g_pendingLightPolicy.effectsReasserted = true;
    }
}

static void* universal_create_point_light_hook(void* ligh, void* refr, void* root,
                                                bool forceDynamic, bool useLightRadius,
                                                bool affectRequesterOnly)
{
    CreatePointLightFn original = g_originalCreatePointLight;
    if (!original) return nullptr;

    // Exact no-light owners are vetoed at the same CreatePointLight choke point
    // already used by the ISL path. No NiPointLight is created, converted,
    // shadow-promoted, budgeted or submitted. No world scan is involved.
    if (is_no_light_point_light_owner(refr, root)) {
        return nullptr;
    }

    // Classification is metadata-only and must not be gated by the renderer
    // lifecycle suspension. CreatePointLight commonly runs while a cell/load
    // transition is still inside the post-load settle window. If we skipped the
    // only TESObjectLIGH observation here, that light could remain ordinary for
    // its entire lifetime even though later LightData mutation is safely gated.
    u32 baseFormID = 0u;
    if (ligh) {
        classify_runtime_ligh(ligh);
        baseFormID = read_u32(static_cast<const u8*>(ligh) + kTESFormFormIDOffset);
    }

    const bool suspended = processing_suspended();
    bool canAssociateDetached = false;
    u32 refFormID = 0u;
    if (!suspended) {
        clear_pending_after_transition_on_renderer_thread();
        if (g_fireColorEnabled && g_fireColorPairDetachedBulbs && refr) {
            refFormID = read_u32(static_cast<const u8*>(refr) + kTESFormFormIDOffset);
            canAssociateDetached =
                baseFormID != 0u && baseFormID < 0xFF000000u &&
                refFormID != 0u && refFormID < 0xFF000000u;
        }
    }

    const u32 dynamicOwnerFormID = forceDynamic ? owner_form_id_for_dynamic_light(refr, root) : 0u;
    const u64 dynamicStreamKey = (forceDynamic && baseFormID != 0u)
        ? note_dynamic_stream_event(baseFormID, dynamicOwnerFormID) : 0u;
    ActiveDynamicCreationContext* activeDynamic = push_active_dynamic_context(dynamicStreamKey);
    void* created = original(ligh, refr, root, forceDynamic, useLightRadius, affectRequesterOnly);
    pop_active_dynamic_context(activeDynamic);
    if (created && dynamicStreamKey != 0u) {
        // Keep a short-lived pointer->stream association for re-adds that occur
        // after CreatePointLight returns. The in-flight context above covers the
        // common nested AddLight path before the NiLight pointer is returned.
        remember_dynamic_light_instance(created, dynamicStreamKey);
    }
    if (created && canAssociateDetached) {
        cache_detached_fire_instance(baseFormID, created, is_detached_fire_ref(refFormID));
    }
    return created;
}

// -----------------------------------------------------------------------------
// Startup-only signature resolution / transactional function-entry hooks
// -----------------------------------------------------------------------------

static void build_absolute_jump14(u8 out[14], const void* target)
{
    out[0] = 0xFFu;
    out[1] = 0x25u;
    out[2] = 0x00u;
    out[3] = 0x00u;
    out[4] = 0x00u;
    out[5] = 0x00u;
    write_u64(out + 6u, reinterpret_cast<u64>(target));
}

static bool rel32_displacement(const u8* sourceAfterInstruction, const u8* target, i32& out)
{
    const i64 delta = static_cast<i64>(reinterpret_cast<uptr>(target)) -
                      static_cast<i64>(reinterpret_cast<uptr>(sourceAfterInstruction));
    if (delta < -2147483648LL || delta > 2147483647LL) return false;
    out = static_cast<i32>(delta);
    return true;
}

static uptr align_down_64k(uptr value)
{
    return value & ~static_cast<uptr>(0xFFFFu);
}

static void* allocate_near_relay_page(const u8* firstTarget, const u8* secondTarget,
                                      const u8* thirdTarget)
{
    static constexpr SIZE_T kPageSize = 0x1000u;
    static constexpr uptr kAllocationGranularity = 0x10000u;
    static constexpr uptr kSearchStep = 0x100000u;
    static constexpr uptr kSearchLimit = 0x70000000ull;

    if (!firstTarget) return nullptr;
    const u8* targets[3] = { firstTarget, secondTarget, thirdTarget };
    uptr minAddress = reinterpret_cast<uptr>(firstTarget);
    uptr maxAddress = minAddress;
    for (u32 i = 1u; i < 3u; ++i) {
        if (!targets[i]) continue;
        const uptr address = reinterpret_cast<uptr>(targets[i]);
        if (address < minAddress) minAddress = address;
        if (address > maxAddress) maxAddress = address;
    }
    const uptr center = align_down_64k(minAddress + (maxAddress - minAddress) / 2u);

    for (uptr distance = kAllocationGranularity;
         distance <= kSearchLimit;
         distance += kSearchStep) {
        uptr candidates[2] = {};
        u32 candidateCount = 0u;
        if (center <= ~static_cast<uptr>(0) - distance) candidates[candidateCount++] = center + distance;
        if (center > distance) candidates[candidateCount++] = center - distance;

        for (u32 i = 0u; i < candidateCount; ++i) {
            void* page = VirtualAlloc(reinterpret_cast<void*>(candidates[i]), kPageSize,
                                      MEM_RESERVE_ | MEM_COMMIT_, PAGE_EXECUTE_READWRITE_);
            if (!page) continue;

            const u8* relay = static_cast<const u8*>(page);
            bool allReachable = true;
            for (u32 targetIndex = 0u; targetIndex < 3u; ++targetIndex) {
                if (!targets[targetIndex]) continue;
                i32 unused = 0;
                if (!rel32_displacement(targets[targetIndex] + 5u,
                                        relay + targetIndex * 0x10u, unused)) {
                    allReachable = false;
                    break;
                }
            }
            if (allReachable) return page;
            VirtualFree(page, 0u, MEM_RELEASE_);
        }
    }
    return nullptr;
}

static bool write_code_bytes(u8* destination, const u8* bytes, u32 count)
{
    if (!destination || !bytes || count == 0u) return false;
    DWORD oldProtect = 0u;
    if (!VirtualProtect(destination, count, PAGE_EXECUTE_READWRITE_, &oldProtect)) {
        return false;
    }

    copy_bytes(destination, bytes, count);
    FlushInstructionCache(GetCurrentProcess(), destination, count);

    DWORD ignored = 0u;
    if (!VirtualProtect(destination, count, oldProtect, &ignored)) {
        // Code itself is valid; leaving this tiny region writable is safer than
        // attempting an unprotected rollback that could corrupt instructions.
        return bytes_equal(destination, bytes, count);
    }
    return bytes_equal(destination, bytes, count);
}

static bool shadow_gate_register_contract_ok(const u8* gate, const ModuleInfo& info)
{
    if (!gate || !info.textBase || info.textSize < 96u || gate < info.textBase + 96u || gate >= info.textBase + info.textSize) return false;
    static constexpr u8 kLoadLightIntoRsi[] = { 0x48,0x8B,0xB1,0x88,0x00,0x00,0x00 };
    static constexpr u8 kLoadParamsIntoRdi[] = { 0x48,0x8B,0xB9,0xB8,0x00,0x00,0x00 };
    bool sawRsi = false;
    bool sawRdi = false;
    const u8* begin = gate - 96u;
    for (const u8* cur = begin; cur + 7u <= gate; ++cur) {
        if (!sawRsi && bytes_equal(cur, kLoadLightIntoRsi, 7u)) sawRsi = true;
        if (!sawRdi && bytes_equal(cur, kLoadParamsIntoRdi, 7u)) sawRdi = true;
    }
    return sawRsi && sawRdi;
}

static bool apply_shadow_bias_cap_to_params(void* rawParams)
{
    if (!g_shadowBiasCorrectionEnabled || !rawParams) return false;

    // Skyrim/CommonLibSE LIGHT_CREATE_PARAMS::depthBias is +0x14. Open Shaders
    // mutates the same transient creation structure in Hook_ConvertLights_Add.
    // We intentionally touch only this float and never the source plugin record.
    auto* params = static_cast<u8*>(rawParams);
    const float oldBias = read_f32(params + 0x14u);
    if (!finite_f32(oldBias) || oldBias <= g_shadowBiasMaximum) return false;
    write_f32(params + 0x14u, g_shadowBiasMaximum);
    return true;
}

static bool process_shadow_add_policy(const void* niLight, void* rawParams)
{
    // Bias correction is independent of ISL conversion/promotion success. In
    // particular, native/already-shadow-casting spotlights still pass here.
    apply_shadow_bias_cap_to_params(rawParams);

    // Preserve the user's Open Shaders native PromoteNormalToShadow setting
    // whenever Global ISL's force option is disabled. The installer resolves
    // this byte from the exact RIP-relative CMP that the relay replaces.
    if (!g_forceShadowPromotion) {
        const volatile u8* setting = g_openShadersPromoteNormalSetting;
        if (!setting) {
            // Installer should never permit this state. Fail toward the native
            // non-promoted branch rather than accidentally forcing shadows on.
            return true;
        }
        // Preserve test20 semantics exactly when Global ISL's force option is
        // off: native Open Shaders promotion remains in sole control and the
        // continuous limiter does not become newly active just because bias
        // correction shares this relay.
        return (*setting == 0u);
    }

    // Existing test19/20 spray limiter remains a separate policy layered on
    // top of Global ISL forced promotion. Ordinary/one-shot lights return false.
    return should_skip_continuous_shadow_promotion(niLight);
}

static bool resolve_open_shaders_promote_setting(const u8* gate, const ModuleInfo& info,
                                                  const volatile u8*& settingOut)
{
    settingOut = nullptr;
    if (!gate || !info.imageBase || info.imageSize < 8u) return false;
    // 80 3D xx xx xx xx 00 = cmp byte ptr [rip+disp32],0
    if (gate[0] != 0x80u || gate[1] != 0x3Du || gate[6] != 0x00u) return false;
    const i32 disp = static_cast<i32>(read_u32(gate + 2u));
    const i64 address = static_cast<i64>(reinterpret_cast<uptr>(gate + 7u)) + static_cast<i64>(disp);
    const i64 imageBegin = static_cast<i64>(reinterpret_cast<uptr>(info.imageBase));
    const i64 imageEnd = imageBegin + static_cast<i64>(info.imageSize);
    if (address < imageBegin || address >= imageEnd) return false;
    settingOut = reinterpret_cast<const volatile u8*>(static_cast<uptr>(address));
    return true;
}

static u8* find_promoted_depth_bias_assignment(u8* gate, u8* postPromotionTarget)
{
    if (!gate || !postPromotionTarget || postPromotionTarget <= gate + 23u) return nullptr;
    // Open Shaders source assigns p->depthBias = 1.0f only inside the normal
    // light promotion block. Match that exact write within the already-validated
    // promotion block rather than scanning the module globally.
    static constexpr u8 kWriteBiasOne[] = { 0xC7u,0x47u,0x14u,0x00u,0x00u,0x80u,0x3Fu };
    u8* match = nullptr;
    u32 count = 0u;
    for (u8* cur = gate + 23u; cur + sizeof(kWriteBiasOne) <= postPromotionTarget; ++cur) {
        if (!bytes_equal(cur, kWriteBiasOne, static_cast<u32>(sizeof(kWriteBiasOne)))) continue;
        match = cur;
        ++count;
    }
    return count == 1u ? match : nullptr;
}

static u32 build_shadow_add_policy_relay(u8* out, u32 capacity,
                                         const void* predicate, const void* skipTarget)
{
    if (!out || capacity < 192u || !predicate || !skipTarget) return 0u;
    u32 o = 0u;
    auto emit8 = [&](u8 v) { out[o++] = v; };
    auto emit64 = [&](u64 v) { write_u64(out + o, v); o += 8u; };
    auto emitMovdquStore = [&](u8 xmm, u8 disp) {
        // F3 0F 7F 44 24 xx = movdqu [rsp+disp8], xmmN, with ModRM N in reg field.
        emit8(0xF3u); emit8(0x0Fu); emit8(0x7Fu);
        emit8(static_cast<u8>(0x44u | ((xmm & 7u) << 3u)));
        emit8(0x24u); emit8(disp);
    };
    auto emitMovdquLoad = [&](u8 xmm, u8 disp) {
        // F3 0F 6F 44 24 xx = movdqu xmmN, [rsp+disp8].
        emit8(0xF3u); emit8(0x0Fu); emit8(0x6Fu);
        emit8(static_cast<u8>(0x44u | ((xmm & 7u) << 3u)));
        emit8(0x24u); emit8(disp);
    };

    // Preserve every volatile GPR plus XMM0..XMM5. This inline call is inserted
    // into compiler-generated Open Shaders code that did not originally contain
    // a call at this exact point, so we cannot assume volatile temporaries are dead.
    emit8(0x50u);                         // push rax
    emit8(0x51u);                         // push rcx
    emit8(0x52u);                         // push rdx
    emit8(0x41u); emit8(0x50u);           // push r8
    emit8(0x41u); emit8(0x51u);           // push r9
    emit8(0x41u); emit8(0x52u);           // push r10
    emit8(0x41u); emit8(0x53u);           // push r11
    emit8(0x48u); emit8(0x81u); emit8(0xECu); // sub rsp,80h
    emit8(0x80u); emit8(0x00u); emit8(0x00u); emit8(0x00u);
    for (u8 xmm = 0u; xmm < 6u; ++xmm) emitMovdquStore(xmm, static_cast<u8>(0x20u + xmm * 0x10u));

    emit8(0x48u); emit8(0x89u); emit8(0xF1u);               // mov rcx,rsi (NiLight*)
    emit8(0x48u); emit8(0x89u); emit8(0xFAu);               // mov rdx,rdi (LIGHT_CREATE_PARAMS*)
    emit8(0x48u); emit8(0xB8u); emit64(reinterpret_cast<u64>(predicate));
    emit8(0xFFu); emit8(0xD0u);                             // call rax

    for (u8 xmm = 0u; xmm < 6u; ++xmm) emitMovdquLoad(xmm, static_cast<u8>(0x20u + xmm * 0x10u));
    emit8(0x48u); emit8(0x81u); emit8(0xC4u); // add rsp,80h
    emit8(0x80u); emit8(0x00u); emit8(0x00u); emit8(0x00u);
    emit8(0x84u); emit8(0xC0u);                                // test al,al
    emit8(0x75u);                                                // jne skip_restore
    const u32 branchDispOffset = o++;

    // Normal restore + ret to gate+13 (the original already-shadow CMP).
    emit8(0x41u); emit8(0x5Bu);
    emit8(0x41u); emit8(0x5Au);
    emit8(0x41u); emit8(0x59u);
    emit8(0x41u); emit8(0x58u);
    emit8(0x5Au);
    emit8(0x59u);
    emit8(0x58u);
    emit8(0xC3u);

    const u32 skipLabel = o;
    const i32 shortDisp = static_cast<i32>(skipLabel) - static_cast<i32>(branchDispOffset + 1u);
    if (shortDisp < -128 || shortDisp > 127) return 0u;
    out[branchDispOffset] = static_cast<u8>(shortDisp & 0xFF);

    // Return-address slot is 7 saved GPRs above RSP (56 / 0x38 bytes). Rewrite
    // it BEFORE restoring RAX so every incoming register, including RAX itself,
    // is bit-identical at Open Shaders' non-promoted branch target.
    emit8(0x48u); emit8(0xB8u); emit64(reinterpret_cast<u64>(skipTarget));
    emit8(0x48u); emit8(0x89u); emit8(0x44u); emit8(0x24u); emit8(0x38u); // mov [rsp+38h],rax
    emit8(0x41u); emit8(0x5Bu);
    emit8(0x41u); emit8(0x5Au);
    emit8(0x41u); emit8(0x59u);
    emit8(0x41u); emit8(0x58u);
    emit8(0x5Au);
    emit8(0x59u);
    emit8(0x58u);
    emit8(0xC3u);

    return o <= capacity ? o : 0u;
}

static bool install_open_shaders_shadow_add_policy(const ModuleInfo& info)
{
    // Match test20 when bias correction is disabled: the continuous limiter is
    // only part of Global ISL's forced-promotion path, never a reason by itself
    // to replace Open Shaders' native gate.
    const bool needPolicyRelay = g_forceShadowPromotion || g_shadowBiasCorrectionEnabled;
    if (!needPolicyRelay) {
        log_line("Open Shaders shadow-add policy hook not needed; native promotion and bias remain untouched.");
        return true;
    }

    u32 matches = 0u;
    u8* gate = find_unique_pattern(
        info, kShadowPromotionGatePattern, kShadowPromotionGateMask,
        static_cast<u32>(sizeof(kShadowPromotionGatePattern)), matches);
    if (!gate || matches != 1u) {
        log_line("WARNING: Open Shaders shadow-add gate signature unavailable; ISL remains active but shadow-force/continuous/bias policies could not attach.");
        return false;
    }

    // Semantic invariant from the supported Open Shaders family: branch #1 is
    // PromoteNormalToShadow and branch #2 is the already-shadow test. Both
    // branches converge after the promotion block.
    if (gate[7] != 0x0Fu || gate[8] != 0x84u ||
        gate[17] != 0x0Fu || gate[18] != 0x85u) {
        log_line("WARNING: Open Shaders shadow-add branch ABI changed; optional shadow policies were not installed.");
        return false;
    }
    const i32 firstDisp = static_cast<i32>(read_u32(gate + 9u));
    const i32 secondDisp = static_cast<i32>(read_u32(gate + 19u));
    const i64 firstTarget64 = static_cast<i64>(reinterpret_cast<uptr>(gate + 13u)) + static_cast<i64>(firstDisp);
    const i64 secondTarget64 = static_cast<i64>(reinterpret_cast<uptr>(gate + 23u)) + static_cast<i64>(secondDisp);
    if (firstTarget64 != secondTarget64 || firstTarget64 <= static_cast<i64>(reinterpret_cast<uptr>(gate + 23u))) {
        log_line("WARNING: Open Shaders shadow-add control-flow validation failed; optional shadow policies were not installed.");
        return false;
    }
    u8* const postPromotionTarget = reinterpret_cast<u8*>(static_cast<uptr>(firstTarget64));
    const uptr promotionSpan = reinterpret_cast<uptr>(postPromotionTarget) - reinterpret_cast<uptr>(gate + 23u);
    if (!text_address_in_module(info, postPromotionTarget) || promotionSpan > 4096u) {
        log_line("WARNING: Open Shaders shadow-add branch target escaped the validated .text region; optional shadow policies were not installed.");
        return false;
    }

    if (!shadow_gate_register_contract_ok(gate, info)) {
        log_line("WARNING: Open Shaders shadow-add register contract was not verified; optional shadow policies were not installed.");
        // Preserve the old forced-promotion fallback when that is the only
        // behavior the user explicitly requested.
        if (g_forceShadowPromotion && !g_shadowBiasCorrectionEnabled && !g_continuousShadowLimiterEnabled) {
            static constexpr u8 kForcePromotionPatch[6] = { 0x90u,0x90u,0x90u,0x90u,0x90u,0x90u };
            if (write_code_bytes(gate + 7u, kForcePromotionPatch, 6u) &&
                bytes_equal(gate + 7u, kForcePromotionPatch, 6u)) {
                log_line("Open Shaders native normal-light shadow promotion forced ON (legacy fallback).");
                return true;
            }
        }
        return false;
    }

    const volatile u8* nativeSetting = nullptr;
    if (!resolve_open_shaders_promote_setting(gate, info, nativeSetting)) {
        log_line("WARNING: Open Shaders PromoteNormalToShadow setting address could not be validated; optional shadow policies were not installed.");
        return false;
    }
    g_openShadersPromoteNormalSetting = nativeSetting;

    // If a user deliberately configures a cap below Open Shaders' promoted
    // default of 1.0, its promotion block would otherwise overwrite our earlier
    // cap. Resolve that single immediate write inside the validated block now;
    // no patch is necessary for default MaximumBias=1.0 because Open Shaders'
    // own promoted-light value is already exactly 1.0.
    u8* promotedBiasWrite = nullptr;
    if (g_shadowBiasCorrectionEnabled && g_shadowBiasMaximum < 1.0f) {
        promotedBiasWrite = find_promoted_depth_bias_assignment(gate, postPromotionTarget);
        if (!promotedBiasWrite) {
            log_line("WARNING: MaximumBias below 1.0 requires the promoted-bias assignment, but it could not be uniquely resolved; shadow-bias correction was disabled for safety.");
            g_shadowBiasCorrectionEnabled = false;
        }
    }

    u8 originalGateBytes[6] = {};
    copy_bytes(originalGateBytes, gate + 7u, 6u);

    u8* relayPage = static_cast<u8*>(allocate_near_relay_page(gate + 7u, nullptr, nullptr));
    if (!relayPage) {
        log_line("WARNING: Open Shaders shadow-add policy relay page could not be allocated.");
        return false;
    }
    const u32 relaySize = build_shadow_add_policy_relay(
        relayPage, 192u, reinterpret_cast<const void*>(&process_shadow_add_policy),
        reinterpret_cast<const void*>(postPromotionTarget));
    if (relaySize == 0u) {
        VirtualFree(relayPage, 0u, MEM_RELEASE_);
        log_line("WARNING: Open Shaders shadow-add policy relay could not be built.");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relayPage, relaySize);
    DWORD oldProtect = 0u;
    if (!VirtualProtect(relayPage, 0x1000u, PAGE_EXECUTE_READ_, &oldProtect)) {
        VirtualFree(relayPage, 0u, MEM_RELEASE_);
        log_line("WARNING: Open Shaders shadow-add policy relay could not be made executable.");
        return false;
    }

    u8 relayPatch[6] = { 0xE8u,0u,0u,0u,0u,0x90u };
    i32 relayDisp = 0;
    if (!rel32_displacement(gate + 12u, relayPage, relayDisp)) {
        VirtualFree(relayPage, 0u, MEM_RELEASE_);
        log_line("WARNING: Open Shaders shadow-add policy relay was outside rel32 range.");
        return false;
    }
    write_u32(relayPatch + 1u, static_cast<u32>(relayDisp));
    if (!write_code_bytes(gate + 7u, relayPatch, 6u) ||
        !bytes_equal(gate + 7u, relayPatch, 6u)) {
        VirtualFree(relayPage, 0u, MEM_RELEASE_);
        log_line("WARNING: Open Shaders shadow-add policy gate patch failed.");
        return false;
    }

    if (promotedBiasWrite && g_shadowBiasCorrectionEnabled) {
        FloatBits bits{};
        bits.f = g_shadowBiasMaximum;
        u8 immediate[4] = {};
        write_u32(immediate, bits.u);
        if (!write_code_bytes(promotedBiasWrite + 3u, immediate, 4u) ||
            !bytes_equal(promotedBiasWrite + 3u, immediate, 4u)) {
            // Do not leave a partially-installed configurable bias feature. The
            // relay can be safely detached by restoring the exact original JZ.
            write_code_bytes(gate + 7u, originalGateBytes, 6u);
            VirtualFree(relayPage, 0u, MEM_RELEASE_);
            g_shadowBiasCorrectionEnabled = false;
            log_line("WARNING: Open Shaders promoted-depth-bias immediate patch failed; shadow-add policy relay rolled back.");
            return false;
        }
    }

    if (g_shadowBiasCorrectionEnabled) {
        log_line("Shadow Depth Bias correction active on local ShadowSceneNode AddLight params; values above MaximumBias are capped only.");
    }
    if (g_forceShadowPromotion) {
        log_line("Open Shaders normal-light shadow promotion forced ON through the shared shadow-add policy relay.");
    } else {
        log_line("Open Shaders native PromoteNormalToShadow setting preserved through the shared shadow-add policy relay.");
    }
    if (g_continuousShadowLimiterEnabled) {
        log_line("Continuous dynamic bursts remain rate-limited before BSShadowLight promotion; one-shot projectiles remain unchanged.");
    }
    return true;
}

static bool build_entry_jump(u8* out, u32 overwriteSize, u8* source, u8* relay)
{
    if (!out || overwriteSize < 5u || !source || !relay) return false;
    i32 displacement = 0;
    if (!rel32_displacement(source + 5u, relay, displacement)) return false;
    fill_bytes(out, 0x90u, overwriteSize);
    out[0] = 0xE9u;
    write_u32(out + 1u, static_cast<u32>(displacement));
    return true;
}

static bool install_universal_point_light_hooks(const ModuleInfo& info)
{
    u32 pushMatches = 0u;
    u32 processMatches = 0u;
    u32 createMatches = 0u;
    u8* pushTarget = find_unique_pattern(
        info, kPushTargetPattern, kPushTargetMask,
        static_cast<u32>(sizeof(kPushTargetPattern)), pushMatches);
    u8* processTarget = find_unique_pattern(
        info, kProcessLightPattern, kProcessLightMask,
        static_cast<u32>(sizeof(kProcessLightPattern)), processMatches);
    u8* createTarget = find_unique_pattern(
        info, kCreatePointLightPattern, kCreatePointLightMask,
        static_cast<u32>(sizeof(kCreatePointLightPattern)), createMatches);

    static constexpr u32 kMaxEffects11AuthorityCallsites = 16u;
    u8* effects11Callsites[kMaxEffects11AuthorityCallsites] = {};
    u32 effects11CallsiteCount = 0u;
    u8* effects11Target = resolve_effects11_point_light_color_target(
        info, processTarget, effects11Callsites, kMaxEffects11AuthorityCallsites, effects11CallsiteCount);

    log_hex32("S pushMatches=", pushMatches);
    log_hex32("S processMatches=", processMatches);
    log_hex32("S createMatches=", createMatches);
    log_hex32("S effects11PointColorCalls=", effects11CallsiteCount);

    if (!pushTarget || pushMatches != 1u) {
        log_error("Renderer LightData push helper signature was missing/ambiguous. Patch disabled.");
        return false;
    }

    const u32 pushCallCount = count_direct_calls_to(info, pushTarget);
    log_hex32("S pushDirectCalls=", pushCallCount);
    if (pushCallCount == 0u || pushCallCount > 96u) {
        log_error("Renderer LightData push helper call structure is unsupported. Patch disabled.");
        return false;
    }

    static constexpr u8 kExpectedPushPrologue[kPushOverwriteSize] = {
        0x40,0x53,0x55,0x57,0x48,0x83,0xEC,0x20
    };
    static constexpr u8 kExpectedProcessPrologue[kProcessOverwriteSize] = {
        0x48,0x8B,0xC4,0x48,0x89,0x48,0x08
    };
    static constexpr u8 kExpectedCreatePrologue[kCreatePointLightOverwriteSize] = {
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18
    };
    if (!bytes_equal(pushTarget, kExpectedPushPrologue, kPushOverwriteSize)) {
        log_error("Renderer LightData push prologue ABI changed. Patch disabled.");
        return false;
    }

    const bool classifierWanted = any_policy_scan_enabled();
    bool processSupported = processTarget && processMatches == 1u;
    if (processSupported) {
        const u32 processCallCount = count_direct_calls_to(info, processTarget);
        log_hex32("S processDirectCalls=", processCallCount);
        processSupported = processCallCount > 0u && processCallCount <= 96u &&
            bytes_equal(processTarget, kExpectedProcessPrologue, kProcessOverwriteSize);
    }

    const bool createSupported = createTarget && createMatches == 1u &&
        bytes_equal(createTarget, kExpectedCreatePrologue, kCreatePointLightOverwriteSize);
    const bool classifierSupported = classifierWanted && processSupported && createSupported;
    if (classifierWanted && !classifierSupported) {
        log_line("WARNING: runtime EditorID classifier unavailable; environmental/hand/impact/fire policies are disabled, ordinary ISL remains active.");
    }
    if (!classifierSupported) {
        processTarget = nullptr;
        createTarget = nullptr;
    }

    bool effects11AuthoritySupported = g_fireColorAuthoritative && g_fireColorEnabled &&
        classifierSupported && effects11Target && effects11CallsiteCount >= 2u &&
        effects11CallsiteCount <= kMaxEffects11AuthorityCallsites;
    if (g_fireColorAuthoritative && g_fireColorEnabled && classifierSupported && !effects11AuthoritySupported) {
        log_line("WARNING: Effects11 point-light color callsites could not be resolved safely; ProcessLight/push fire authority remains active, but Effects11 can still repaint strict lights.");
        effects11Target = nullptr;
        effects11CallsiteCount = 0u;
    }

    u8* relayPage = static_cast<u8*>(allocate_near_relay_page(pushTarget, processTarget, createTarget));
    if (!relayPage) {
        log_error("Unable to allocate a near trampoline page for renderer hooks. Patch disabled.");
        return false;
    }

    u8* pushRelay = relayPage + 0x00u;
    u8* processRelay = relayPage + 0x10u;
    u8* createRelay = relayPage + 0x20u;
    u8* effects11Relay = relayPage + 0x30u;
    u8* pushTrampoline = relayPage + 0x40u;
    u8* processTrampoline = relayPage + 0x80u;
    u8* createTrampoline = relayPage + 0xC0u;

    u8 absoluteJump[14] = {};
    build_absolute_jump14(absoluteJump, reinterpret_cast<const void*>(&universal_point_light_push_hook));
    copy_bytes(pushRelay, absoluteJump, 14u);
    copy_bytes(pushTrampoline, kExpectedPushPrologue, kPushOverwriteSize);
    build_absolute_jump14(absoluteJump, pushTarget + kPushOverwriteSize);
    copy_bytes(pushTrampoline + kPushOverwriteSize, absoluteJump, 14u);

    if (classifierSupported) {
        build_absolute_jump14(absoluteJump, reinterpret_cast<const void*>(&universal_strict_process_light_hook));
        copy_bytes(processRelay, absoluteJump, 14u);
        copy_bytes(processTrampoline, kExpectedProcessPrologue, kProcessOverwriteSize);
        build_absolute_jump14(absoluteJump, processTarget + kProcessOverwriteSize);
        copy_bytes(processTrampoline + kProcessOverwriteSize, absoluteJump, 14u);

        build_absolute_jump14(absoluteJump, reinterpret_cast<const void*>(&universal_create_point_light_hook));
        copy_bytes(createRelay, absoluteJump, 14u);
        copy_bytes(createTrampoline, kExpectedCreatePrologue, kCreatePointLightOverwriteSize);
        build_absolute_jump14(absoluteJump, createTarget + kCreatePointLightOverwriteSize);
        copy_bytes(createTrampoline + kCreatePointLightOverwriteSize, absoluteJump, 14u);
    }
    if (effects11AuthoritySupported) {
        build_absolute_jump14(absoluteJump, reinterpret_cast<const void*>(&universal_effects11_point_light_color_hook));
        copy_bytes(effects11Relay, absoluteJump, 14u);
    }

    FlushInstructionCache(GetCurrentProcess(), relayPage, 0x100u);
    DWORD relayOldProtect = 0u;
    if (!VirtualProtect(relayPage, 0x1000u, PAGE_EXECUTE_READ_, &relayOldProtect)) {
        VirtualFree(relayPage, 0u, MEM_RELEASE_);
        log_error("Unable to protect renderer trampoline page. Patch disabled.");
        return false;
    }

    u8 pushPatch[kPushOverwriteSize] = {};
    u8 processPatch[kProcessOverwriteSize] = {};
    u8 createPatch[kCreatePointLightOverwriteSize] = {};
    if (!build_entry_jump(pushPatch, kPushOverwriteSize, pushTarget, pushRelay) ||
        (classifierSupported &&
         (!build_entry_jump(processPatch, kProcessOverwriteSize, processTarget, processRelay) ||
          !build_entry_jump(createPatch, kCreatePointLightOverwriteSize, createTarget, createRelay)))) {
        VirtualFree(relayPage, 0u, MEM_RELEASE_);
        log_error("Renderer trampoline fell outside rel32 range. Patch disabled.");
        return false;
    }
    if (effects11AuthoritySupported) {
        for (u32 i = 0u; i < effects11CallsiteCount; ++i) {
            u8 callPatch[5] = {};
            if (!build_rel32_call(callPatch, effects11Callsites[i], effects11Relay)) {
                effects11AuthoritySupported = false;
                effects11Target = nullptr;
                effects11CallsiteCount = 0u;
                log_line("WARNING: Effects11 authority relay was outside rel32 range; strict fire recolor falls back to ProcessLight stage.");
                break;
            }
        }
    }

    g_originalPushLight = reinterpret_cast<PushLightFn>(pushTrampoline);
    g_originalProcessLight = classifierSupported ? reinterpret_cast<ProcessLightFn>(processTrampoline) : nullptr;
    g_originalCreatePointLight = classifierSupported ? reinterpret_cast<CreatePointLightFn>(createTrampoline) : nullptr;
    g_originalEffects11OverridePointLightColor = effects11AuthoritySupported
        ? reinterpret_cast<Effects11OverridePointLightColorFn>(effects11Target) : nullptr;

    bool pushPatched = false;
    bool processPatched = false;
    bool createPatched = false;

    if (write_code_bytes(pushTarget, pushPatch, kPushOverwriteSize)) {
        pushPatched = true;
    } else {
        g_originalPushLight = nullptr;
        g_originalProcessLight = nullptr;
        g_originalCreatePointLight = nullptr;
        VirtualFree(relayPage, 0u, MEM_RELEASE_);
        log_error("Renderer LightData push entry patch failed.");
        return false;
    }

    if (classifierSupported) {
        if (write_code_bytes(processTarget, processPatch, kProcessOverwriteSize)) {
            processPatched = true;
        }
        if (processPatched && write_code_bytes(createTarget, createPatch, kCreatePointLightOverwriteSize)) {
            createPatched = true;
        }

        if (!processPatched || !createPatched) {
            const bool createRestored = !createPatched ||
                write_code_bytes(createTarget, kExpectedCreatePrologue, kCreatePointLightOverwriteSize);
            const bool processRestored = !processPatched ||
                write_code_bytes(processTarget, kExpectedProcessPrologue, kProcessOverwriteSize);
            const bool pushRestored = write_code_bytes(pushTarget, kExpectedPushPrologue, kPushOverwriteSize);
            if (createRestored) g_originalCreatePointLight = nullptr;
            if (processRestored) g_originalProcessLight = nullptr;
            if (pushRestored) g_originalPushLight = nullptr;
            if (createRestored && processRestored && pushRestored) VirtualFree(relayPage, 0u, MEM_RELEASE_);
            log_error("Runtime EditorID classifier hook transaction failed; installed hooks were rolled back where possible.");
            return false;
        }
    }

    bool effects11Patched = false;
    if (effects11AuthoritySupported) {
        u32 patchedCount = 0u;
        for (u32 i = 0u; i < effects11CallsiteCount; ++i) {
            u8 callPatch[5] = {};
            if (!build_rel32_call(callPatch, effects11Callsites[i], effects11Relay) ||
                !write_code_bytes(effects11Callsites[i], callPatch, 5u)) {
                break;
            }
            ++patchedCount;
        }
        if (patchedCount == effects11CallsiteCount) {
            effects11Patched = true;
            g_effects11AuthorityInstalled = true;
        } else {
            // Roll back every callsite already redirected. The original target
            // remains intact because this layer patches only direct CALLs.
            for (u32 i = 0u; i < patchedCount; ++i) {
                u8 originalCall[5] = {};
                originalCall[0] = 0xE8u;
                i32 originalDisp = 0;
                if (rel32_displacement(effects11Callsites[i] + 5u, effects11Target, originalDisp)) {
                    write_u32(originalCall + 1u, static_cast<u32>(originalDisp));
                    write_code_bytes(effects11Callsites[i], originalCall, 5u);
                }
            }
            g_originalEffects11OverridePointLightColor = nullptr;
            g_effects11AuthorityInstalled = false;
            log_line("WARNING: Effects11 authority callsite transaction failed and was rolled back; core ISL hooks remain active.");
        }
    }

    const bool pushVerified = bytes_equal(pushTarget, pushPatch, kPushOverwriteSize);
    const bool processVerified = !classifierSupported ||
        bytes_equal(processTarget, processPatch, kProcessOverwriteSize);
    const bool createVerified = !classifierSupported ||
        bytes_equal(createTarget, createPatch, kCreatePointLightOverwriteSize);
    bool effects11Verified = true;
    if (effects11Patched) {
        for (u32 i = 0u; i < effects11CallsiteCount; ++i) {
            u8 expectedCall[5] = {};
            if (!build_rel32_call(expectedCall, effects11Callsites[i], effects11Relay) ||
                !bytes_equal(effects11Callsites[i], expectedCall, 5u)) {
                effects11Verified = false;
                break;
            }
        }
    }
    if (!pushVerified || !processVerified || !createVerified || !effects11Verified) {
        if (effects11Patched) {
            for (u32 i = 0u; i < effects11CallsiteCount; ++i) {
                u8 originalCall[5] = {};
                originalCall[0] = 0xE8u;
                i32 originalDisp = 0;
                if (rel32_displacement(effects11Callsites[i] + 5u, effects11Target, originalDisp)) {
                    write_u32(originalCall + 1u, static_cast<u32>(originalDisp));
                    write_code_bytes(effects11Callsites[i], originalCall, 5u);
                }
            }
            g_originalEffects11OverridePointLightColor = nullptr;
            g_effects11AuthorityInstalled = false;
        }
        const bool createRestored = !createPatched ||
            write_code_bytes(createTarget, kExpectedCreatePrologue, kCreatePointLightOverwriteSize);
        const bool processRestored = !processPatched ||
            write_code_bytes(processTarget, kExpectedProcessPrologue, kProcessOverwriteSize);
        const bool pushRestored = !pushPatched ||
            write_code_bytes(pushTarget, kExpectedPushPrologue, kPushOverwriteSize);
        if (createRestored) g_originalCreatePointLight = nullptr;
        if (processRestored) g_originalProcessLight = nullptr;
        if (pushRestored) g_originalPushLight = nullptr;
        if (createRestored && processRestored && pushRestored) VirtualFree(relayPage, 0u, MEM_RELEASE_);
        log_error("Renderer hook verification failed; installed hooks were rolled back where possible.");
        return false;
    }

    log_line("Signature-resolved renderer LightData push hook installed (80/96-byte ABI auto-detect).");
    if (classifierSupported) {
        log_line("Global runtime TESObjectLIGH EditorID classifier installed.");
        log_line("Environmental matches bypass MaxRadius; hand lights and HAZD/MAG+Impact lights use dedicated local caps.");
        if (g_fireColorAuthoritative) {
            log_line("Authoritative fire color stage active at Open Shaders ProcessLight (clustered + strict paths).");
            if (g_effects11AuthorityInstalled) {
                log_line("Authoritative fire color reassertion installed after Effects11 point-light color processing.");
            }
        }
    }
    return true;
}

enum class InstallResult : u32 {
    Success,
    ModuleNotLoaded,
    Failure
};

static InstallResult try_install_hook()
{
    if (g_hookInstalled) return InstallResult::Success;
    if (g_permanentFailure) return InstallResult::Failure;

    HMODULE module = GetModuleHandleA(kCommunityShadersModule);
    if (!module) return InstallResult::ModuleNotLoaded;

    ModuleInfo info{};
    if (!get_module_info(module, info)) {
        log_error("CommunityShaders.dll/Open Shaders renderer PE validation failed. Plugin disabled.");
        g_permanentFailure = true;
        return InstallResult::Failure;
    }

    // Timestamp/SizeOfImage are diagnostic only. Compatibility is decided by
    // startup-only semantic signature resolution and structural validation.
    log_hex32("CommunityShaders.dll timestamp: ", info.timeDateStamp);

    if (!install_universal_point_light_hooks(info)) {
        g_permanentFailure = true;
        return InstallResult::Failure;
    }

    // Independent optional shadow-add policy layer. A failure here must never regress
    // the proven ISL conversion/whitelist/falloff path. Bias correction, forced
    // promotion and the continuous-spell limiter all share this validated hook.
    install_open_shaders_shadow_add_policy(info);

    g_hookInstalled = true;
    log_line("Global Point Light ISL renderer hooks installed.");
    return InstallResult::Success;
}

// SKSE messaging provides startup retry plus authoritative save/load lifecycle boundaries.
// No renderer patching is ever attempted from gameplay/load-game messages.
static void on_skse_message(SKSEMessage* message)
{
    if (!message) return;

    if (message->type == kMessage_SaveGame) {
        if (!g_enabled || !g_hookInstalled || !g_taskInterface || !g_taskInterface->AddTask) return;
        __atomic_store_n(&g_transitionKind, 1u, __ATOMIC_RELEASE);
        __atomic_add_fetch(&g_pendingSaveReleaseTasks, 1u, __ATOMIC_ACQ_REL);
        begin_transition(false);
        g_taskInterface->AddTask(&g_saveGuardReleaseTask);
        log_line("Lifecycle safety: save suspension armed.");
        return;
    }

    if (message->type == kMessage_PreLoadGame) {
        if (!g_enabled) return;
        __atomic_store_n(&g_transitionKind, 2u, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pendingSaveReleaseTasks, 0u, __ATOMIC_RELEASE);
        begin_transition(true);
        log_line("Lifecycle safety: load suspension armed and runtime generation advanced.");
        return;
    }

    if (message->type == kMessage_PostLoadGame) {
        if (!g_enabled) return;
        reset_admission_state_while_suspended();
        __atomic_store_n(&g_transitionKind, 0u, __ATOMIC_RELEASE);
        request_resume_after(kLoadSettleMs);
        log_line("Lifecycle safety: load completed; delayed resume requested.");
        return;
    }

    if (message->type == kMessage_NewGame) {
        if (!g_enabled) return;
        __atomic_store_n(&g_transitionKind, 2u, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pendingSaveReleaseTasks, 0u, __ATOMIC_RELEASE);
        begin_transition(true);
        reset_admission_state_while_suspended();
        __atomic_store_n(&g_transitionKind, 0u, __ATOMIC_RELEASE);
        request_resume_after(kLoadSettleMs);
        log_line("Lifecycle safety: new game; delayed resume requested.");
        return;
    }

    if (message->type != kMessage_PostLoad ||
        g_hookInstalled || g_permanentFailure || !g_enabled) {
        return;
    }

    const InstallResult result = try_install_hook();
    if (result == InstallResult::ModuleNotLoaded) {
        g_permanentFailure = true;
        log_error("CommunityShaders.dll was still unavailable at SKSE PostLoad. Plugin disabled.");
    }
}

// -----------------------------------------------------------------------------
// SKSE entry points
// -----------------------------------------------------------------------------

DLLEXPORT bool SKSEPlugin_Query(const SKSEInterface* skse, PluginInfo* info)
{
    if (!skse || !info) return false;

    info->infoVersion = 1u;
    info->name = kPluginName;
    info->version = 125u;

    load_debug_setting();
    if (g_loggingEnabled) {
        ensure_log(false);
        log_line("=== Global Point Light ISL 1.5-test22 ===");
    }

    if (skse->isEditor != 0u) {
        log_error("Rejected: editor runtime.");
        return false;
    }
    if (skse->runtimeVersion != kRuntime_1_5_97) {
        log_error_hex32("Rejected unsupported Skyrim runtime: ", skse->runtimeVersion);
        return false;
    }
    log_line("Skyrim SE 1.5.97 runtime validated.");
    log_line("Global runtime EditorID + HAZD local-magic radius build with lifecycle-safe renderer admission.");
    log_line("ISL conversion remains the shallow 1.1 final-LightData path; authoritative fire RGB covers ProcessLight, Effects11 and final clustered submission.");
    log_line("TESObjectLIGH metadata classification remains active during lifecycle suspension; renderer mutation remains gated.");
    return true;
}

DLLEXPORT bool SKSEPlugin_Load(const SKSEInterface* skse)
{
    if (!skse || !skse->QueryInterface || !skse->GetPluginHandle) {
        log_error("SKSEPlugin_Load received an invalid SKSE interface.");
        return false;
    }

    load_settings();
    if (!g_enabled) {
        log_line("Disabled by INI; no renderer hook installed.");
        return true;
    }
    load_editor_id_policy_settings();

    const PluginHandle pluginHandle = skse->GetPluginHandle();
    auto* messaging = static_cast<SKSEMessagingInterface*>(skse->QueryInterface(kInterface_Messaging));
    g_taskInterface = static_cast<SKSETaskInterface*>(skse->QueryInterface(kInterface_Task));

    // Register once even when the renderer is already available: kMessage_SaveGame
    // is the authoritative pre-save boundary used by the safety guard.
    const bool listenerReady =
        messaging && messaging->RegisterListener &&
        pluginHandle != kInvalidPluginHandle &&
        messaging->RegisterListener(pluginHandle, "SKSE", on_skse_message);

    if (!listenerReady || !g_taskInterface || !g_taskInterface->AddTask) {
        g_taskInterface = nullptr;
        g_permanentFailure = true;
        log_error("Lifecycle safety requires SKSE messaging + task interfaces; renderer hooks were not installed.");
        return true;
    }

    const InstallResult immediate = try_install_hook();
    if (immediate == InstallResult::Success || immediate == InstallResult::Failure) {
        return true;
    }

    // Renderer DLL was not loaded yet. The already-registered listener provides
    // exactly one early PostLoad retry. Never patch after that startup boundary.
    if (!listenerReady) {
        g_permanentFailure = true;
        log_error("CommunityShaders.dll not loaded and SKSE PostLoad fallback unavailable. Plugin disabled.");
    } else {
        log_line("CommunityShaders.dll not loaded yet; one SKSE PostLoad retry registered.");
    }

    return true;
}

extern "C" BOOL WINAPI DllMain(void*, DWORD, void*)
{
    return 1;
}
