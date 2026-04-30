#include "CardStack.h"

#include "../display/Display.h"

void CardStack::addCard(Card* card) {
    cards_.push_back(card);
}

void CardStack::pushOverlay(Card* card) {
    if (overlay_ == card) return;
    overlay_ = card;
    if (overlay_) overlay_->invalidate();
}

void CardStack::clearOverlay() {
    if (!overlay_) return;
    overlay_ = nullptr;
    Card* a = active();
    if (a) a->invalidate();
}

void CardStack::next() {
    if (overlay_ || cards_.empty()) return;
    index_ = (index_ + 1) % cards_.size();
    Card* a = active();
    if (a) a->invalidate();
}

void CardStack::prev() {
    if (overlay_ || cards_.empty()) return;
    index_ = (index_ + cards_.size() - 1) % cards_.size();
    Card* a = active();
    if (a) a->invalidate();
}

Card* CardStack::active() const {
    if (overlay_) return overlay_;
    if (cards_.empty()) return nullptr;
    return cards_[index_];
}

void CardStack::tick(uint32_t now_ms, Display& display) {
    Card* a = active();
    if (!a) return;
    a->tick(now_ms);
    if (a->isDirty()) a->render(display);
}

bool CardStack::handleButton(ButtonEvent ev, uint32_t now_ms) {
    Card* a = active();
    if (!a) return false;
    return a->handleButton(ev, now_ms);
}
