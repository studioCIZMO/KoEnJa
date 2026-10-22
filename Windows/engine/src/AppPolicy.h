#pragma once

namespace rea {

/**
 * How eagerly the session converts: Seamtype's `AppPolicies().defaultPolicy`, the parts
 * [InputSession] reads. Not ported: per-app policies (apps.json), `enabled`, `holdSpace`
 * (terminals), and the switch key set beyond whether Esc is one (Key::Cycle always is).
 */
struct AppPolicy {
    /** The "strict" choice: convert only when quite sure (short shell commands and identifiers
     *  collide with Hangul easily). */
    static constexpr double STRICT_THRESHOLD = 0.97;

    /** Convert away from the typed English letters only when the engine is at least
     *  this confident. On the held-out set, raising it above 0.5 left more Korean
     *  words as letters without catching more wrong conversions, so it only pays
     *  off where a wrong conversion is expensive (terminals, editors). */
    double conversionThreshold = 0.5;
    /** Esc switches the word's language (as Key::Cycle does) instead of going to the app. */
    bool escapeSwitches = false;
    /** Full-width digits and spaces after Japanese (when turned on) apply here. */
    bool fullWidth = true;
};

} // namespace rea
