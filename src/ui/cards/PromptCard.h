#pragma once

#include <stdint.h>

#include "../Card.h"
#include "prompt_ui.h"

// Overlay card that wraps the existing PromptUi state machine.
//
// CardController pushes it onto CardStack as an overlay when the snapshot
// reports a visible prompt and pops it (clearOverlay) when the prompt clears.
// PromptCard never touches the radio directly — it just owns the ui state
// and rendering, and consumes button events. The controller drains the
// outgoing decision line via prompt_ui_take_outgoing.
class PromptCard : public Card {
public:
    explicit PromptCard(PromptUi& ui);

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;

    // Consumes UP/DOWN/CENTER for the prompt UI; returns true so the
    // surrounding CardStack does not fall through to nav.
    bool handleButton(ButtonEvent ev, uint32_t now_ms) override;

    // Optional footer device-name + live/offln strip on render.
    void setFooter(const char* device_name, bool live);

private:
    PromptUi&         ui_;
    bool              ever_drawn_;
    bool              last_visible_;
    PromptOption      last_highlight_;
    char              last_id_[40];
    bool              last_flashing_;
    char              footer_device_[16];
    bool              footer_live_;
    bool              has_footer_;
};
