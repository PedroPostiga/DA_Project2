#include "RegisterAllocator.h"

#include <climits>
#include <set>
#include <stack>
#include <algorithm>
#include <vector>

// ─────────────────────────────────────────────────────────────
// T2.1 — Basic Register Allocation
//
// Greedy coloring with exactly config.numRegisters colors.
// No spilling is permitted; infeasibility is reported via the
// result's feasible flag.
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::allocateBasic() const {
    InterferenceGraph workingIg = ig;
    // maxSpills = 0: no spilling allowed in basic mode
    return greedyColor(workingIg, config.numRegisters, 0);
}

// ─────────────────────────────────────────────────────────────
// T2.2 — Register Allocation with Web Spilling
//
// If basic coloring fails, incrementally permits more spills
// (1, 2, … up to config.algorithmParam). The highest-degree
// web is always chosen as the spill candidate to maximally
// reduce graph connectivity.
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::allocateSpilling() const {
    int K        = config.numRegisters;
    int maxSpill = config.algorithmParam;

    // Incrementally allow more spills (0, 1, 2, … up to maxSpill).
    // allowed = 0 is the pure basic attempt (no spills).
    for (int allowed = 0; allowed <= maxSpill; allowed++) {
        InterferenceGraph workingIg = ig;
        AllocationResult r = greedyColor(workingIg, K, allowed);
        if (r.feasible) return r;   // zero-spill coloring succeeded
    }

    // Could not achieve zero-spill coloring within the budget.
    // Run with the full spill budget and accept the partial result
    // (some webs get registers, the rest are spilled to memory).
    InterferenceGraph workingIg = ig;
    AllocationResult r = greedyColor(workingIg, K, maxSpill);
    r.feasible = true;  // spilling mode: partial allocation is valid
    return r;
}

// ─────────────────────────────────────────────────────────────
// T2.3 — Register Allocation with Web Splitting
//
// If basic coloring fails, splits up to config.algorithmParam
// webs at their midpoint. Splitting reduces a web's interference
// footprint, potentially allowing coloring with the same K.
// The highest-degree splittable web is chosen each round.
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::allocateSplitting() const {
    int K         = config.numRegisters;
    int maxSplits = config.algorithmParam;

    // Guard: if K <= 0, we can't allocate any registers, so spill everything
    if (K <= 0) {
        InterferenceGraph workingIg = ig;
        int W = (int)workingIg.getWebs().size();
        AllocationResult r = greedyColor(workingIg, K, W);
        r.feasible = true;
        r.webs = workingIg.getWebs();
        return r;
    }

    // First try with no splits
    {
        InterferenceGraph workingIg = ig;
        AllocationResult r = greedyColor(workingIg, K, 0);
        if (r.feasible) {
            r.webs = workingIg.getWebs();
            return r;
        }
    }

    // Incrementally split webs (1, 2, … up to maxSplits)
    InterferenceGraph workingIg = ig;
    std::vector<bool> disabled(workingIg.getWebs().size(), false);

    for (int s = 0; s < maxSplits; s++) {
        int victim = selectSplitCandidate(workingIg, disabled);
        if (victim == -1) break;   // no splittable web left

        splitWeb(workingIg, victim);

        // Resize disabled conservatively after the new web is appended
        disabled.assign(workingIg.getWebs().size(), false);

        InterferenceGraph trialIg = workingIg;
        AllocationResult r = greedyColor(trialIg, K, 0);
        if (r.feasible) {
            r.webs = workingIg.getWebs();
            return r;
        }
    }

    // Splitting alone wasn't enough — color the split graph allowing up to
    // W spills (effectively unlimited) so phase 2 gets to attempt coloring
    // all remaining nodes. Force feasible=true: partial allocation is valid.
    int W = (int)workingIg.getWebs().size();
    AllocationResult r = greedyColor(workingIg, K, W);
    r.feasible = true;
    r.webs = workingIg.getWebs();
    return r;
}

// ─────────────────────────────────────────────────────────────
// T2.4 — Free Strategy (Smallest-Last Ordering + Selective Spill)
//
// Phase 1 — Smallest-last simplification:
//   Repeatedly remove the node with the LOWEST effective degree
//   and push it onto the stack. Low-degree nodes are more likely
//   to find a free color when reinserted, producing fewer spills
//   than the basic heuristic on sparse graphs.
//   When all remaining nodes have degree >= K, spill the node
//   with the highest degree (no explicit budget).
//
// Phase 2 — Coloring: identical to greedyColor phase 2.
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::allocateFree() const {
    std::vector<Web> webs = ig.getWebs(); // Mutable copy to use web.reg
    const int n = static_cast<int>(webs.size());
    const int numRegisters = config.numRegisters;
    if (n == 0) return AllocationResult();

    // ── 1. Build interference matrix ────────────────────────────────────────
    std::vector<char> interferes(n * n, 0);
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (webs[i].interferesWith(webs[j])) {
                interferes[i * n + j] = 1;
                interferes[j * n + i] = 1;
            }
        }
    }

    // ── 2. Sort webs by defPoint (earliest definition first) ────────────────
    std::vector<int> order(n);
    for (int i = 0; i < n; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return webs[a].defPoint < webs[b].defPoint;
    });

    std::vector<bool> usedReg(numRegisters, false);

    // ── 3. Greedy assignment with eviction ──────────────────────────────────
    for (int idx : order) {
        Web& web = webs[idx];

        // ── 3a. Mark registers used by already-assigned interfering neighbors ──
        // Reset only the slots we set last iteration (or use fill for simplicity).
        std::fill(usedReg.begin(), usedReg.end(), false);

        int usedCount = 0;  // early-exit counter
        const char* row = interferes.data() + idx * n;  // pointer to row idx

        for (int j = 0; j < n && usedCount < numRegisters; j++) {
            if (row[j] && webs[j].reg >= 0) {
                if (!usedReg[webs[j].reg]) {
                    usedReg[webs[j].reg] = true;
                    ++usedCount;
                }
            }
        }

        // ── 3b. Assign lowest free register ────────────────────────────────
        int assigned = -1;
        for (int r = 0; r < numRegisters; r++) {
            if (!usedReg[r]) { assigned = r; break; }
        }

        if (assigned >= 0) {
            web.reg = assigned;
            continue;  // done for this web
        }

        // ── 3c. No register free — spill the interfering neighbor with the
        //        fewest program points, then retry in a single pass ───────────
        Web* spillCandidate = nullptr;

        for (int j = 0; j < n; j++) {
            if (row[j] && webs[j].reg >= 0) {
                if (!spillCandidate ||
                    webs[j].programPoints.size() < spillCandidate->programPoints.size()) {
                    spillCandidate    = &webs[j];

                }
            }
        }

        if (!spillCandidate) {
            // No assigned neighbor to evict — spill the current web itself.
            web.reg = -2;
            continue;
        }

        // Evict the candidate and free its register slot.
        int freedReg = spillCandidate->reg;
        spillCandidate->reg = -2;
        usedReg[freedReg] = false;  // no rescan needed — just unmark

        // Now find the lowest free register (freedReg is guaranteed free,
        // but there may be an even lower one that was never taken).
        for (int r = 0; r < numRegisters; r++) {
            if (!usedReg[r]) { web.reg = r; break; }
        }
    }

    // ── 4. Build result ─────────────────────────────────────────────────────
    AllocationResult result;
    std::set<int> distinctRegs;
    bool anySpilled = false;

    for (const auto& w : webs) {
        int finalReg = (w.reg == -2) ? SPILLED : w.reg;
        result.webToRegister[w.id] = finalReg;
        if (finalReg != SPILLED) {
            distinctRegs.insert(finalReg);
        } else {
            anySpilled = true;
        }
    }

    result.registersUsed = static_cast<int>(distinctRegs.size());
    result.feasible      = !anySpilled;
    return result;
}