#include "CardStack.h"

#include "../display/Display.h"

void CardStack::addCard(Card* card) {
    cards_.push_back(card);
}

void CardStack::clear() {
    cards_.clear();
    index_   = 0;
    overlay_ = nullptr;
}

void CardStack::setIndex(size_t i) {
    if (overlay_ || cards_.empty()) return;
    if (i >= cards_.size()) i = cards_.size() - 1;
    Card* prev_a = active();
    index_ = i;
    Card* a = active();
    if (prev_a && prev_a != a) prev_a->onHide();
    if (a) {
        if (a != prev_a) a->onShow();
        a->invalidate();
    }
}

void CardStack::pushOverlay(Card* card) {
    if (overlay_ == card) return;
    Card* prev_a = active();
    overlay_ = card;
    Card* a = active();
    if (prev_a && prev_a != a) prev_a->onHide();
    if (overlay_) {
        if (overlay_ != prev_a) overlay_->onShow();
        overlay_->invalidate();
    }
}

void CardStack::clearOverlay() {
    if (!overlay_) return;
    Card* prev_a = active();   // == overlay_
    overlay_ = nullptr;
    Card* a = active();
    if (prev_a) prev_a->onHide();
    if (a) {
        a->onShow();
        a->invalidate();
    }
}

void CardStack::next() {
    if (overlay_ || cards_.empty()) return;
    Card* prev_a = active();
    index_ = (index_ + 1) % cards_.size();
    Card* a = active();
    if (prev_a && prev_a != a) prev_a->onHide();
    if (a) {
        if (a != prev_a) a->onShow();
        a->invalidate();
    }
}

void CardStack::prev() {
    if (overlay_ || cards_.empty()) return;
    Card* prev_a = active();
    index_ = (index_ + cards_.size() - 1) % cards_.size();
    Card* a = active();
    if (prev_a && prev_a != a) prev_a->onHide();
    if (a) {
        if (a != prev_a) a->onShow();
        a->invalidate();
    }
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
