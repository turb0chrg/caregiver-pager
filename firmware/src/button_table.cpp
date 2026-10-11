#include "button_table.h"

#include <stddef.h>

// Signatures captured with RTL-SDR + rtl_433 and recorded in
// docs/RF-VERIFICATION.md §7. These are the trailing 64 bits of the ~68-bit
// frame; the leading nibble rtl_433 prints ("d...") is deliberately excluded
// because it shifts with sync-gap alignment between repeats.
//
// Adding a button: run the node with LOG_RAW_FRAMES enabled, press the new
// unit, and copy the signature the console prints for an unmatched frame.
static const ButtonEntry kButtons[] = {
    { 0x0076803B401DA00EULL, "unit-1", "Bedroom call button" },

    // Unit 2 is intentionally absent. It is hardware-faulty: it radiates
    // strongly (RSSI ~ -7 dB) but never completes a frame, and has been
    // retired. Decode rule 2 already guarantees its fragments produce nothing,
    // so this is belt-and-braces. Do not add a signature for it if one ever
    // appears — that would mean the fragment threshold is wrong, not that the
    // unit recovered.

    { 0x3BC29DE14EF0A778ULL, "unit-3", "Living room call button" },
};

static const uint8_t kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);

const ButtonEntry* buttonLookup(uint64_t signature) {
    for (uint8_t i = 0; i < kButtonCount; i++) {
        if (kButtons[i].signature == signature) return &kButtons[i];
    }
    return nullptr;
}

uint8_t buttonTableSize() { return kButtonCount; }
