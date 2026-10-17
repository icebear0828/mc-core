#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// Target header to be implemented
#include "mc/contracts/mod_interface.hpp"
#include "mc/contracts/mod_manager.hpp"

namespace mc::test {

class TrackingMod : public contracts::IModInstance {
public:
    TrackingMod(std::string name, int priority)
        : name_(std::move(name)), priority_(priority) {}

    const char* name() const override { return name_.c_str(); }
    int priority() const override { return priority_; }

    bool onInitialize(contracts::CoreHostServices& /*services*/) override {
        init_called_ = true;
        execution_order_.push_back(name_ + ":init");
        return init_result_;
    }

    void onShutdown() override {
        shutdown_called_ = true;
        execution_order_.push_back(name_ + ":shutdown");
    }

    void onTick(float dt) override {
        tick_count_++;
        last_dt_ = dt;
        execution_order_.push_back(name_ + ":tick");
    }

    void onRender2D(void* draw_list, float w, float h) override {
        render2d_called_ = true;
        (void)draw_list; (void)w; (void)h;
        execution_order_.push_back(name_ + ":render2d");
    }

    void onRender3D(void* cmd_list, const void* camera) override {
        render3d_called_ = true;
        (void)cmd_list; (void)camera;
        execution_order_.push_back(name_ + ":render3d");
    }

    bool onInput(const contracts::ModInputEvent& event) override {
        input_events_received_.push_back(event.key_code);
        if (event.key_code == consume_key_) {
            execution_order_.push_back(name_ + ":consumed_input");
            return true; // Consume event, truncate propagation
        }
        execution_order_.push_back(name_ + ":passed_input");
        return false;
    }

    bool onPlayerDamage(contracts::ModDamageContext& ctx) override {
        damage_events_received_++;
        if (absorb_ratio_ > 0.0f) {
            ctx.damage *= (1.0f - absorb_ratio_);
            execution_order_.push_back(name_ + ":absorbed_damage");
        }
        return cancel_damage_;
    }

    // Test inspection helpers
    std::string name_;
    int priority_{0};
    bool init_result_{true};
    bool init_called_{false};
    bool shutdown_called_{false};
    int tick_count_{0};
    float last_dt_{0.0f};
    bool render2d_called_{false};
    bool render3d_called_{false};
    uint32_t consume_key_{0};
    std::vector<uint32_t> input_events_received_;
    int damage_events_received_{0};
    float absorb_ratio_{0.0f};
    bool cancel_damage_{false};

    static inline std::vector<std::string> execution_order_;
};

class ModPluginFrameworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        TrackingMod::execution_order_.clear();
    }
    void TearDown() override {
        TrackingMod::execution_order_.clear();
    }
};

TEST_F(ModPluginFrameworkTest, RegistrationAndDuplicateCheck) {
    contracts::ModManager manager;
    auto mod_mc = std::make_shared<TrackingMod>("minecraft", 100);
    auto mod_cs = std::make_shared<TrackingMod>("counter_strike", 200);
    auto mod_dup = std::make_shared<TrackingMod>("minecraft", 150);

    EXPECT_TRUE(manager.registerMod(mod_mc));
    EXPECT_TRUE(manager.registerMod(mod_cs));
    // Duplicate mod name must be rejected
    EXPECT_FALSE(manager.registerMod(mod_dup));
    EXPECT_EQ(manager.modCount(), 2u);
}

TEST_F(ModPluginFrameworkTest, PriorityOrderedExecution) {
    contracts::ModManager manager;
    // Low priority (10), Normal priority (50), High priority (100)
    auto mod_low = std::make_shared<TrackingMod>("low_priority", 10);
    auto mod_mid = std::make_shared<TrackingMod>("mid_priority", 50);
    auto mod_high = std::make_shared<TrackingMod>("high_priority", 100);

    // Register out of order
    manager.registerMod(mod_mid);
    manager.registerMod(mod_low);
    manager.registerMod(mod_high);

    contracts::CoreHostServices dummy_services{};
    ASSERT_TRUE(manager.initializeAll(dummy_services));

    // Initialization must proceed from highest priority to lowest
    ASSERT_EQ(TrackingMod::execution_order_.size(), 3u);
    EXPECT_EQ(TrackingMod::execution_order_[0], "high_priority:init");
    EXPECT_EQ(TrackingMod::execution_order_[1], "mid_priority:init");
    EXPECT_EQ(TrackingMod::execution_order_[2], "low_priority:init");

    TrackingMod::execution_order_.clear();
    manager.tick(0.016f);

    // Tick must also maintain strict priority order
    ASSERT_EQ(TrackingMod::execution_order_.size(), 3u);
    EXPECT_EQ(TrackingMod::execution_order_[0], "high_priority:tick");
    EXPECT_EQ(TrackingMod::execution_order_[1], "mid_priority:tick");
    EXPECT_EQ(TrackingMod::execution_order_[2], "low_priority:tick");
}

TEST_F(ModPluginFrameworkTest, InputConsumptionTruncation) {
    contracts::ModManager manager;
    auto cs_mod = std::make_shared<TrackingMod>("counter_strike", 200);
    auto mc_mod = std::make_shared<TrackingMod>("minecraft", 100);

    // CS consumes Key 'F' (e.g. Inspect Knife)
    cs_mod->consume_key_ = 'F';

    manager.registerMod(mc_mod);
    manager.registerMod(cs_mod);

    contracts::CoreHostServices dummy_services{};
    manager.initializeAll(dummy_services);
    TrackingMod::execution_order_.clear();

    // Event 1: Key 'F'
    contracts::ModInputEvent event_f{'F', true};
    bool consumed = manager.dispatchInput(event_f);

    EXPECT_TRUE(consumed);
    // CS mod must have received 'F', but MC mod must NEVER receive 'F' because it was truncated!
    EXPECT_EQ(cs_mod->input_events_received_.size(), 1u);
    EXPECT_EQ(mc_mod->input_events_received_.size(), 0u);

    // Event 2: Key 'W' (unconsumed)
    contracts::ModInputEvent event_w{'W', true};
    consumed = manager.dispatchInput(event_w);

    EXPECT_FALSE(consumed);
    // Both mods should receive 'W'
    EXPECT_EQ(cs_mod->input_events_received_.size(), 2u);
    EXPECT_EQ(mc_mod->input_events_received_.size(), 1u);
}

TEST_F(ModPluginFrameworkTest, DamageFilterPipeline) {
    contracts::ModManager manager;
    // CS mod has higher priority and absorbs 50% damage via armor
    auto cs_mod = std::make_shared<TrackingMod>("counter_strike", 200);
    cs_mod->absorb_ratio_ = 0.5f;

    // MC mod has lower priority and absorbs 20% of the remaining damage
    auto mc_mod = std::make_shared<TrackingMod>("minecraft", 100);
    mc_mod->absorb_ratio_ = 0.2f;

    manager.registerMod(mc_mod);
    manager.registerMod(cs_mod);

    contracts::CoreHostServices dummy_services{};
    manager.initializeAll(dummy_services);

    contracts::ModDamageContext ctx{100.0f, false};
    bool cancelled = manager.filterPlayerDamage(ctx);

    EXPECT_FALSE(cancelled);
    // 100.0 * (1 - 0.5) = 50.0; then 50.0 * (1 - 0.2) = 40.0
    EXPECT_NEAR(ctx.damage, 40.0f, 1e-4f);
    EXPECT_EQ(cs_mod->damage_events_received_, 1);
    EXPECT_EQ(mc_mod->damage_events_received_, 1);
}

TEST_F(ModPluginFrameworkTest, DynamicModToggle) {
    contracts::ModManager manager;
    auto cs_mod = std::make_shared<TrackingMod>("counter_strike", 200);
    cs_mod->consume_key_ = 'F';

    manager.registerMod(cs_mod);
    contracts::CoreHostServices dummy_services{};
    manager.initializeAll(dummy_services);

    // Enabled state
    contracts::ModInputEvent event_f{'F', true};
    EXPECT_TRUE(manager.dispatchInput(event_f));

    // Disable CS mod
    EXPECT_TRUE(manager.setModEnabled("counter_strike", false));
    EXPECT_FALSE(manager.isModEnabled("counter_strike"));

    // Event 'F' should pass through unconsumed when mod is disabled
    EXPECT_FALSE(manager.dispatchInput(event_f));

    // Re-enable CS mod
    EXPECT_TRUE(manager.setModEnabled("counter_strike", true));
    EXPECT_TRUE(manager.isModEnabled("counter_strike"));
    EXPECT_TRUE(manager.dispatchInput(event_f));
}

TEST_F(ModPluginFrameworkTest, ReverseShutdownSafety) {
    contracts::ModManager manager;
    auto mod_a = std::make_shared<TrackingMod>("mod_a", 10);
    auto mod_b = std::make_shared<TrackingMod>("mod_b", 50);

    manager.registerMod(mod_a);
    manager.registerMod(mod_b);

    contracts::CoreHostServices dummy_services{};
    manager.initializeAll(dummy_services);
    TrackingMod::execution_order_.clear();

    manager.shutdownAll();

    // Shutdown must execute in reverse priority order (low priority before high, or vice versa depending on teardown contract)
    // Safe teardown: high priority depends on low priority services, so higher priority is torn down first.
    ASSERT_EQ(TrackingMod::execution_order_.size(), 2u);
    EXPECT_EQ(TrackingMod::execution_order_[0], "mod_b:shutdown");
    EXPECT_EQ(TrackingMod::execution_order_[1], "mod_a:shutdown");
}

TEST_F(ModPluginFrameworkTest, ClearRemovesAllMods) {
    contracts::ModManager manager;
    auto mod = std::make_shared<TrackingMod>("temp_mod", 10);
    manager.registerMod(mod);
    EXPECT_EQ(manager.modCount(), 1u);

    manager.clear();
    EXPECT_EQ(manager.modCount(), 0u);
    // After clear, re-registering with the same name must succeed
    EXPECT_TRUE(manager.registerMod(std::make_shared<TrackingMod>("temp_mod", 10)));
}

} // namespace mc::test
