#include "pch.h"

using namespace optiscaler::dlssg;

static void off_mode_is_inert()
{
    LifecycleController controller;
    controller.Configure(LifecycleMode::Off, 10);

    const auto decision = controller.Step({ .fgChanged = true, .validInputs = true });
    assert(!decision.beginCycle);
    assert(controller.Phase() == LifecyclePhase::Idle);
}

static void observe_mode_never_cycles()
{
    LifecycleController controller;
    controller.Configure(LifecycleMode::Observe, 10);

    auto decision = controller.Step({ .swapchainChanged = true, .validInputs = true });
    assert(decision.logSignalChange);
    assert(!decision.beginCycle);
    assert(controller.Phase() == LifecyclePhase::Idle);

    decision = controller.Step({ .validInputs = true });
    assert(decision.logSignalChange);
    assert(controller.Phase() == LifecyclePhase::Idle);
}

static void trigger_frame_is_not_a_warmup_frame()
{
    LifecycleController controller;
    controller.Configure(LifecycleMode::Cycle, 2);

    auto decision = controller.Step({ .reset = true, .validInputs = true });
    assert(decision.beginCycle);
    assert(decision.validWarmupFrames == 0);
    assert(controller.Phase() == LifecyclePhase::WarmingInputs);

    decision = controller.Step({ .validInputs = true });
    assert(!decision.reenable);
    assert(decision.validWarmupFrames == 1);

    decision = controller.Step({ .validInputs = true });
    assert(decision.warmupCompleted);
    assert(controller.Phase() == LifecyclePhase::ReenablePending);

    decision = controller.Step({ .validInputs = true });
    assert(decision.reenable);
    assert(controller.Phase() == LifecyclePhase::Idle);
}

static void invalid_input_restarts_the_warmup_count()
{
    LifecycleController controller;
    controller.Configure(LifecycleMode::Cycle, 2);

    assert(controller.Step({ .fgChanged = true }).beginCycle);
    assert(controller.Step({ .validInputs = true }).validWarmupFrames == 1);
    assert(controller.Step({ .validInputs = false }).validWarmupFrames == 0);
    assert(controller.Step({ .validInputs = true }).validWarmupFrames == 1);
    assert(controller.Step({ .validInputs = true }).warmupCompleted);
}

static void repeated_triggers_do_not_restart_an_active_cycle()
{
    LifecycleController controller;
    controller.Configure(LifecycleMode::Cycle, 2);

    assert(controller.Step({ .fgChanged = true }).beginCycle);
    auto decision = controller.Step({ .fgChanged = true, .reset = true, .validInputs = true });
    assert(!decision.beginCycle);
    assert(decision.validWarmupFrames == 1);
}

static void explicit_requests_and_fail_closed_are_bounded()
{
    LifecycleController controller;
    controller.Configure(LifecycleMode::Cycle, 1);
    controller.RequestCycle();

    assert(controller.Step({}).beginCycle);
    controller.FailClosed();
    auto decision = controller.Step({ .validInputs = true });
    assert(!decision.reenable);
    assert(controller.Phase() == LifecyclePhase::FailedClosed);

    controller.Configure(LifecycleMode::Observe, 1);
    assert(controller.Phase() == LifecyclePhase::Idle);
}

static void requested_cycle_waits_for_runtime_readiness()
{
    LifecycleController controller;
    controller.Configure(LifecycleMode::Cycle, 1);
    controller.RequestCycle();

    auto decision = controller.Step({ .runtimeReady = false });
    assert(!decision.beginCycle);
    assert(controller.Phase() == LifecyclePhase::Idle);

    decision = controller.Step({ .runtimeReady = true });
    assert(decision.beginCycle);
}

static void runtime_loss_blocks_warmup_and_reenable()
{
    LifecycleController controller;
    controller.Configure(LifecycleMode::Cycle, 1);

    assert(controller.Step({ .fgChanged = true }).beginCycle);

    auto decision = controller.Step({ .validInputs = true, .runtimeReady = false });
    assert(decision.validWarmupFrames == 0);
    assert(controller.Phase() == LifecyclePhase::WarmingInputs);

    decision = controller.Step({ .validInputs = true });
    assert(decision.warmupCompleted);

    decision = controller.Step({ .validInputs = true, .runtimeReady = false });
    assert(!decision.reenable);
    assert(controller.Phase() == LifecyclePhase::ReenablePending);

    decision = controller.Step({ .validInputs = true });
    assert(decision.reenable);
}

static void heartbeat_is_sparse()
{
    LifecycleController controller;
    controller.Configure(LifecycleMode::Observe, 10, 3);

    assert(!controller.Step({}).logHeartbeat);
    assert(!controller.Step({}).logHeartbeat);
    assert(controller.Step({}).logHeartbeat);
}

int main()
{
    off_mode_is_inert();
    observe_mode_never_cycles();
    trigger_frame_is_not_a_warmup_frame();
    invalid_input_restarts_the_warmup_count();
    repeated_triggers_do_not_restart_an_active_cycle();
    explicit_requests_and_fail_closed_are_bounded();
    requested_cycle_waits_for_runtime_readiness();
    runtime_loss_blocks_warmup_and_reenable();
    heartbeat_is_sparse();

    std::cout << "DLSSG lifecycle tests passed\n";
    return 0;
}
