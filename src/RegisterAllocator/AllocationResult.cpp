#include "RegisterAllocator.h"

// ─────────────────────────────────────────────────────────────
// AllocationResult
// ─────────────────────────────────────────────────────────────

AllocationResult::AllocationResult() : registersUsed(0), feasible(false) {}
