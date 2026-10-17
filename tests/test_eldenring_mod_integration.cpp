#include <gtest/gtest.h>
#include "eldenring_runtime_context.hpp"

namespace eldenring::test {

class TestEldenRingMod : public mc::contracts::IModInstance {
public:
    explicit TestEldenRingMod(std::string name, int priority = 100)
        : name_(std::move(name)), priority_(priority) {}

    const char* name() const override { return name_.c_str(); }
    int priority() const override { return priority_; }

    bool onInitialize(mc::contracts::CoreHostServices& /*services*/) override {
        initialized_ = true;
        return true;
    }

    void onShutdown() override {
        shutdown_ = true;
    }

    void onTick(float dt) override {
        ticks_++;
        last_dt_ = dt;
    }

    void onRender2D(void* /*draw_list*/, float /*w*/, float /*h*/) override {
        rendered_2d_ = true;
    }

    void onRender3D(void* /*cmd_list*/, const void* /*camera*/) override {
        rendered_3d_ = true;
    }

    bool onInput(const mc::contracts::ModInputEvent& event) override {
        if (event.key_code == consume_key_) {
            consumed_keys_count_++;
            return true;
        }
        return false;
    }

    bool onPlayerDamage(mc::contracts::ModDamageContext& ctx) override {
        if (shield_active_) {
            ctx.damage = 0.0f; // 100% block
            return true;       // Cancel damage event
        }
        return false;
    }

    std::string name_;
    int priority_{100};
    bool initialized_{false};
    bool shutdown_{false};
    int ticks_{0};
    float last_dt_{0.0f};
    bool rendered_2d_{false};
    bool rendered_3d_{false};
    uint32_t consume_key_{0};
    int consumed_keys_count_{0};
    bool shield_active_{false};
};

class EldenRingModIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        RuntimeContext::Instance().resetForTesting();
    }
    void TearDown() override {
        RuntimeContext::Instance().resetForTesting();
    }
};

TEST_F(EldenRingModIntegrationTest, ContextHostRegistrationAndTick) {
    auto& ctx = RuntimeContext::Instance();
    auto mc_mod = std::make_shared<TestEldenRingMod>("minecraft", 100);

    EXPECT_TRUE(ctx.mods().registerMod(mc_mod));
    EXPECT_EQ(ctx.mods().modCount(), 1u);

    mc::contracts::CoreHostServices services{};
    EXPECT_TRUE(ctx.mods().initializeAll(services));
    EXPECT_TRUE(mc_mod->initialized_);

    ctx.mods().tick(0.016f);
    EXPECT_EQ(mc_mod->ticks_, 1);
    EXPECT_FLOAT_EQ(mc_mod->last_dt_, 0.016f);
}

TEST_F(EldenRingModIntegrationTest, MultiModInputArbitrationInEldenRing) {
    auto& ctx = RuntimeContext::Instance();
    auto cs_mod = std::make_shared<TestEldenRingMod>("counter_strike", 200);
    auto mc_mod = std::make_shared<TestEldenRingMod>("minecraft", 100);

    // CS Mod claims 'F' for knife inspection
    cs_mod->consume_key_ = 'F';

    ctx.mods().registerMod(mc_mod);
    ctx.mods().registerMod(cs_mod);

    mc::contracts::CoreHostServices services{};
    ctx.mods().initializeAll(services);

    // Dispatch 'F' key event
    mc::contracts::ModInputEvent inspect_event{'F', true};
    bool consumed = ctx.mods().dispatchInput(inspect_event);

    EXPECT_TRUE(consumed);
    EXPECT_EQ(cs_mod->consumed_keys_count_, 1);
    EXPECT_EQ(mc_mod->consumed_keys_count_, 0); // Mc mod does not receive it
}

TEST_F(EldenRingModIntegrationTest, ModDamageInterceptionInEldenRing) {
    auto& ctx = RuntimeContext::Instance();
    auto mc_mod = std::make_shared<TestEldenRingMod>("minecraft", 100);
    mc_mod->shield_active_ = true;

    ctx.mods().registerMod(mc_mod);
    mc::contracts::CoreHostServices services{};
    ctx.mods().initializeAll(services);

    mc::contracts::ModDamageContext hit_ctx{250.0f, false};
    bool cancelled = ctx.mods().filterPlayerDamage(hit_ctx);

    EXPECT_TRUE(cancelled);
    EXPECT_FLOAT_EQ(hit_ctx.damage, 0.0f);
}

TEST_F(EldenRingModIntegrationTest, ContextResetCleansUpMods) {
    auto& ctx = RuntimeContext::Instance();
    auto test_mod = std::make_shared<TestEldenRingMod>("test_mod", 50);

    ctx.mods().registerMod(test_mod);
    mc::contracts::CoreHostServices services{};
    ctx.mods().initializeAll(services);

    EXPECT_FALSE(test_mod->shutdown_);

    // Simulating session end / death reload
    ctx.resetForTesting();
    EXPECT_TRUE(test_mod->shutdown_);
}

} // namespace eldenring::test
