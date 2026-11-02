// Persists the player's inventory, armor, offhand, cursor, selected hotbar slot and crafting grid
// to mods\mc_adapter\inventory.txt. The game gives the DLL no exit hook, so a worker thread writes
// whenever the serialized state changed; the loader reads the file once before the key thread starts.

#include "core/loader_internal.hpp"

#include "mc/inventory_persistence.hpp"

namespace eldenring::loader_detail {
namespace {

std::string InventorySavePath() {
    return g_game_dir + "mods\\mc_adapter\\inventory.txt";
}

void EnsureSaveDirectory() {
    CreateDirectoryA((g_game_dir + "mods").c_str(), nullptr);
    CreateDirectoryA((g_game_dir + "mods\\mc_adapter").c_str(), nullptr);
}

bool WriteAll(const std::string& path, const std::string& text) {
    // Write a temporary file and replace it, so a crash mid-write never leaves half a save behind.
    const std::string tmp = path + ".tmp";
    FILE* f = nullptr;
    if (fopen_s(&f, tmp.c_str(), "wb") != 0 || f == nullptr) return false;
    const size_t written = fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    if (written != text.size()) {
        DeleteFileA(tmp.c_str());
        return false;
    }
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp.c_str());
        return false;
    }
    return true;
}

std::string Snapshot() {
    // Same lock order as InventoryTick: the melee mutex first, then the crafting grid.
    std::lock_guard<std::mutex> melee(g_melee_mutex);
    std::lock_guard<std::mutex> craft(g_ui.craft_mutex);
    return mc::serializeInventory(g_melee.inventory(), g_melee.selectedSlot(), g_ui.craft.grid());
}

} // namespace

void LoadInventorySave() {
    const std::string path = InventorySavePath();
    if (!FileExists(path)) return;
    std::vector<uint8_t> bytes;
    if (!ReadFileAll(path, bytes)) {
        Log("inventory: could not read %s", path.c_str());
        return;
    }
    const std::string text(bytes.begin(), bytes.end());

    std::lock_guard<std::mutex> melee(g_melee_mutex);
    std::lock_guard<std::mutex> craft(g_ui.craft_mutex);
    mc::Inventory inv = g_melee.inventory();
    mc::CraftingGrid grid = g_ui.craft.grid();
    int selected = g_melee.selectedSlot();
    const mc::InventoryLoadResult r = mc::deserializeInventory(text, inv, selected, grid);
    if (!r.ok) {
        Log("inventory: save ignored (%s)", r.error.c_str());
        return;
    }
    g_melee.inventory() = inv;
    g_melee.select(selected);
    g_ui.craft.setGrid(grid);
    Log("inventory: restored %d stack(s), %d line(s) skipped, selected slot %d, grid cells %d", r.loaded, r.skipped, selected,
        grid.occupied());
}

DWORD WINAPI InventorySaveThread(LPVOID) {
    EnsureSaveDirectory();
    std::string last;
    uint64_t last_log_ms = 0;
    for (;;) {
        Sleep(1000);
        std::string text = Snapshot();
        if (text == last) continue;
        if (!WriteAll(InventorySavePath(), text)) continue;
        last = std::move(text);
        const uint64_t now = GetTickCount64();
        if (now - last_log_ms > 10000) {
            last_log_ms = now;
            Log("inventory: saved to %s", InventorySavePath().c_str());
        }
    }
    return 0;
}

} // namespace eldenring::loader_detail
