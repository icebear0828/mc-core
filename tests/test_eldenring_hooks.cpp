#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gtest/gtest.h>

#include "eldenring_hooks.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_damage.hpp"
#include "eldenring_blocks.hpp"
#include "eldenring_buddy.hpp"
#include "eldenring_combat_service.hpp"
#include "eldenring_bullet.hpp"
#include "eldenring_singletons.hpp"
#include "eldenring_hotkey_dispatcher.hpp"
#include "mc/contracts/combat_adapter.hpp"

using namespace eldenring;
using namespace eldenring::hooks;

namespace {

TEST(EldenRingHooksTest, OutcomeNameMapping) {
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::Applied), "Applied");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::Deferred), "Deferred");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::Expired), "Expired");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::NoTemplate), "NoTemplate");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::NoPlayer), "NoPlayer");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::VictimGone), "VictimGone");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::WrongModule), "WrongModule");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::VictimDead), "VictimDead");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::NotHostile), "NotHostile");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::BadDamage), "BadDamage");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::NoLineOfSight), "NoLineOfSight");
}

TEST(EldenRingHooksTest, BlockSoundSelection) {
    // Stone maps to stone.step
    EXPECT_STREQ(blocks::blockSoundName(mc::BlockId::Stone), "block.stone.step");
    // Dirt/Gravel maps to gravel.step
    EXPECT_STREQ(blocks::blockSoundName(mc::BlockId::Dirt), "block.gravel.step");
    // Default grass/wood/tnt
    EXPECT_STREQ(blocks::blockSoundName(mc::BlockId::Wood), "block.grass.step");
    EXPECT_STREQ(blocks::blockSoundName(mc::BlockId::Tnt), "block.grass.step");
}

TEST(EldenRingHooksTest, KaidenWarriorSummonResolution) {
    EXPECT_EQ(buddy::kKaidenWarriorRequest, 231000);
    EXPECT_EQ(buddy::resolveSummonRequest("kaiden"), 231000);
    EXPECT_EQ(buddy::resolveSummonRequest("wolves"), 232000);
}

TEST(EldenRingHooksTest, BulletConstantsAndTemplateValidation) {
    auto& combat = combat::CombatService::Instance();
    // Initially bullet template is not loaded in unit test environment
    EXPECT_FALSE(combat.hasValidBulletTemplate());
    EXPECT_FLOAT_EQ(bullet::kCrossbowDamage, 9.0f);
}

TEST(EldenRingHooksTest, ReadSingletonGuardsZeroRva) {
    class FailingReader : public live::IMemoryReader {
    public:
        bool read(uintptr_t, void*, size_t) const override {
            ADD_FAILURE() << "read must not be called when global_rva is 0!";
            return false;
        }
    } reader;

    const uintptr_t result = live::readSingleton(reader, 0x140000000, 0, live::sigs::kWorldChrMan);
    EXPECT_EQ(result, 0u);
}

TEST(EldenRingHooksTest, MemoryContextRvasStorage) {
    auto& ctx = RuntimeContext::Instance();
    ctx.memory().rva_world.store(0x123456);
    ctx.memory().rva_menu.store(0x234567);
    ctx.memory().rva_loading.store(0x345678);
    ctx.memory().rva_fade.store(0x456789);

    EXPECT_EQ(ctx.memory().rva_world.load(), 0x123456u);
    EXPECT_EQ(ctx.memory().rva_menu.load(), 0x234567u);
    EXPECT_EQ(ctx.memory().rva_loading.load(), 0x345678u);
    EXPECT_EQ(ctx.memory().rva_fade.load(), 0x456789u);
}

TEST(EldenRingHooksTest, HotkeyDispatcherHandlesCallbacksSafely) {
    input::HotkeyCallbacks cb;
    bool invoked = false;
    cb.inventory_tick = [&](bool) { invoked = true; };
    input::HotkeyDispatcher::Instance().setCallbacks(cb);
    EXPECT_TRUE(input::HotkeyDispatcher::Instance().callbacks().inventory_tick != nullptr);
    input::HotkeyDispatcher::Instance().callbacks().inventory_tick(true);
    EXPECT_TRUE(invoked);
}

} // namespace
