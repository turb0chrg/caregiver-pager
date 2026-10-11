// button_table.h — frame signature to human-readable button_id (FR-A2, FR-A3).

#pragma once

#include <stdint.h>

struct ButtonEntry {
    uint64_t    signature;  // trailing 64 bits of the frame; see decode rule 4
    const char* buttonId;   // stable machine id sent to the backend
    const char* label;      // human text used in the alert (FR-N3)
};

// Returns nullptr if the signature is not in the table.
const ButtonEntry* buttonLookup(uint64_t signature);

// Number of entries, for the startup banner and heartbeat.
uint8_t buttonTableSize();
