#include "RegisterAllocator.h"

#include <climits>
#include <set>
#include <stack>

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

    // First try with no spills
    {
        InterferenceGraph workingIg = ig;
        AllocationResult r = greedyColor(workingIg, K, 0);
        if (r.feasible) return r;
    }

    // Incrementally allow more spills (1, 2, … up to maxSpill)
    for (int allowed = 1; allowed <= maxSpill; allowed++) {
        InterferenceGraph workingIg = ig;
        AllocationResult r = greedyColor(workingIg, K, allowed);
        if (r.feasible) return r;
    }

    // Could not color even with maxSpill spills — return best effort
    InterferenceGraph workingIg = ig;
    return greedyColor(workingIg, K, maxSpill);
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

    // First try with no splits
    {
        InterferenceGraph workingIg = ig;
        AllocationResult r = greedyColor(workingIg, K, 0);
        if (r.feasible) return r;
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
        if (r.feasible) return r;
    }

    // Best effort on the final split graph
    return greedyColor(workingIg, K, 0);
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
    int K = config.numRegisters;
    InterferenceGraph workingIg = ig;
    const std::vector<Web>& webs = workingIg.getWebs();
    int W = (int)webs.size();

    std::vector<bool> disabled(W, false);
    std::vector<int>  spilledIds;
    std::stack<int>   stk;
    int active = W;

    // ── Phase 1: Smallest-last simplification ────────────────────────────
    while (active > 0) {
        // Find the active node with minimum effective degree
        int minId  = -1;
        int minDeg = INT_MAX;
        for (const Web& w : webs) {
            if (disabled[w.id]) continue;
            int deg = effectiveDegree(workingIg, w.id, disabled);
            if (deg < minDeg || (deg == minDeg && w.id < minId)) {
                minDeg = deg;
                minId  = w.id;
            }
        }
        if (minId == -1) break;

        if (minDeg < K) {
            // Safe to push — will always find a color on reinsertion
            disabled[minId] = true;
            stk.push(minId);
            active--;
        } else {
            // All remaining nodes have degree >= K; spill the worst one
            int victim = selectSpillCandidate(workingIg, disabled);
            if (victim == -1) break;
            disabled[victim] = true;
            spilledIds.push_back(victim);
            active--;
        }
    }

    // ── Phase 2: Coloring (mirrors greedyColor phase 2) ──────────────────
    std::fill(disabled.begin(), disabled.end(), true);
    for (int sid : spilledIds) disabled[sid] = true;

    std::vector<int>  color(W, -1);
    std::vector<bool> reinserted(W, false);

    while (!stk.empty()) {
        int id = stk.top(); stk.pop();
        reinserted[id] = true;

        std::set<int> usedColors;
        for (int nb : workingIg.getNeighbors(id))
            if (reinserted[nb] && color[nb] != -1)
                usedColors.insert(color[nb]);

        int chosen = -1;
        for (int c = 0; c < K; c++) {
            if (!usedColors.count(c)) { chosen = c; break; }
        }

        if (chosen == -1) spilledIds.push_back(id);
        else              color[id] = chosen;
    }

    // ── Build result ─────────────────────────────────────────────────────
    AllocationResult result;
    std::set<int> usedRegs;
    for (const Web& w : webs) {
        result.webToRegister[w.id] = color[w.id];
        if (color[w.id] != -1) usedRegs.insert(color[w.id]);
    }
    result.registersUsed = (int)usedRegs.size();
    result.feasible      = spilledIds.empty();
    return result;
}
