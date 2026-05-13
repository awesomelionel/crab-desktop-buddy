#include "StatusCard.h"

#include <Arduino.h>

#include "../../display/Display.h"
#include "../BatteryWidget.h"
#include "../Footer.h"
#include "../PromptBadge.h"
#include "format.h"

namespace {
// Charging animation pacing. Bars sweep 1 → 4 → 1 → 4 … at this
// rate. Slow enough that it doesn't feel jittery on the small
// 4-bar icon, fast enough that it reads as motion.
constexpr uint32_t kChargeAnimMs = 600;

constexpr int kUsageX = 8;
constexpr int kUsageY = 69;
constexpr int kUsageW = 224;
constexpr int kUsageH = 33;
constexpr int kUsageBarX = 8;
constexpr int kUsageBarY = 85;
constexpr int kUsageBarW = 224;
constexpr int kUsageBarH = 8;
constexpr int kMsgLegacyY = 92;
constexpr int kMsgUsageY = 104;

uint8_t usagePercent(const ClaudeUsage& usage) {
    uint64_t denom = (uint64_t)usage.used + usage.remaining;
    if (denom == 0 && usage.has_limit) denom = usage.limit;
    if (denom == 0) return 0;
    uint64_t pct = ((uint64_t)usage.used * 100u + (denom / 2u)) / denom;
    if (pct > 100u) pct = 100u;
    return (uint8_t)pct;
}

uint16_t usageBarColor(uint8_t pct) {
    if (pct >= 90) return ST77XX_RED;
    if (pct >= 70) return ST77XX_YELLOW;
    if (pct >= 45) return 0xFD20;  // orange
    return ST77XX_CYAN;
}

void drawUsageBar(Adafruit_ST7789& tft, uint8_t pct) {
    tft.drawRect(kUsageBarX, kUsageBarY, kUsageBarW, kUsageBarH, 0x39E7);
    tft.fillRect(kUsageBarX + 1, kUsageBarY + 1,
                 kUsageBarW - 2, kUsageBarH - 2, 0x2124);

    int fill_w = ((kUsageBarW - 2) * pct) / 100;
    if (fill_w > 0) {
        tft.fillRect(kUsageBarX + 1, kUsageBarY + 1,
                     fill_w, kUsageBarH - 2, usageBarColor(pct));
    }
}
}  // namespace

StatusCard::StatusCard(const AppState& state, PromptUi& prompt)
    : state_(state),
      prompt_(prompt),
      ever_drawn_(false),
      last_drawn_state_(STATE_DISCONNECTED),
      last_drawn_total_(0xFF),       // sentinel: no real count == 0xFF
      last_drawn_running_(0xFF),
      last_drawn_waiting_(0xFF),
      last_drawn_valid_(false),
      last_drawn_msg_{0},
      last_drawn_live_(false),
      last_recheck_ms_(0),
      last_drawn_tokens_today_(0xFFFFFFFFu),
      last_drawn_usage_valid_(false),
      last_drawn_usage_used_(0xFFFFFFFFu),
      last_drawn_usage_remaining_(0xFFFFFFFFu),
      last_drawn_usage_pct_(0xFF),
      last_drawn_battery_pct_(0xFE),       // sentinel != 0xFF (unsampled)
      last_drawn_battery_charging_(false),
      last_drawn_battery_present_(false),
      last_drawn_anim_step_(0xFF),
      anim_step_(0),
      last_anim_step_ms_(0),
      last_drawn_prompt_collapsed_(false),
      last_drawn_prompt_tool_{0} {}

bool StatusCard::handleButton(ButtonEvent ev, uint32_t now_ms) {
    // While a permission prompt is COLLAPSED to the badge, StatusCard
    // is the active carousel card. Forward button events to the
    // prompt UI so a CENTER press re-EXPANDs and UP/DOWN don't
    // silently navigate the carousel — same contract as EyesCard.
    if (prompt_.mode != PROMPT_UI_HIDDEN) {
        prompt_ui_button(&prompt_, ev, now_ms);
        return true;
    }
    return false;
}

void StatusCard::invalidate() {
    ever_drawn_ = false;
    last_drawn_msg_[0] = 0;
}

void StatusCard::tick(uint32_t now_ms) {
    if (now_ms - last_recheck_ms_ > 1000) {
        last_recheck_ms_ = now_ms;
        // No-op: the live flag is read at render time. The 1s tick is just
        // here so isDirty() flips true once liveness changes due to timeout.
    }
    // Advance the charging animation while charging. Outside of
    // charging we keep anim_step_ at 0 so the discharge render is
    // fully determined by percent.
    if (state_.battery().present && state_.battery().charging) {
        if (now_ms - last_anim_step_ms_ >= kChargeAnimMs) {
            last_anim_step_ms_ = now_ms;
            anim_step_ = (uint8_t)((anim_step_ + 1) & 0x03);
        }
    } else {
        anim_step_ = 0;
    }
}

bool StatusCard::isDirty() const {
    if (!ever_drawn_) return true;
    const ClaudeStatus& status = state_.status();
    if (state_.buddyState() != last_drawn_state_) return true;
    if (status.total   != last_drawn_total_)   return true;
    if (status.running != last_drawn_running_) return true;
    if (status.waiting != last_drawn_waiting_) return true;
    if (status.valid   != last_drawn_valid_)   return true;
    if (status.tokens_today != last_drawn_tokens_today_) return true;
    const uint8_t pct = status.usage.valid ? usagePercent(status.usage) : 0;
    if (status.usage.valid      != last_drawn_usage_valid_)     return true;
    if (status.usage.used       != last_drawn_usage_used_)      return true;
    if (status.usage.remaining  != last_drawn_usage_remaining_) return true;
    if (status.usage.valid && pct != last_drawn_usage_pct_)     return true;
    if (strncmp(last_drawn_msg_, status.msg, sizeof(last_drawn_msg_)) != 0) return true;
    if (state_.isLive(millis()) != last_drawn_live_) return true;
    const BatteryStatus& bat = state_.battery();
    if (bat.present  != last_drawn_battery_present_)  return true;
    if (bat.percent  != last_drawn_battery_pct_)      return true;
    if (bat.charging != last_drawn_battery_charging_) return true;
    if (bat.charging && anim_step_ != last_drawn_anim_step_) return true;
    const bool collapsed = (prompt_.mode == PROMPT_UI_COLLAPSED);
    if (collapsed != last_drawn_prompt_collapsed_) return true;
    if (collapsed && strncmp(last_drawn_prompt_tool_, prompt_.tool,
                              sizeof(last_drawn_prompt_tool_)) != 0) return true;
    return false;
}

void StatusCard::render(Display& display) {
    auto&               tft    = display.tft();
    const ClaudeStatus& status = state_.status();
    const BuddyState    bs     = state_.buddyState();
    const bool          live   = state_.isLive(millis());

    // Per-region rendering. fillScreen runs only on a state transition
    // (rare and meaningful — IDLE↔WORKING↔WAITING↔DISCONNECTED). Every
    // other dirty trip (msg / counters / tokens / live) erases just the
    // strip that owns the changing content, eliminating the whole-card
    // strobe the previous unconditional fillScreen produced. Per
    // CLAUDE.md: never fillScreen in a path that runs more than once
    // per state.
    const bool state_changed = !ever_drawn_ || (last_drawn_state_ != bs);

    if (state_changed) {
        tft.fillScreen(ST77XX_BLACK);

        // State name (size 3, white, centred at y=20). Only redrawn on a
        // state transition because the string is derived from `bs` and
        // can't change without state_changed being true.
        tft.setTextSize(3);
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        const char* name = state_name(bs);
        int16_t  x1, y1; uint16_t tw, th;
        tft.getTextBounds(name, 0, 0, &x1, &y1, &tw, &th);
        tft.setCursor((display.width() - (int)tw) / 2, 20);
        tft.print(name);
    }

    // Counters strip (size 1, cyan, y=58 .. 65). Repaint when any of
    // total/running/waiting changes. Erase as a 240×9 strip so any
    // shorter previous text (e.g. counts went from 10 → 9) doesn't
    // leave trailing chars.
    const bool counters_changed = state_changed ||
                                  (last_drawn_total_   != status.total)   ||
                                  (last_drawn_running_ != status.running) ||
                                  (last_drawn_waiting_ != status.waiting);
    if (counters_changed) {
        if (!state_changed) {
            tft.fillRect(0, 58, 240, 9, ST77XX_BLACK);
        }
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
        tft.setCursor(8, 58);
        tft.printf("total %u  run %u  wait %u",
                   status.total, status.running, status.waiting);
    }

    // Usage strip. Newer bridge snapshots can provide quota usage, in
    // which case this draws a percentage + bar + remaining amount.
    // Legacy snapshots fall back to the old daily-token line.
    const uint8_t usage_pct = status.usage.valid ? usagePercent(status.usage) : 0;
    const bool token_changed = state_changed ||
                               (last_drawn_valid_         != status.valid) ||
                               (last_drawn_tokens_today_  != status.tokens_today) ||
                               (last_drawn_usage_valid_   != status.usage.valid) ||
                               (last_drawn_usage_used_    != status.usage.used) ||
                               (last_drawn_usage_remaining_ != status.usage.remaining) ||
                               (status.usage.valid && last_drawn_usage_pct_ != usage_pct);
    if (token_changed) {
        if (!state_changed) {
            tft.fillRect(0, 70, 240, 46, ST77XX_BLACK);
        }
        if (status.usage.valid) {
            char left_buf[kFormatTokenCountBufLen];
            format_token_count(status.usage.remaining, left_buf, sizeof(left_buf));

            char pct_line[16];
            snprintf(pct_line, sizeof(pct_line), "%u%% used", usage_pct);
            char left_line[20];
            snprintf(left_line, sizeof(left_line), "%s left", left_buf);

            tft.setTextSize(2);
            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            tft.setCursor(kUsageX, kUsageY);
            tft.print(pct_line);

            tft.setTextSize(1);
            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            int16_t lx1, ly1; uint16_t lw, lh;
            tft.getTextBounds(left_line, 0, 0, &lx1, &ly1, &lw, &lh);
            tft.setCursor(display.width() - 8 - (int)lw, 75);
            tft.print(left_line);

            drawUsageBar(tft, usage_pct);
        } else if (status.valid) {
            char tok_buf[kFormatTokenCountBufLen];
            format_token_count(status.tokens_today, tok_buf, sizeof(tok_buf));
            char tok_line[32];
            snprintf(tok_line, sizeof(tok_line), "%s tokens today", tok_buf);

            tft.setTextSize(2);
            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            int16_t tx1, ty1; uint16_t ttw, tth;
            tft.getTextBounds(tok_line, 0, 0, &tx1, &ty1, &ttw, &tth);
            tft.setCursor((display.width() - (int)ttw) / 2, 70);
            tft.print(tok_line);
        }
    }

    // Message block / prompt badge (y=92..115). When a permission
    // prompt is COLLAPSED, the badge takes over this slot so the
    // user sees the same approve-prompt UI as on the eyes card.
    // Otherwise, the regular two-line msg renders here. The two
    // share an erase rect so a transition between them wipes
    // everything cleanly.
    const bool collapsed = (prompt_.mode == PROMPT_UI_COLLAPSED);
    const bool msg_changed = state_changed ||
                             (strncmp(last_drawn_msg_, status.msg,
                                      sizeof(last_drawn_msg_)) != 0) ||
                             (last_drawn_usage_valid_ != status.usage.valid);
    const bool prompt_changed = state_changed ||
                                (collapsed != last_drawn_prompt_collapsed_) ||
                                (collapsed && strncmp(last_drawn_prompt_tool_,
                                                      prompt_.tool,
                                                      sizeof(last_drawn_prompt_tool_)) != 0);
    if (msg_changed || prompt_changed) {
        if (!state_changed) {
            if (status.usage.valid && collapsed) {
                tft.fillRect(0, ui::kPromptBadgeEraseY, 240,
                             ui::kPromptBadgeEraseH, ST77XX_BLACK);
            } else if (status.usage.valid) {
                tft.fillRect(0, 102, 240, 14, ST77XX_BLACK);
            } else {
                tft.fillRect(0, 92, 240, 24, ST77XX_BLACK);
            }
        }
        if (collapsed) {
            ui::drawPromptBadge(tft, prompt_.tool);
        } else {
            tft.setTextSize(1);
            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            if (status.msg[0]) {
                const int msg_y = status.usage.valid ? kMsgUsageY : kMsgLegacyY;
                tft.setCursor(8, msg_y);
                tft.printf("%.34s", status.msg);
                if (!status.usage.valid && strlen(status.msg) > 34) {
                    tft.setCursor(8, 104);
                    tft.printf("%.34s", status.msg + 34);
                }
            }
        }
    }

    // Footer band (y=117 .. 134). Repaint when liveness flips. The
    // device name is treated as static here — drawFooter handles its
    // own internal layout. The battery widget shares this band on
    // the right side; it lives in y=120..130 so it sits inside the
    // footer's erase rect, which is why a footer redraw also forces
    // a battery redraw below.
    const bool footer_changed = state_changed || (last_drawn_live_ != live);
    if (footer_changed) {
        if (!state_changed) {
            tft.fillRect(0, ui::kFooterTopY, 240, ui::kFooterH, ST77XX_BLACK);
        }
        ui::drawFooter(tft, state_.deviceName(), live);
    }

    // Battery widget (right edge of footer band). Hidden when the
    // fuel gauge isn't present (e.g. older board revision or chip
    // not detected at boot). When charging, the bars cycle 1→2→3→4
    // so the user sees the icon "filling up"; when discharging, the
    // bar count maps to percent thresholds (see BatteryWidget.cpp).
    const BatteryStatus& bat = state_.battery();
    const bool battery_changed = state_changed || footer_changed ||
                                 (last_drawn_battery_present_  != bat.present)  ||
                                 (last_drawn_battery_pct_      != bat.percent)  ||
                                 (last_drawn_battery_charging_ != bat.charging) ||
                                 (bat.charging && last_drawn_anim_step_ != anim_step_);
    if (battery_changed) {
        if (!state_changed && !footer_changed) {
            // Footer redraw already wiped this rect; skip the
            // duplicate erase to keep the per-frame cost tiny.
            tft.fillRect(ui::kBatteryEraseX, ui::kBatteryEraseY,
                         ui::kBatteryEraseW, ui::kBatteryEraseH, ST77XX_BLACK);
        }
        if (bat.present) {
            ui::drawBatteryWidget(tft, bat.percent, bat.charging, anim_step_);
        }
    }

    // Snapshot every tracked value.
    last_drawn_state_         = bs;
    last_drawn_total_         = status.total;
    last_drawn_running_       = status.running;
    last_drawn_waiting_       = status.waiting;
    last_drawn_valid_         = status.valid;
    strncpy(last_drawn_msg_, status.msg, sizeof(last_drawn_msg_) - 1);
    last_drawn_msg_[sizeof(last_drawn_msg_) - 1] = 0;
    last_drawn_live_              = live;
    last_drawn_tokens_today_      = status.tokens_today;
    last_drawn_usage_valid_       = status.usage.valid;
    last_drawn_usage_used_        = status.usage.used;
    last_drawn_usage_remaining_   = status.usage.remaining;
    last_drawn_usage_pct_         = usage_pct;
    last_drawn_battery_present_   = bat.present;
    last_drawn_battery_pct_       = bat.percent;
    last_drawn_battery_charging_  = bat.charging;
    last_drawn_anim_step_         = anim_step_;
    last_drawn_prompt_collapsed_  = collapsed;
    strncpy(last_drawn_prompt_tool_, prompt_.tool,
            sizeof(last_drawn_prompt_tool_) - 1);
    last_drawn_prompt_tool_[sizeof(last_drawn_prompt_tool_) - 1] = 0;
    ever_drawn_                   = true;
}
