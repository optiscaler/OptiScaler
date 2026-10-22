#include "pch.h"
#include "low_latency.h"

// private
void LowLatency::update_effective_fg_state()
{
    if (auto current_tech = currently_active_tech.load())
    {
        if (!current_tech)
            return;

        current_tech->set_effective_fg_state(fg);
    }
}

void LowLatency::update_enabled_override()
{
    if (auto current_tech = currently_active_tech.load())
    {
        if (!current_tech)
            return;

        current_tech->set_low_latency_override((ForceReflex) Config::Instance()->FN_ForceReflex.value_or_default());
    }
}

// public
bool LowLatency::deinit_current_tech()
{
    // currently_active_tech becomes nullptr
    // but we need to wait for all users of the old one to release
    auto old_tech = currently_active_tech.exchange(nullptr);

    if (old_tech)
    {
        LOG_TRACE("Deiniting current tech");
        while (old_tech.use_count() > 1)
            std::this_thread::yield();

        old_tech->deinit();

        std::memset(frame_reports, 0, sizeof(frame_reports));

        return true;
    }

    return false;
}

bool LowLatency::get_low_latency_tech_context(void** low_latency_context, LowLatencyMode* low_latency_tech)
{
    if (auto current_tech = currently_active_tech.load())
    {
        if (!current_tech || !low_latency_context || !low_latency_tech)
            return false;

        *low_latency_context = current_tech->get_tech_context();
        *low_latency_tech = current_tech->get_mode();
    }

    // We are during deinit, don't let app use the context
    if (delay_deinit > 0)
    {
        *low_latency_context = nullptr;
        delay_deinit = 1;
    }

    return true;
}

bool LowLatency::is_low_latency_enabled()
{
    auto current_tech = currently_active_tech.load();

    if (!current_tech)
        return false;

    return current_tech->is_enabled();
}