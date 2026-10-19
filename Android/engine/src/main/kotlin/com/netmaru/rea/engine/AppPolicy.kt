package com.netmaru.rea.engine

/**
 * How eagerly the session converts: Seamtype's `AppPolicies().defaultPolicy`, the parts
 * [InputSession] reads. Not ported: per-app policies (apps.json), `enabled`, `holdSpace`
 * (terminals), and the switch key set beyond whether Esc is one ([InputSession.Key.Cycle] always is).
 */
data class AppPolicy(
    /** Convert away from the typed English letters only when the engine is at least
     *  this confident. On the held-out set, raising it above 0.5 left more Korean
     *  words as letters without catching more wrong conversions, so it only pays
     *  off where a wrong conversion is expensive (terminals, editors). */
    val conversionThreshold: Double = 0.5,
    /** Esc switches the word's language (as [InputSession.Key.Cycle] does) instead of going to the app. */
    val escapeSwitches: Boolean = false,
    /** Full-width digits and spaces after Japanese (when turned on) apply here. */
    val fullWidth: Boolean = true,
) {
    companion object {
        /** The "strict" choice: convert only when quite sure (short shell commands and identifiers
         *  collide with Hangul easily). */
        const val STRICT_THRESHOLD = 0.97
    }
}
