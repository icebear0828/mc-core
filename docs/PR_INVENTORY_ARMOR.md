## Summary
This PR implements **Batch 1** of the inventory, armor, and elytra expansion roadmap for Elden Ring:
- **Armor Slots in `Inventory`**: Adds 4 dedicated armor slots (`Helmet`, `Chestplate`, `Leggings`, `Boots`) with strict slot-type matching constraints (`getArmorSlot`), shift-click auto-equipping/unequipping, and cursor swap. Elytra (`ItemId::Elytra`) is supported as a chestplate item.
- **Diamond Armor Set**: Expands `ItemId` with `DiamondHelmet`, `DiamondChestplate`, `DiamondLeggings`, `DiamondBoots`. Stack sizes are constrained to 1.
- **Damage Reduction**: Implements Minecraft-authentic armor defense points (full diamond set = 20 points -> 80% physical damage reduction). Hooked into `PlayerHitSurvival` (`ProcessDamageContext`) in the Elden Ring adapter.
- **ImGui D3D12 Inventory UI**: Renders the 4 vertical armor slots above the offhand slot on the left of the container panel, complete with border, header label, and empty slot ghost icon watermarks (Helmet, Chest, Leggings, Boots).
- **HUD Atlas Integration**: Extracted authentic icons and slot watermarks into `mc_hud_atlas.png` (65 UV regions).

## Testing
- GoogleTest: 32/32 Inventory tests pass, 353/353 Elden Ring adapter tests pass.
- Pytest: 39/39 asset extraction and atlas verification tests pass.
- Physical Deployment: Built `dinput8.dll` (SHA256: `94E97F855BFC4608D9C848069E2E578B3BA4D86D3AE085C0E1C8084E1EFB4FBD`) and deployed to Elden Ring directory alongside updated `mc_hud_atlas.png`.
