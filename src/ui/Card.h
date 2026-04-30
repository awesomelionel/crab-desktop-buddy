#pragma once

#include <stdint.h>

#include "InputHandler.h"

class Display;

class Card : public InputHandler {
public:
    virtual ~Card() = default;

    // Called once per loop tick. Default no-op; cards with per-frame animation
    // override (eyes).
    virtual void tick(uint32_t now_ms) { (void)now_ms; }

    // The stack calls this when the card becomes the visible top of the stack
    // (on push, or on pop revealing it). Cards use it to clear dirty state so
    // the next render redraws everything.
    virtual void invalidate() = 0;

    // Returns true if the card needs to be repainted on this tick.
    virtual bool isDirty() const = 0;

    // Render the card. Called when isDirty() returns true.
    virtual void render(Display& display) = 0;

    // Default: cards do not consume buttons, so the stack falls through to nav.
    bool handleButton(ButtonEvent /*ev*/, uint32_t /*now_ms*/) override { return false; }
};
