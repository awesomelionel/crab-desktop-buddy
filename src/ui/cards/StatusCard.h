#pragma once

#include <stdint.h>
#include <string.h>

#include "../Card.h"
#include "../../core/AppState.h"
#include "prompt_ui.h"
#include "protocol.h"
#include "state.h"

class StatusCard : public Card {
public:
    StatusCard(const AppState& state, PromptUi& prompt);

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;
    bool handleButton(ButtonEvent ev, uint32_t now_ms) override;

    // The status card needs a periodic recheck so live → offline transitions
    // repaint even if the snapshot stops arriving.
    void tick(uint32_t now_ms) override;

private:
    const AppState& state_;
    PromptUi&       prompt_;
    bool            ever_drawn_;
    BuddyState      last_drawn_state_;
    // Per-region snapshots so render() can repaint only the strips that
    // actually changed and avoid a full-screen fillScreen on every dirty
    // trip — that pattern strobed the whole card visibly each time
    // tokens_today, msg, or the live flag updated.
    uint8_t         last_drawn_total_;
    uint8_t         last_drawn_running_;
    uint8_t         last_drawn_waiting_;
    bool            last_drawn_valid_;
    char            last_drawn_msg_[sizeof(ClaudeStatus::msg)];
    bool            last_drawn_live_;
    uint32_t        last_recheck_ms_;
    uint32_t        last_drawn_tokens_today_;
    bool            last_drawn_usage_valid_;
    uint32_t        last_drawn_usage_used_;
    uint32_t        last_drawn_usage_remaining_;
    uint8_t         last_drawn_usage_pct_;
    // The user-configured daily token cap, snapshotted so changing it
    // via the web UI triggers a repaint even when tokens_today is
    // unchanged. 0xFFFFFFFFu sentinel ensures the first render is dirty.
    uint32_t        last_drawn_daily_cap_;
    // Tracks whether the prompt badge was on screen on the previous
    // render so we know to wipe the msg block area when COLLAPSED
    // turns off, and tracks the tool string so the badge re-renders
    // when a new prompt arrives.
    bool            last_drawn_prompt_collapsed_;
    char            last_drawn_prompt_tool_[16];
    // Battery snapshot — updated when the cached BatteryStatus
    // changes or the charging animation steps. anim_step_ cycles
    // 0..3 every kChargeAnimMs while charging; outside of charging
    // we leave it at 0 so the discharge render path is fully
    // determined by percent_.
    uint8_t         last_drawn_battery_pct_;
    bool            last_drawn_battery_charging_;
    bool            last_drawn_battery_present_;
    uint8_t         last_drawn_anim_step_;
    uint8_t         anim_step_;
    uint32_t        last_anim_step_ms_;
};
