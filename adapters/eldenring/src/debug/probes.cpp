#include "eldenring_debug_probes.hpp"

#if defined(WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include "eldenring_buddy.hpp"
#include "eldenring_camera.hpp"
#include "eldenring_chrscan.hpp"
#include "eldenring_damage.hpp"
#include "eldenring_diagnostics.hpp"
#include "eldenring_memory_scanner.hpp"
#include "eldenring_model.hpp"
#include "eldenring_singletons.hpp"
#include "eldenring_steve.hpp"
#include "eldenring_world.hpp"

namespace eldenring::debug {

using diagnostics::Log;
using memory::SafeCopy;
using namespace eldenring::live;

static CamWatchHit s_watch_ring[256];
static std::atomic<unsigned> s_watch_head{0};

LONG CALLBACK CamWatchVeh(PEXCEPTION_POINTERS info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || (info->ContextRecord->Dr6 & 0xF) == 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const unsigned idx = s_watch_head.fetch_add(1) % 256;
    CamWatchHit& h = s_watch_ring[idx];
    h.rip = info->ContextRecord->Rip;
    const uint64_t* sp = reinterpret_cast<const uint64_t*>(info->ContextRecord->Rsp);
    for (int i = 0; i < 6; ++i) {
        uint64_t v = 0;
        SafeCopy(reinterpret_cast<uintptr_t>(sp + i), &v, sizeof(v));
        h.stack[i] = v;
    }
    info->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void ArmThreadWatch(DWORD tid, uintptr_t addr) {
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, tid);
    if (th == nullptr) return;
    if (SuspendThread(th) != static_cast<DWORD>(-1)) {
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(th, &ctx)) {
            ctx.Dr0 = addr;
            ctx.Dr1 = addr + 8;
            ctx.Dr7 = 0x1ull | (1ull << 16) | (3ull << 18) | (1ull << 2) | (1ull << 20) | (3ull << 22);
            SetThreadContext(th, &ctx);
        }
        ResumeThread(th);
    }
    CloseHandle(th);
}

static bool ChrCamPosAddressHelper(uintptr_t& addr) {
    memory::SelfReader reader;
    memory::ImageInfo img;
    memory::ReadImageInfo(img);
    const uintptr_t world = readSingleton(reader, img.base, 0, sigs::kWorldChrMan);
    uint64_t cam = 0;
    if (world == 0 || !SafeCopy(world + layout::kChrCamInWorldChrMan, &cam, sizeof(cam)) || cam == 0) return false;
    addr = static_cast<uintptr_t>(cam) + layout::kCamMatrix + 0x30;
    return true;
}

DWORD WINAPI CamWatchThread(LPVOID) {
    AddVectoredExceptionHandler(1, CamWatchVeh);
    std::unordered_map<DWORD, uintptr_t> armed;
    std::unordered_map<uint64_t, unsigned> counts;
    unsigned printed_head = 0, rounds = 0;
    memory::ImageInfo img;
    memory::ReadImageInfo(img);

    for (;;) {
        Sleep(1000);
        ++rounds;
        uintptr_t addr = 0;
        if (ChrCamPosAddressHelper(addr)) {
            if ((addr & 7) != 0) {
                Log("camwatch: position address %p is not 8-aligned, watch not armed", reinterpret_cast<void*>(addr));
            } else {
                const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
                if (snap != INVALID_HANDLE_VALUE) {
                    THREADENTRY32 te{};
                    te.dwSize = sizeof(te);
                    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
                        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
                        auto it = armed.find(te.th32ThreadID);
                        if (it != armed.end() && it->second == addr) continue;
                        ArmThreadWatch(te.th32ThreadID, addr);
                        armed[te.th32ThreadID] = addr;
                    }
                    CloseHandle(snap);
                }
                if (rounds % 10 == 1) Log("camwatch: armed %zu threads on ChrCam pos %p", armed.size(), reinterpret_cast<void*>(addr));
            }
        }
        const unsigned head = s_watch_head.load();
        for (; printed_head < head && printed_head + 256 > head; ++printed_head) {
            const CamWatchHit& h = s_watch_ring[printed_head % 256];
            if (++counts[h.rip] == 1) {
                Log("camwatch: NEW writer rip=%p (RVA 0x%llX) stack: %llX %llX %llX %llX %llX %llX",
                    reinterpret_cast<void*>(h.rip),
                    static_cast<unsigned long long>(h.rip - img.base),
                    static_cast<unsigned long long>(h.stack[0] - img.base),
                    static_cast<unsigned long long>(h.stack[1] - img.base),
                    static_cast<unsigned long long>(h.stack[2] - img.base),
                    static_cast<unsigned long long>(h.stack[3] - img.base),
                    static_cast<unsigned long long>(h.stack[4] - img.base),
                    static_cast<unsigned long long>(h.stack[5] - img.base));
            }
        }
        if (rounds % 10 == 0) {
            for (const auto& kv : counts) Log("camwatch: writer RVA 0x%llX hit %u times", static_cast<unsigned long long>(kv.first - img.base), kv.second);
        }
    }
}

void LogGroundBytesOnChange(const live::IMemoryReader& reader, uintptr_t base, uintptr_t world) {
    static uint8_t last[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint64_t player = 0;
    uint8_t b[4];
    if (world == 0 || !SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0 ||
        !detail::readGroundBytes(reader, base, static_cast<uintptr_t>(player), b)) {
        return;
    }
    if (std::memcmp(b, last, 4) == 0) return;
    std::memcpy(last, b, 4);
    Log("ground: 92=%u 93=%u 1D0=%u 1D1=%u", b[0], b[1], b[2], b[3]);
}

void LogPhysicsVectors(const live::IMemoryReader& reader, uintptr_t base, uintptr_t world) {
    static uint64_t last_ms = 0;
    const uint64_t now = GetTickCount64();
    if (now - last_ms < 500) return;
    last_ms = now;
    uint64_t player = 0;
    float p70[3], p80[3], p120[3];
    if (world == 0 || !SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0) return;
    const auto chr = static_cast<uintptr_t>(player);
    if (!detail::readPhysicsVec3(reader, base, chr, 0x70, p70) ||
        !detail::readPhysicsVec3(reader, base, chr, 0x80, p80) ||
        !detail::readPhysicsVec3(reader, base, chr, 0x120, p120)) {
        return;
    }
    Log("phys: +70=(%.3f %.3f %.3f) +80=(%.3f %.3f %.3f) +120=(%.3f %.3f %.3f)",
        p70[0], p70[1], p70[2], p80[0], p80[1], p80[2], p120[0], p120[1], p120[2]);
}

void LogPlayerPoseForBuddy(const live::IMemoryReader& reader, uintptr_t base, uintptr_t player) {
    float pos[3], q[4];
    if (player != 0 && detail::readPhysicsPosition(reader, base, player, pos) &&
        detail::readPhysicsOrientation(reader, base, player, q)) {
        Log("buddy: player pos=(%.2f %.2f %.2f) heading=%.2f", pos[0], pos[1], pos[2],
            render::yawFromQuat(q[0], q[1], q[2], q[3]));
    }
}

DWORD WINAPI BuddyThread(LPVOID) {
    buddy::Monitor monitor;
    uint64_t last_check_ms = 0;
    bool armed = false;
    memory::SelfReader reader;
    memory::ImageInfo img;
    memory::ReadImageInfo(img);

    for (;;) {
        const uint64_t now = GetTickCount64();
        if (now - last_check_ms >= 1000) {
            last_check_ms = now;
            DWORD attr = GetFileAttributesA((diagnostics::GetGameDir() + "mc_er_buddylog.txt").c_str());
            const bool file = (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
            if (file && !armed) monitor = buddy::Monitor{};
            armed = file;
        }
        if (!armed) {
            Sleep(1000);
            continue;
        }
        const std::string text = monitor.update(buddy::sample(reader, img.base));
        if (!text.empty()) {
            Log("%s", text.c_str());
            const uintptr_t world = readSingleton(reader, img.base, 0, sigs::kWorldChrMan);
            uint64_t player = 0;
            if (world != 0 && SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) && player != 0) {
                LogPlayerPoseForBuddy(reader, img.base, static_cast<uintptr_t>(player));
            }
        }
        YieldProcessor();
        SwitchToThread();
    }
}

void SnapshotNativeParts([[maybe_unused]] const live::IMemoryReader& reader, [[maybe_unused]] uintptr_t base, uintptr_t world,
                         const RuntimeContext& ctx, const std::unordered_map<uintptr_t, HiddenWord>& hidden_flags,
                         std::mutex& native_mutex) {
    uint64_t player = 0, model = 0;
    if (world == 0 || !SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0 ||
        !SafeCopy(static_cast<uintptr_t>(player) + layout::kAsmModelInPlayerIns, &model, sizeof(model)) || model == 0) {
        Log("snapshot: no player model");
        return;
    }
    Log("snapshot: model=%p first_person=%d hide_wanted=%d slots=0x%X mask1=0x%X mask2=0x%X",
        reinterpret_cast<void*>(model),
        ctx.mode().first_person.load() ? 1 : 0,
        ctx.ui().native_hide_wanted.load() ? 1 : 0,
        ctx.ui().hide_slots.load(),
        ctx.ui().hide_mask1.load(),
        ctx.ui().hide_mask2.load());

    std::lock_guard<std::mutex> lock(native_mutex);
    for (unsigned i = 0; i < layout::kAsmPartSlots; ++i) {
        uint64_t part = 0, disp = 0;
        uint32_t f1 = 0, f2 = 0;
        if (!SafeCopy(static_cast<uintptr_t>(model) + layout::kAsmPartPointers + i * 8, &part, sizeof(part)) || part == 0) continue;
        if (!SafeCopy(static_cast<uintptr_t>(part) + layout::kPartDispEntity, &disp, sizeof(disp)) || disp == 0) {
            Log("snapshot: slot %2u part=%p (no disp entity)", i, reinterpret_cast<void*>(part));
            continue;
        }
        SafeCopy(static_cast<uintptr_t>(disp) + layout::kDispFlags1, &f1, sizeof(f1));
        SafeCopy(static_cast<uintptr_t>(disp) + layout::kDispFlags2, &f2, sizeof(f2));
        const bool ours = hidden_flags.count(static_cast<uintptr_t>(disp) + layout::kDispFlags1) != 0;
        Log("snapshot: slot %2u part=%p disp=%p flags1=%08X flags2=%08X drawn=%d tracked=%d", i, reinterpret_cast<void*>(part),
            reinterpret_cast<void*>(disp), f1, f2, (f1 & layout::kDispVisibleBit) ? 1 : 0, ours ? 1 : 0);
    }
}

void CycleHiddenSlot(int& cursor, uintptr_t player, const live::IMemoryReader& reader, uintptr_t base,
                     std::atomic<uint32_t>& hide_slots, std::atomic<int>& slot_probe, std::atomic<bool>& slots_changed) {
    if (player == 0) return;
    int next = cursor;
    for (int tries = 0; tries <= static_cast<int>(layout::kAsmPartSlots); ++tries) {
        next = next + 1;
        if (next >= static_cast<int>(layout::kAsmPartSlots)) {
            next = -1;
            break;
        }
        if (!collectDispFlagAddresses(reader, base, player, 1u << next).empty()) break;
    }
    cursor = next;
    hide_slots.store(next < 0 ? 0xFFFFFFFFu : (1u << next));
    slot_probe.store(next);
    slots_changed.store(true);
    if (next < 0) {
        Log("slots: hiding ALL part slots");
    } else {
        Log("slots: hiding ONLY part slot %d (mask 0x%X)", next, 1u << next);
    }
}

void LogHitGeometry(const char* tag, void* attacker, uintptr_t player, const uint8_t* ctx,
                    const live::IMemoryReader& reader, uintptr_t base) {
    constexpr size_t kVecOffsets[4] = {0x190, 0x1A0, 0x1B0, 0x1C0};
    float v[4][4] = {};
    for (int i = 0; i < 4; ++i) SafeCopy(reinterpret_cast<uintptr_t>(ctx) + kVecOffsets[i], v[i], sizeof(v[i]));
    uint8_t kind = 0, d9 = 0, react = 0;
    float kb = 0.f;
    uint64_t ctx_attacker = 0;
    SafeCopy(reinterpret_cast<uintptr_t>(ctx) + 0xDA, &kind, 1);
    SafeCopy(reinterpret_cast<uintptr_t>(ctx) + 0xD9, &d9, 1);
    SafeCopy(reinterpret_cast<uintptr_t>(ctx) + layout::kHitEnableReaction, &react, 1);
    SafeCopy(reinterpret_cast<uintptr_t>(ctx) + layout::kHitKnockbackIn, &kb, sizeof(kb));
    SafeCopy(reinterpret_cast<uintptr_t>(ctx) + layout::kHitAttacker, &ctx_attacker, sizeof(ctx_attacker));
    auto vtable_rva = [&](uintptr_t object) -> long long {
        uint64_t vt = 0;
        if (object == 0 || !SafeCopy(object, &vt, sizeof(vt)) || vt < base) return -1;
        return static_cast<long long>(vt - base);
    };
    float pp[3] = {}, ap[3] = {};
    const bool ok_player = player != 0 && detail::readPhysicsPosition(reader, base, player, pp);
    const bool ok_attacker = attacker != nullptr && detail::readPhysicsPosition(reader, base, reinterpret_cast<uintptr_t>(attacker), ap);
    Log("%s: kind=%u d9=%u react=%u kb=%.2f attacker=%p vt=0x%llX ctx_attacker=%p vt=0x%llX player=%s(%.2f %.2f %.2f) attacker_pos=%s(%.2f %.2f %.2f) "
        "v190=(%.3f %.3f %.3f %.1f) v1A0=(%.3f %.3f %.3f %.1f) v1B0=(%.3f %.3f %.3f %.1f) v1C0=(%.3f %.3f %.3f %.1f)",
        tag, kind, d9, react, kb, attacker, static_cast<unsigned long long>(vtable_rva(reinterpret_cast<uintptr_t>(attacker))),
        reinterpret_cast<void*>(ctx_attacker), static_cast<unsigned long long>(vtable_rva(static_cast<uintptr_t>(ctx_attacker))),
        ok_player ? "ok" : "no", pp[0], pp[1], pp[2], ok_attacker ? "ok" : "no", ap[0], ap[1], ap[2], v[0][0], v[0][1], v[0][2], v[0][3], v[1][0], v[1][1],
        v[1][2], v[1][3], v[2][0], v[2][1], v[2][2], v[2][3], v[3][0], v[3][1], v[3][2], v[3][3]);
}

void LogPlayerHit(void* module, void* attacker, const uint8_t* ctx, uint8_t blocked_flag,
                  const live::IMemoryReader& reader, uintptr_t base, uintptr_t world) {
    uint64_t owner = 0, player = 0;
    if (!SafeCopy(reinterpret_cast<uintptr_t>(module) + layout::kOwnerInDataModule, &owner, sizeof(owner)) || owner == 0) return;
    if (world == 0 || !SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || owner != player) return;
    uint8_t b[0x270];
    if (!SafeCopy(reinterpret_cast<uintptr_t>(ctx), b, sizeof(b))) return;
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, b + o, 4); return v; };
    auto f32 = [&](size_t o) { float v; std::memcpy(&v, b + o, 4); return v; };
    int32_t npc = 0;
    uint8_t team = 0;
    SafeCopy(reinterpret_cast<uintptr_t>(attacker) + layout::kNpcIdInChrIns, &npc, sizeof(npc));
    SafeCopy(reinterpret_cast<uintptr_t>(attacker) + layout::kTeamTypeInChrIns, &team, sizeof(team));
    Log("HIT-IN: dmg=%u poise=%u tier=%u kb+FC=%.2f f+50=%.2f %.2f %.2f u8[67]=%u u8[D9]=%u u8[DA]=%u u8[114]=%02X u8[115]=%02X u32[54]=%u "
        "u32[21C]=%u u32[230]=%u blocked_arg=%u attacker=%p npc=%d team=%u",
        u32(layout::kHitDamage), u32(0x40), u32(0x44), f32(layout::kHitKnockbackIn), f32(0x50), f32(0x54), f32(0x58), b[0x67], b[0xD9], b[0xDA],
        b[0x114], b[0x115], u32(0x54), u32(0x21C), u32(0x230), static_cast<unsigned>(blocked_flag), attacker, npc, static_cast<unsigned>(team));
    uint32_t words[6] = {};
    uint64_t container = 0, data = 0;
    if (SafeCopy(owner + layout::kModuleContainerInChrIns, &container, sizeof(container)) && container != 0 &&
        SafeCopy(static_cast<uintptr_t>(container) + layout::kChrDataModuleSlot * sizeof(uint64_t), &data, sizeof(data)) && data != 0) {
        SafeCopy(static_cast<uintptr_t>(data) + 0x148, words, sizeof(words));
    }
    auto u16 = [&](size_t o) { uint16_t v; std::memcpy(&v, b + o, 2); return static_cast<unsigned>(v); };
    Log("HIT-IN2: u32[22C]=%u u32[228]=%u anim[220]=%u [222]=%u [224]=%u [226]=%u u8[258]=%u u8[259]=%u u8[25A]=%u u8[266]=%02X u8[267]=%02X "
        "module+148..15C=%u %u %u %u %u %u",
        u32(0x22C), u32(0x228), u16(0x220), u16(0x222), u16(0x224), u16(0x226), b[0x258], b[0x259], b[0x25A], b[0x266], b[0x267], words[0], words[1],
        words[2], words[3], words[4], words[5]);
    LogHitGeometry("HIT-GEOM", attacker, static_cast<uintptr_t>(owner), ctx, reader, base);
}

void LogChrScan(uintptr_t chr, size_t index, const live::IMemoryReader& reader, uintptr_t base, uintptr_t player) {
    static unsigned s_scanned = 0;
    if (++s_scanned > 3) return;
    if (s_scanned == 1 && player != 0) {
        const std::vector<uintptr_t> player_flags = collectDispFlagAddresses(reader, base, player);
        Log("chrscan: the player has %zu drawn parts; first dumps (disp_flags1 sits at +0x20 of the entity)", player_flags.size());
        for (size_t i = 0; i < player_flags.size() && i < 3; ++i) {
            Log("chrscan:   player part %zu %s", i, dumpDwords(reader, player_flags[i] - 0x20, 0x10, 0x50).c_str());
        }
    }
    Log("chrscan: entity #%zu chr=%p", index, reinterpret_cast<void*>(chr));
    for (const ScanHit& h : scanForClasses(reader, base, chr, 0, 0xA00)) {
        Log("%s", formatScanHit(h, 0).c_str());
        if (!looksLikeModelClass(h.cls)) continue;
        for (const ScanHit& h2 : scanForClasses(reader, base, h.object, 0, 0x300, 40)) {
            Log("%s", formatScanHit(h2, 1).c_str());
            if (h2.cls.find("CSModelDispEntity") != std::string::npos) {
                Log("chrscan:     dump %s", dumpDwords(reader, h2.object, 0x10, 0x50).c_str());
            }
            if (!looksLikeModelClass(h2.cls)) continue;
            for (const uintptr_t at : findDwordOffsets(reader, h2.object, 0, 0x100, 0x000100A1u)) {
                Log("chrscan:     display flags 0x000100A1 at +0x%llX of %p (chr+0x%llX -> +0x%llX)",
                    static_cast<unsigned long long>(at), reinterpret_cast<void*>(h2.object),
                    static_cast<unsigned long long>(h.offset), static_cast<unsigned long long>(h2.offset));
            }
        }
        for (const uintptr_t at : findDwordOffsets(reader, h.object, 0, 0x100, 0x000100A1u)) {
            Log("chrscan:   display flags 0x000100A1 at +0x%llX of %p (chr+0x%llX)",
                static_cast<unsigned long long>(at), reinterpret_cast<void*>(h.object),
                static_cast<unsigned long long>(h.offset));
        }
    }
}

} // namespace eldenring::debug
#endif
