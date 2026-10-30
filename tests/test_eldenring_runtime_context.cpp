#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#include "eldenring_runtime_context.hpp"

using namespace eldenring;

class RuntimeContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset or verify baseline states
        auto& ctx = RuntimeContext::Instance();
        ctx.resetForTesting();
    }
};

TEST_F(RuntimeContextTest, DefaultModeConfigMatchesAdapterInvariants) {
    auto& ctx = RuntimeContext::Instance();
    auto& mode = ctx.mode();

    // Default states
    EXPECT_TRUE(mode.mc_mode.load());
    EXPECT_FALSE(mode.cs_mode.load());
    EXPECT_EQ(mode.game_mode.load(), static_cast<uint8_t>(creative::GameMode::Survival));
    EXPECT_FALSE(mode.first_person.load());
    EXPECT_TRUE(mode.steve_enabled.load());
    EXPECT_FALSE(mode.block_anchor.load()); // Crucial: must never default to true to prevent movement freeze
    EXPECT_TRUE(mode.blocks_enabled.load());
    EXPECT_TRUE(mode.instant_turn.load());
    EXPECT_TRUE(mode.disable_vanilla_roll.load());
    EXPECT_FLOAT_EQ(mode.reach.load(), 4.5f);
    EXPECT_FLOAT_EQ(mode.damage_scale.load(), 20.0f);
    EXPECT_FLOAT_EQ(mode.sweep_radius.load(), 3.0f);
}

TEST_F(RuntimeContextTest, StartupModesKeepMcOnAndNeverAutoEnableCs) {
    auto& ctx = RuntimeContext::Instance();

    // Issue #101: the default first-person path belongs to MC. CS mode is only
    // entered through the explicit hotkey toggle, never at startup.
    EXPECT_TRUE(ctx.mode().mc_mode.load());
    EXPECT_FALSE(ctx.mode().cs_mode.load());
    EXPECT_FALSE(ctx.mode().first_person.load());
}

TEST_F(RuntimeContextTest, GameModeTransitionsAreAtomicAndConsistent) {
    auto& ctx = RuntimeContext::Instance();

    EXPECT_EQ(ctx.currentGameMode(), creative::GameMode::Survival);

    ctx.mode().game_mode.store(static_cast<uint8_t>(creative::GameMode::Creative));
    EXPECT_EQ(ctx.currentGameMode(), creative::GameMode::Creative);

    ctx.mode().game_mode.store(static_cast<uint8_t>(creative::GameMode::Survival));
    EXPECT_EQ(ctx.currentGameMode(), creative::GameMode::Survival);
}

TEST_F(RuntimeContextTest, CommandQueueFIFOAndCapacity) {
    auto& ctx = RuntimeContext::Instance();

    // Ensure queue starts empty
    InputCommand dummy;
    while (ctx.dequeueCommand(dummy)) {}

    EXPECT_FALSE(ctx.dequeueCommand(dummy));

    InputCommand cmd1{InputCommandType::ToggleMcMode, 100, 1.0f, 2.0f, 3.0f, 0};
    InputCommand cmd2{InputCommandType::Jump, 101, 0.0f, 0.0f, 0.0f, 0};
    InputCommand cmd3{InputCommandType::PlaceBlock, 102, 10.0f, 20.0f, 30.0f, 1};

    EXPECT_TRUE(ctx.enqueueCommand(cmd1));
    EXPECT_TRUE(ctx.enqueueCommand(cmd2));
    EXPECT_TRUE(ctx.enqueueCommand(cmd3));

    InputCommand out;
    EXPECT_TRUE(ctx.dequeueCommand(out));
    EXPECT_EQ(out.type, InputCommandType::ToggleMcMode);
    EXPECT_EQ(out.tick, 100u);

    EXPECT_TRUE(ctx.dequeueCommand(out));
    EXPECT_EQ(out.type, InputCommandType::Jump);
    EXPECT_EQ(out.tick, 101u);

    EXPECT_TRUE(ctx.dequeueCommand(out));
    EXPECT_EQ(out.type, InputCommandType::PlaceBlock);
    EXPECT_EQ(out.tick, 102u);
    EXPECT_FLOAT_EQ(out.x, 10.0f);
    EXPECT_FLOAT_EQ(out.y, 20.0f);
    EXPECT_FLOAT_EQ(out.z, 30.0f);
    EXPECT_EQ(out.slot, 1);

    EXPECT_FALSE(ctx.dequeueCommand(out));
}

TEST_F(RuntimeContextTest, ConcurrentProducersConsumerIntegrity) {
    auto& ctx = RuntimeContext::Instance();

    InputCommand dummy;
    while (ctx.dequeueCommand(dummy)) {}

    constexpr int kProducerCount = 4;
    constexpr int kItemsPerProducer = 100;
    std::atomic<bool> start_signal{false};
    std::atomic<int> success_enqueued{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducerCount; ++p) {
        producers.emplace_back([&, p]() {
            while (!start_signal.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kItemsPerProducer; ++i) {
                InputCommand cmd{
                    InputCommandType::AttackSwing,
                    static_cast<uint64_t>(p * 1000 + i),
                    static_cast<float>(p),
                    static_cast<float>(i),
                    0.0f,
                    p
                };
                if (ctx.enqueueCommand(cmd)) {
                    success_enqueued.fetch_add(1, std::memory_order_relaxed);
                }
                std::this_thread::yield();
            }
        });
    }

    start_signal.store(true);

    int dequeued_count = 0;
    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(2)) {
        InputCommand cmd;
        if (ctx.dequeueCommand(cmd)) {
            dequeued_count++;
            EXPECT_EQ(cmd.type, InputCommandType::AttackSwing);
        } else if (dequeued_count == success_enqueued.load() && success_enqueued.load() > 0) {
            // Check if producers finished
            bool all_done = true;
            for (auto& t : producers) {
                if (t.joinable()) { all_done = false; break; }
            }
            if (all_done) break;
        }
    }

    for (auto& t : producers) {
        if (t.joinable()) t.join();
    }

    // Drain remaining
    InputCommand cmd;
    while (ctx.dequeueCommand(cmd)) {
        dequeued_count++;
    }

    EXPECT_EQ(dequeued_count, success_enqueued.load());
    EXPECT_GT(dequeued_count, 0);
}

TEST_F(RuntimeContextTest, WorldAndCombatSystemsSubstructuresIntegrity) {
    auto& ctx = RuntimeContext::Instance();

    // Verify WorldSystems block mutex and grid access
    {
        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
        EXPECT_EQ(ctx.world().block_grid.count(), 0u);
    }

    // Verify CombatSystems access
    {
        std::lock_guard<std::mutex> g(ctx.combat().melee_mutex);
        EXPECT_EQ(ctx.combat().damage_queue.pending(), 0u);
        EXPECT_FALSE(ctx.combat().shield.isBlocking());
    }
}
