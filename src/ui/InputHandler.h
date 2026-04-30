#pragma once

#include "buttons.h"

class InputHandler {
public:
    virtual ~InputHandler() = default;

    // Return true if the event was consumed and should not propagate further
    // (e.g. to nav). Return false to let the surrounding stack handle it.
    virtual bool handleButton(ButtonEvent ev, uint32_t now_ms) = 0;
};
