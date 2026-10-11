// Exercise the actual plugin code with simulated Win32 services. We inspect
// patched bytes and ownership; synthetic Windows machine code is never run.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define __declspec(x)
#define __stdcall
#define memset isl_memset
#define memcpy isl_memcpy
#ifndef ISL_SOURCE_FILE
#define ISL_SOURCE_FILE "../GlobalPointLightISL.cpp"
#endif
#include ISL_SOURCE_FILE
#undef memcpy
#undef memset
#undef __stdcall
#undef __declspec

namespace test {
alignas(65536) u8 image[0x40000];
u8* const relay = image + 0x30000;
u8* const push = image + 0x100;
u8* const process = image + 0x400;
u8* const create = image + 0x800;
u8* const effects = image + 0x1000;
u8* const effectsCall1 = image + 0x2010;
u8* const effectsCall2 = image + 0x2410;
u8* const gate = image + 0x600;
u8* const bias = gate + 40;
u32 frees = 0;
bool damagePush = false;
u64 tick = 1000;
std::vector<u8> fileData;
SIZE_T filePosition = 0;
struct Denial { void* address; u32 firstDeniedWrite; u32 writes; };
Denial denials[4]{};

void reset()
{
    std::memset(image, 0xCC, sizeof(image));
    std::memset(image + 0x10000, 0, 0x1000);
    std::memset(denials, 0, sizeof(denials));
    frees = 0;
    tick = 1000;
    fileData.clear();
    filePosition = 0;
    damagePush = false;
    g_log = nullptr;
    g_loggingEnabled = false;
    g_originalPushLight = nullptr;
    g_originalProcessLight = nullptr;
    g_originalCreatePointLight = nullptr;
    g_originalEffects11OverridePointLightColor = nullptr;
    g_effects11AuthorityInstalled = false;
    g_forceShadowPromotion = true;
    g_shadowBiasCorrectionEnabled = true;
    g_shadowBiasMaximum = 1.0f;
    g_fireColorEnabled = true;
    g_fireColorAuthoritative = true;
    g_lifecycleSuspendActive = 0;
    g_resumeNotBeforeTick = 0;
    g_taskInterface = nullptr;
    g_sceneGeneration = 1;
    g_clearPendingAfterTransition = 0;
    g_pendingLightPolicy = {};
    g_continuousShadowLimiterEnabled = true;
    g_continuousStateLock = 0;
    g_continuousStateGeneration = 0;
    g_continuousInstancesDirty = 0;
    g_untrackedDynamicCreations = 0;
    std::memset(g_activeDynamicContexts, 0, sizeof(g_activeDynamicContexts));
    std::memset(g_runtimeClassCache, 0, sizeof(g_runtimeClassCache));
    std::memset(g_detachedFireRefCache, 0, sizeof(g_detachedFireRefCache));
    std::memset(g_detachedFireInstanceCache, 0, sizeof(g_detachedFireInstanceCache));
}

void deny(u32 slot, void* address, u32 firstWrite)
{
    assert(slot < 4);
    denials[slot] = {address, firstWrite, 0};
}

ModuleInfo module()
{
    return {0, image, sizeof(image), image, 0x8000, image + 0x10000, 0x1000};
}

void call(u8* site, u8* target)
{
    assert(build_rel32_call(site, site, target));
}

void hooks_fixture()
{
    reset();
    copy_bytes(push, kPushTargetPattern, sizeof(kPushTargetPattern));
    copy_bytes(process, kProcessLightPattern, sizeof(kProcessLightPattern));
    copy_bytes(create, kCreatePointLightPattern, sizeof(kCreatePointLightPattern));
    call(image + 0x2000, process);
    call(image + 0x2400, process);
    call(effectsCall1, effects);
    call(effectsCall2, effects);
    call(image + 0x2800, push);
    const char* literals[] = {"PointLightingCurve", "PointLightingDesaturation", "PointLightingIntensity"};
    for (u32 i = 0; i < 3; ++i) {
        u8* literal = image + 0x10000 + i * 64;
        std::strcpy(reinterpret_cast<char*>(literal), literals[i]);
        i32 displacement = 0;
        assert(rel32_displacement(effects + i * 8 + 4, literal, displacement));
        write_u32(effects + i * 8, static_cast<u32>(displacement));
    }
    u8* sites[16]{};
    u32 count = 0;
    assert(resolve_effects11_point_light_color_target(module(), process, sites, 16, count) == effects);
    assert(count == 2 && sites[0] == effectsCall1 && sites[1] == effectsCall2);
}

void shadow_fixture()
{
    reset();
    copy_bytes(gate, kShadowPromotionGatePattern, sizeof(kShadowPromotionGatePattern));
    const u8 loadLight[] = {0x48,0x8B,0xB1,0x88,0,0,0};
    const u8 loadParams[] = {0x48,0x8B,0xB9,0xB8,0,0,0};
    copy_bytes(gate - 32, loadLight, sizeof(loadLight));
    copy_bytes(gate - 16, loadParams, sizeof(loadParams));
    write_u32(gate + 2, static_cast<u32>((image + 0x10000) - (gate + 7)));
    write_u32(gate + 9, 80 - 13);
    write_u32(gate + 19, 80 - 23);
    const u8 assignment[] = {0xC7,0x47,0x14,0,0,0x80,0x3F};
    copy_bytes(bias, assignment, sizeof(assignment));
    g_shadowBiasMaximum = 0.5f;
}

void shadow_failed_rollback()
{
    shadow_fixture();
    deny(0, bias + 3, 1);
    deny(1, gate + 7, 2);
    assert(!install_open_shaders_shadow_add_policy(module()));
    assert(decode_rel32_call_target(gate + 7) == relay);
    assert(frees == 0); // A surviving CALL must never point at a freed page.
    assert(!g_shadowBiasCorrectionEnabled);
}

void shadow_successful_rollback()
{
    shadow_fixture();
    deny(0, bias + 3, 1);
    assert(!install_open_shaders_shadow_add_policy(module()));
    assert(gate[7] == 0x0F && gate[8] == 0x84);
    assert(frees == 1);
}

void shadow_gate_denied()
{
    shadow_fixture();
    deny(0, gate + 7, 1);
    assert(!install_open_shaders_shadow_add_policy(module()));
    assert(gate[7] == 0x0F && gate[8] == 0x84);
    assert(frees == 1);
}

void shadow_success()
{
    shadow_fixture();
    assert(install_open_shaders_shadow_add_policy(module()));
    assert(decode_rel32_call_target(gate + 7) == relay);
    assert(read_f32(bias + 3) == 0.5f);
    assert(frees == 0);
}

void effects_partial_failed_rollback()
{
    hooks_fixture();
    deny(0, effectsCall2, 1);
    deny(1, effectsCall1, 2);
    assert(install_universal_point_light_hooks(module()));
    assert(decode_rel32_call_target(effectsCall1) == relay + 0x30);
    assert(decode_rel32_call_target(effectsCall2) == effects);
    assert(g_originalEffects11OverridePointLightColor == reinterpret_cast<Effects11OverridePointLightColorFn>(effects));
    assert(!g_effects11AuthorityInstalled);
    assert(frees == 0);
}

void effects_partial_successful_rollback()
{
    hooks_fixture();
    deny(0, effectsCall2, 1);
    assert(install_universal_point_light_hooks(module()));
    assert(decode_rel32_call_target(effectsCall1) == effects);
    assert(decode_rel32_call_target(effectsCall2) == effects);
    assert(g_originalEffects11OverridePointLightColor == nullptr);
    assert(frees == 0); // The core hooks still own this page.
}

void effects_final_failed_rollback()
{
    hooks_fixture();
    damagePush = true;
    deny(0, effectsCall1, 2);
    assert(!install_universal_point_light_hooks(module()));
    assert(bytes_equal(push, kPushTargetPattern, kPushOverwriteSize));
    assert(bytes_equal(process, kProcessLightPattern, kProcessOverwriteSize));
    assert(bytes_equal(create, kCreatePointLightPattern, kCreatePointLightOverwriteSize));
    assert(decode_rel32_call_target(effectsCall1) == relay + 0x30);
    assert(g_originalEffects11OverridePointLightColor == reinterpret_cast<Effects11OverridePointLightColorFn>(effects));
    assert(frees == 0);
}

void effects_final_successful_rollback()
{
    hooks_fixture();
    damagePush = true;
    assert(!install_universal_point_light_hooks(module()));
    assert(decode_rel32_call_target(effectsCall1) == effects);
    assert(decode_rel32_call_target(effectsCall2) == effects);
    assert(g_originalEffects11OverridePointLightColor == nullptr);
    assert(g_originalPushLight == nullptr && g_originalProcessLight == nullptr && g_originalCreatePointLight == nullptr);
    assert(frees == 1);
}

void core_failed_rollback()
{
    hooks_fixture();
    deny(0, create, 1);
    deny(1, process, 2);
    assert(!install_universal_point_light_hooks(module()));
    assert(process[0] == 0xE9);
    assert(g_originalProcessLight != nullptr);
    assert(g_originalPushLight == nullptr && g_originalCreatePointLight == nullptr);
    assert(frees == 0);
}

void core_success()
{
    hooks_fixture();
    assert(install_universal_point_light_hooks(module()));
    assert(g_originalPushLight && g_originalProcessLight && g_originalCreatePointLight);
    assert(g_originalEffects11OverridePointLightColor && g_effects11AuthorityInstalled);
    assert(frees == 0);
}

u64 spray_fixture()
{
    reset();
    u64 key = 0;
    for (u32 i = 0; i < g_continuousBurstThreshold; ++i) key = note_dynamic_stream_event(0x800, 0x123);
    remember_dynamic_light_instance(image, key, g_sceneGeneration);
    assert(should_skip_continuous_shadow_promotion(image));
    return key;
}

void continuous_new_generation()
{
    spray_fixture();
    ++g_sceneGeneration;
    assert(!should_skip_continuous_shadow_promotion(image));
    // Old creation results must not be entered into a new scene's cache.
    remember_dynamic_light_instance(image, 0x12300000800ull, g_sceneGeneration - 1);
    assert(find_continuous_instance(reinterpret_cast<uptr>(image), tick) == nullptr);
}

void continuous_static_reuse()
{
    spray_fixture();
    auto* context = push_active_dynamic_context(0);
    assert(context);
    assert(!should_skip_continuous_shadow_promotion(image));
    pop_active_dynamic_context(context);
    remember_dynamic_light_instance(image, 0, g_sceneGeneration);
    assert(!should_skip_continuous_shadow_promotion(image));
}

void continuous_nested_and_overflow()
{
    const u64 key = spray_fixture();
    auto* outer = push_active_dynamic_context(key);
    auto* inner = push_active_dynamic_context(0);
    assert(!should_skip_continuous_shadow_promotion(image));
    pop_active_dynamic_context(inner);
    assert(should_skip_continuous_shadow_promotion(image));
    ActiveDynamicCreationContext* extra[4]{};
    for (auto& item : extra) item = push_active_dynamic_context(key);
    assert(!should_skip_continuous_shadow_promotion(image)); // depth 5
    for (int i = 3; i >= 0; --i) pop_active_dynamic_context(extra[i]);
    assert(should_skip_continuous_shadow_promotion(image));
    pop_active_dynamic_context(outer);
}

void continuous_contention()
{
    spray_fixture();
    assert(try_lock_continuous_state());
    remember_dynamic_light_instance(image, 0, g_sceneGeneration);
    unlock_continuous_state();
    assert(!should_skip_continuous_shadow_promotion(image));
}

void continuous_interval_and_fail_open()
{
    spray_fixture();
    tick += g_continuousShadowIntervalMs;
    assert(!should_skip_continuous_shadow_promotion(image));
    assert(should_skip_continuous_shadow_promotion(image));
    g_untrackedDynamicCreations = 1;
    assert(!should_skip_continuous_shadow_promotion(image));
    g_untrackedDynamicCreations = 0;
    g_lifecycleSuspendActive = 1;
    assert(!should_skip_continuous_shadow_promotion(image));
}

bool advanceDuringCreate = false;
void* fake_create(void*, void*, void*, bool, bool, bool)
{
    if (advanceDuringCreate) ++g_sceneGeneration;
    return image + 0x5000;
}

void detached_load_association()
{
    reset();
    advanceDuringCreate = false;
    g_originalCreatePointLight = fake_create;
    g_fireColorPairDetachedBulbs = true;
    g_lifecycleSuspendActive = 1;
    u8 ligh[0x80]{};
    u8 ref[0x80]{};
    write_u32(ligh + kTESFormFormIDOffset, 0x800);
    write_u32(ref + kTESFormFormIDOffset, 0x1000);
    assert(cache_runtime_policy(0x800, kPolicyClassified));
    assert(cache_detached_fire_ref(0x1000));
    void* created = universal_create_point_light_hook(ligh, ref, nullptr, false, false, false);
    assert(detached_fire_instance_is_marked(0x800, created));
    // Pointer reuse without an eligible reference clears the old association.
    created = universal_create_point_light_hook(ligh, nullptr, nullptr, false, false, false);
    assert(!detached_fire_instance_is_marked(0x800, created));
    advanceDuringCreate = true;
    created = universal_create_point_light_hook(ligh, ref, nullptr, false, false, false);
    assert(!detached_fire_instance_is_marked(0x800, created));
    advanceDuringCreate = false;
}

void transition_discards_fire_anchors()
{
    reset();
    LightDataVectorRaw vector{};
    g_fireColorAnchorVector = &vector;
    g_fireColorAnchorCount = 1;
    g_pendingLightPolicy.lightData = image;
    begin_transition(false);
    clear_pending_after_transition_on_renderer_thread();
    assert(g_fireColorAnchorCount == 0 && g_fireColorAnchorVector == nullptr);
    assert(g_pendingLightPolicy.lightData == nullptr);
}

void bias_preserves_lower_and_nonfinite()
{
    reset();
    u8 params[0x40];
    const u32 cases[] = {0x3E800000, 0x3F800000, 0x40000000, 0x41C80000, 0xBF800000, 0x7F800000, 0x7FC00000};
    for (u32 value : cases) {
        std::memset(params, 0xA5, sizeof(params));
        write_u32(params + 0x14, value);
        const bool cap = value == 0x40000000 || value == 0x41C80000;
        assert(apply_shadow_bias_cap_to_params(params) == cap);
        assert(read_u32(params + 0x14) == (cap ? 0x3F800000u : value));
        for (u32 i = 0; i < sizeof(params); ++i) {
            if (i < 0x14 || i >= 0x18) assert(params[i] == 0xA5);
        }
    }
}

using Bytes = std::vector<u8>;
void append(Bytes& output, const Bytes& input) { output.insert(output.end(), input.begin(), input.end()); }
Bytes subrecord(u32 signature, const Bytes& payload)
{
    assert(payload.size() < 65536);
    Bytes result(6, 0);
    write_u32(result.data(), signature);
    result[4] = static_cast<u8>(payload.size());
    result[5] = static_cast<u8>(payload.size() >> 8);
    append(result, payload);
    return result;
}
Bytes record(u32 signature, u32 id, const Bytes& payload)
{
    Bytes result(24, 0);
    write_u32(result.data(), signature);
    write_u32(result.data() + 4, static_cast<u32>(payload.size()));
    write_u32(result.data() + 12, id);
    append(result, payload);
    return result;
}
Bytes group(u32 label, u32 type, const Bytes& payload)
{
    Bytes result(24, 0);
    write_u32(result.data(), kSigGRUP);
    write_u32(result.data() + 4, static_cast<u32>(payload.size() + 24));
    write_u32(result.data() + 8, label);
    write_u32(result.data() + 12, type);
    append(result, payload);
    return result;
}
Bytes edid(const char* name)
{
    return subrecord(kSigEDID, Bytes(name, name + std::strlen(name) + 1));
}
Bytes reference(u32 id, u32 base, float x)
{
    Bytes baseBytes(4, 0), position(24, 0);
    write_u32(baseBytes.data(), base);
    write_f32(position.data(), x);
    Bytes payload = subrecord(kSigNAME, baseBytes);
    append(payload, subrecord(kSigDATA, position));
    return record(kSigREFR, id, payload);
}
void fire_hazard_pairing_for_group_order(bool hazardFirst, bool coldHazard = false)
{
    reset();
    g_magicImpactLightsEnabled = true;
    g_fireColorPairDetachedBulbs = true;
    g_fireColorDetachedRequireWarm = true;
    g_fireColorKeywordCount = parse_keyword_list("Fire", g_fireColorKeywords, kMaxFireColorKeywords);
    u8 tesFile[0x500]{};
    std::strcpy(reinterpret_cast<char*>(tesFile + kTESFileNameOffset), "Fixture.esp");
    tesFile[kTESFileCompileIndexOffset] = 1;
    Bytes rgb(12, 0);
    rgb[8] = 255; rgb[9] = 140; rgb[10] = 60;
    Bytes light = edid("GenericWarmBulb");
    append(light, subrecord(kSigDATA, rgb));
    Bytes cold = rgb;
    cold[8] = 20; cold[9] = 100; cold[10] = 255;
    Bytes hazardLight = edid("GenericWarmBulb");
    append(hazardLight, subrecord(kSigDATA, coldHazard ? cold : rgb));
    Bytes lights = record(kSigLIGH, 0x800, hazardLight);
    append(lights, record(kSigLIGH, 0x802, light));
    Bytes hazardData(40, 0);
    write_u32(hazardData.data() + 0x1C, 0x800);
    const Bytes hazard = group(kSigHAZD, 0, record(kSigHAZD, 0x803, subrecord(kSigDATA, hazardData)));
    fileData = record(kSigTES4, 0, {});
    if (hazardFirst) append(fileData, hazard);
    append(fileData, group(kSigLIGH, 0, lights));
    if (!hazardFirst) append(fileData, hazard);
    append(fileData, group(kSigSTAT, 0, record(kSigSTAT, 0x801, edid("FireSource"))));
    Bytes references = reference(0x1001, 0x801, 0.0f);
    append(references, reference(0x1002, 0x800, 1.0f)); // Nearest: a warm fire-associated hazard is eligible.
    append(references, reference(0x1003, 0x802, 2.0f)); // Used when the hazard is cold/ineligible.
    append(fileData, group(kSigCELL, 0, group(0x900, 6, references)));
    assert(scan_owner_plugin(tesFile));
    assert((find_cached_runtime_policy(0x01000800) & kPolicyMagicImpact) != 0);
    assert(is_detached_fire_ref(0x01001002) == !coldHazard);
    assert(is_detached_fire_ref(0x01001003) == coldHazard);
}
void fire_hazard_pairing_ligh_first() { fire_hazard_pairing_for_group_order(false); }
void fire_hazard_pairing_hazd_first() { fire_hazard_pairing_for_group_order(true); }
void cold_hazard_keeps_authored() { fire_hazard_pairing_for_group_order(false, true); }

void fire_hazard_receives_standard_color()
{
    fire_hazard_pairing_for_group_order(false);
    u8 niLight[0x140]{};
    u8 bsLight[16]{};
    u8 light[96]{};
    u8 expected[96]{};
    const u32 form = 0x01000800;
    write_u32(niLight + kNiLightRuntimeDataOffsetSE1597 + kRuntimeFormIdOffset, form);
    cache_detached_fire_instance(form, niLight, true, g_sceneGeneration);
    const u64 key = make_runtime_instance_key(form, niLight, bsLight, g_sceneGeneration);
    AdmissionEntry* entry = find_admission_entry(key, true, g_sceneGeneration, tick);
    assert(entry);
    entry->state = kAdmissionReady;
    write_f32(light + kOffsetColorR, 1.0f);
    write_f32(light + kOffsetColorG, 0.8f);
    write_f32(light + kOffsetColorB, 0.2f);
    std::memcpy(expected, light, sizeof(light));
    assert(apply_consistent_fire_color(expected));
    g_originalProcessLight = [](void*, void*, void*, void*) {};
    universal_strict_process_light_hook(nullptr, light, bsLight, niLight);
    assert((g_pendingLightPolicy.policy & (kPolicyFireColor | kPolicyMagicImpact)) ==
           (kPolicyFireColor | kPolicyMagicImpact));
    assert(std::memcmp(light, expected, sizeof(light)) == 0);
    assert(effective_radius_cap_for_policy(g_pendingLightPolicy.policy) == 20.0f);
}
} // namespace test

extern "C" HANDLE CreateFileA(const char*, DWORD access, DWORD, void*, DWORD, DWORD, HANDLE)
{
    test::filePosition = 0;
    return access == GENERIC_READ_ && !test::fileData.empty() ? &test::fileData : invalid_handle_value();
}
extern "C" BOOL WriteFile(HANDLE, const void*, DWORD, DWORD*, void*) { return 0; }
extern "C" BOOL ReadFile(HANDLE file, void* output, DWORD bytes, DWORD* read, void*)
{
    if (file != &test::fileData || test::filePosition + bytes > test::fileData.size()) return 0;
    std::memcpy(output, test::fileData.data() + test::filePosition, bytes);
    test::filePosition += bytes;
    *read = bytes;
    return 1;
}
extern "C" BOOL CloseHandle(HANDLE) { return 1; }
extern "C" BOOL GetFileSizeEx(HANDLE file, LARGE_INTEGER_* size)
{
    if (file != &test::fileData) return 0;
    size->QuadPart = static_cast<i64>(test::fileData.size());
    return 1;
}
extern "C" BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER_ position, LARGE_INTEGER_*, DWORD origin)
{
    if (file != &test::fileData || origin != FILE_BEGIN_ || position.QuadPart < 0) return 0;
    test::filePosition = static_cast<SIZE_T>(position.QuadPart);
    return test::filePosition <= test::fileData.size();
}
extern "C" HMODULE GetModuleHandleA(const char*) { return nullptr; }
extern "C" DWORD GetPrivateProfileStringA(const char*, const char*, const char*, char*, DWORD, const char*) { return 0; }
extern "C" BOOL VirtualProtect(void* address, SIZE_T, DWORD protection, DWORD* oldProtection)
{
    *oldProtection = PAGE_EXECUTE_READ_;
    if (protection == PAGE_EXECUTE_READWRITE_) {
        for (auto& denial : test::denials) {
            if (address == denial.address && ++denial.writes >= denial.firstDeniedWrite) return 0;
        }
    }
    return 1;
}
extern "C" void* VirtualAlloc(void* requested, SIZE_T bytes, DWORD, DWORD)
{
    return requested ? test::relay : std::calloc(1, static_cast<size_t>(bytes));
}
extern "C" BOOL VirtualFree(void* address, SIZE_T, DWORD)
{
    if (address == test::relay) ++test::frees;
    else std::free(address);
    return 1;
}
extern "C" BOOL FlushInstructionCache(HANDLE, const void* address, SIZE_T)
{
    if (test::damagePush && address == test::effectsCall2) {
        test::push[1] ^= 1;
        test::damagePush = false;
    }
    return 1;
}
extern "C" HANDLE GetCurrentProcess() { return nullptr; }
extern "C" DWORD GetCurrentThreadId() { return 1; }
extern "C" u64 GetTickCount64() { return test::tick; }

int main(int argc, char** argv)
{
    struct Case { const char* name; void (*run)(); };
    const Case cases[] = {
        {"shadow_failed_rollback", test::shadow_failed_rollback},
        {"shadow_successful_rollback", test::shadow_successful_rollback},
        {"shadow_gate_denied", test::shadow_gate_denied},
        {"shadow_success", test::shadow_success},
        {"effects_partial_failed_rollback", test::effects_partial_failed_rollback},
        {"effects_partial_successful_rollback", test::effects_partial_successful_rollback},
        {"effects_final_failed_rollback", test::effects_final_failed_rollback},
        {"effects_final_successful_rollback", test::effects_final_successful_rollback},
        {"core_failed_rollback", test::core_failed_rollback},
        {"core_success", test::core_success},
        {"continuous_new_generation", test::continuous_new_generation},
        {"continuous_static_reuse", test::continuous_static_reuse},
        {"continuous_nested_and_overflow", test::continuous_nested_and_overflow},
        {"continuous_contention", test::continuous_contention},
        {"continuous_interval_and_fail_open", test::continuous_interval_and_fail_open},
        {"detached_load_association", test::detached_load_association},
        {"transition_discards_fire_anchors", test::transition_discards_fire_anchors},
        {"bias_preserves_lower_and_nonfinite", test::bias_preserves_lower_and_nonfinite},
        {"fire_hazard_pairing_ligh_first", test::fire_hazard_pairing_ligh_first},
        {"fire_hazard_pairing_hazd_first", test::fire_hazard_pairing_hazd_first},
        {"cold_hazard_keeps_authored", test::cold_hazard_keeps_authored},
        {"fire_hazard_receives_standard_color", test::fire_hazard_receives_standard_color},
    };
    unsigned passed = 0;
    for (const auto& item : cases) {
        if (argc > 1 && std::strcmp(argv[1], item.name) != 0) continue;
        item.run();
        std::printf("PASS %s\n", item.name);
        ++passed;
    }
    assert(passed != 0);
    std::printf("%u runtime regression checks passed\n", passed);
}
