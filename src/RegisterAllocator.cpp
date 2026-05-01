#include "RegisterAllocator.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <stack>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────
// AllocationResult
// ─────────────────────────────────────────────────────────────

AllocationResult::AllocationResult() : registersUsed(0), feasible(false) {}

// ─────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────

RegisterAllocator::RegisterAllocator(const InterferenceGraph& ig,
                                     const AlgorithmConfig& config)
    : ig(ig), config(config) {}

// ─────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::allocate() {
    if      (config.algorithm == "basic")    return allocateBasic();
    else if (config.algorithm == "spilling") return allocateSpilling();
    else if (config.algorithm == "splitting")return allocateSplitting();
    else if (config.algorithm == "free")     return allocateFree();
    else {
        std::cerr << "[RegisterAllocator] Unknown algorithm '"
                  << config.algorithm << "' — falling back to basic.\n";
        return allocateBasic();
    }
}

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────

int RegisterAllocator::effectiveDegree(const InterferenceGraph& workingIg,
                                       int webId,
                                       const std::vector<bool>& disabled) const {
    int deg = 0;
    for (int nb : workingIg.getNeighbors(webId))
        if (!disabled[nb]) deg++;
    return deg;
}

int RegisterAllocator::selectSpillCandidate(const InterferenceGraph& workingIg,
                                            const std::vector<bool>& disabled) const {
    int bestId  = -1;
    int bestDeg = -1;
    for (const Web& w : workingIg.getWebs()) {
        if (disabled[w.id]) continue;
        int deg = effectiveDegree(workingIg, w.id, disabled);
        if (deg > bestDeg || (deg == bestDeg && w.id < bestId)) {
            bestDeg = deg;
            bestId  = w.id;
        }
    }
    return bestId;
}

int RegisterAllocator::selectSplitCandidate(const InterferenceGraph& workingIg,
                                            const std::vector<bool>& disabled) const {
    int bestId  = -1;
    int bestDeg = -1;
    for (const Web& w : workingIg.getWebs()) {
        if (disabled[w.id]) continue;
        if ((int)w.programPoints.size() <= 1) continue;  // cannot split a single-point web
        int deg = effectiveDegree(workingIg, w.id, disabled);
        if (deg > bestDeg || (deg == bestDeg && w.id < bestId)) {
            bestDeg = deg;
            bestId  = w.id;
        }
    }
    return bestId;
}

void RegisterAllocator::splitWeb(InterferenceGraph& workingIg, int webId) const {
    // Take a copy of the webs, modify, and rebuild the graph
    std::vector<Web> webs = workingIg.getWebs();

    Web& original = webs[webId];
    std::vector<int> pts(original.programPoints.begin(),
                         original.programPoints.end());
    // pts is already sorted (came from std::set)

    int mid = (int)pts.size() / 2;

    // First half keeps the original web's ID and defPoint
    Web first;
    first.id       = original.id;
    first.variable = original.variable;
    for (int i = 0; i < mid; i++)
        first.programPoints.insert(pts[i]);
    first.defPoint     = original.defPoint;
    // lastUsePoint is the last point in the first half
    first.lastUsePoint = pts[mid - 1];

    // Second half gets a new ID appended at the end of the webs vector
    Web second;
    second.id       = (int)webs.size();
    second.variable = original.variable;
    for (int i = mid; i < (int)pts.size(); i++)
        second.programPoints.insert(pts[i]);
    second.defPoint     = pts[mid];   // starts with a definition at the split point
    second.lastUsePoint = original.lastUsePoint;

    // Replace the original web with the first half, append the second half
    webs[webId] = first;
    webs.push_back(second);

    // Rebuild the interference graph with the updated webs
    workingIg.build(webs);
}

// ─────────────────────────────────────────────────────────────
// Core greedy coloring (spec Figure 9)
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::greedyColor(InterferenceGraph& workingIg,
                                                int N,
                                                int maxSpills) const {
    const std::vector<Web>& webs = workingIg.getWebs();
    int W = (int)webs.size();

    // disabled[id] = true means the node has been removed from the active graph
    std::vector<bool> disabled(W, false);
    // spilledIds tracks nodes removed due to degree >= N (no color assigned)
    std::vector<int>  spilledIds;
    // The simplification stack
    std::stack<int>   stk;

    int spillsUsed = 0;

    // ── Phase 1: Simplification ──────────────────────────────────────────
    int active = W;
    while (active > 0) {
        bool progress = false;

        // Remove all nodes with effective degree < N
        for (const Web& w : webs) {
            if (disabled[w.id]) continue;
            if (effectiveDegree(workingIg, w.id, disabled) < N) {
                disabled[w.id] = true;
                stk.push(w.id);
                active--;
                progress = true;
            }
        }

        if (!progress && active > 0) {
            // All remaining nodes have degree >= N — must spill one
            if (maxSpills >= 0 && spillsUsed >= maxSpills) {
                // Spill budget exhausted: mark all remaining nodes as spilled
                for (const Web& w : webs) {
                    if (!disabled[w.id]) {
                        disabled[w.id] = true;
                        spilledIds.push_back(w.id);
                        active--;
                    }
                }
                break;
            }

            int victim = selectSpillCandidate(workingIg, disabled);
            if (victim == -1) break;

            disabled[victim] = true;
            spilledIds.push_back(victim);
            active--;
            spillsUsed++;
        }
    }

    // ── Phase 2: Coloring ────────────────────────────────────────────────
    // Re-enable all non-spilled nodes; color by popping the stack.
    std::fill(disabled.begin(), disabled.end(), true);
    for (int sid : spilledIds)
        disabled[sid] = true;   // spilled nodes stay disabled

    // color[id] = assigned register (-1 = not yet colored)
    std::vector<int> color(W, -1);

    // Re-enable nodes that are on the stack (in reverse push order)
    // We need to process them in pop order, re-enabling as we go.
    // Build a temp vector from the stack for re-enabling tracking.
    std::vector<int> stackOrder;
    {
        std::stack<int> tmp = stk;
        while (!tmp.empty()) { stackOrder.push_back(tmp.top()); tmp.pop(); }
    }
    // stackOrder[0] is the top of the stack (last pushed = first popped)

    // Track which nodes are "re-inserted" into the active graph
    std::vector<bool> reinserted(W, false);
    // Spilled nodes are never re-inserted
    for (int sid : spilledIds) reinserted[sid] = false;

    while (!stk.empty()) {
        int id = stk.top(); stk.pop();

        // Re-insert this node: mark it active
        reinserted[id] = true;

        // Find colors used by already-reinserted neighbors
        std::set<int> usedColors;
        for (int nb : workingIg.getNeighbors(id))
            if (reinserted[nb] && color[nb] != -1)
                usedColors.insert(color[nb]);

        // Assign the lowest available color
        int chosen = -1;
        for (int c = 0; c < N; c++) {
            if (!usedColors.count(c)) { chosen = c; break; }
        }

        // chosen should always be found because degree was < N when pushed;
        // if for some reason it isn't, spill this node too.
        if (chosen == -1) {
            spilledIds.push_back(id);
        } else {
            color[id] = chosen;
        }
    }

    // ── Build result ─────────────────────────────────────────────────────
    AllocationResult result;
    std::set<int> usedRegs;

    for (const Web& w : webs) {
        result.webToRegister[w.id] = color[w.id];  // -1 = SPILLED
        if (color[w.id] != -1)
            usedRegs.insert(color[w.id]);
    }

    result.registersUsed = (int)usedRegs.size();
    result.feasible      = spilledIds.empty();

    return result;
}

// ─────────────────────────────────────────────────────────────
// T2.1 — Basic
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::allocateBasic() const {
    InterferenceGraph workingIg = ig;   // work on a copy
    AllocationResult result = greedyColor(workingIg, config.numRegisters, 0);
    // maxSpills = 0: no spilling allowed in basic mode
    return result;
}

// ─────────────────────────────────────────────────────────────
// T2.2 — Spilling
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::allocateSpilling() const {
    int K        = config.numRegisters;
    int maxSpill = config.algorithmParam;   // max webs we may spill in total

    // First try basic with no spills
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

    // Could not color even with maxSpill spills — return the best effort
    // (all remaining uncolorable webs are marked SPILLED)
    InterferenceGraph workingIg = ig;
    return greedyColor(workingIg, K, maxSpill);
}

// ─────────────────────────────────────────────────────────────
// T2.3 — Splitting
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::allocateSplitting() const {
    int K         = config.numRegisters;
    int maxSplits = config.algorithmParam;

    // First try basic with no splits
    {
        InterferenceGraph workingIg = ig;
        AllocationResult r = greedyColor(workingIg, K, 0);
        if (r.feasible) return r;
    }

    // Incrementally split webs (1, 2, … up to maxSplits)
    InterferenceGraph workingIg = ig;   // persistent working copy across splits
    std::vector<bool> disabled(workingIg.getWebs().size(), false);

    for (int s = 0; s < maxSplits; s++) {
        // Find the best split candidate among active webs
        int victim = selectSplitCandidate(workingIg, disabled);
        if (victim == -1) break;   // no splittable web left

        splitWeb(workingIg, victim);

        // After splitting, the disabled vector may be stale (new web added);
        // resize it conservatively
        disabled.assign(workingIg.getWebs().size(), false);

        // Try coloring the modified graph
        InterferenceGraph trialIg = workingIg;
        AllocationResult r = greedyColor(trialIg, K, 0);
        if (r.feasible) return r;
    }

    // Best effort with the split graph
    return greedyColor(workingIg, K, 0);
}

// ─────────────────────────────────────────────────────────────
// T2.4 — Free (smallest-last ordering + selective spill)
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::allocateFree() const {
    // Smallest-last ordering: repeatedly remove the node with the LOWEST
    // effective degree and push it onto the stack. This tends to produce
    // better colorings than the basic heuristic for sparse graphs because
    // low-degree nodes are more likely to find a free color when reinserted.
    //
    // When all remaining nodes have degree >= K, we spill the highest-degree
    // node (same heuristic as spilling, but without an explicit budget).

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
        // Find node with minimum effective degree
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
            // All remaining nodes have degree >= K, must spill the worst one
            int victim = selectSpillCandidate(workingIg, disabled);
            if (victim == -1) break;
            disabled[victim] = true;
            spilledIds.push_back(victim);
            active--;
        }
    }

    // ── Phase 2: Coloring (same as greedyColor phase 2) ──────────────────
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

// ─────────────────────────────────────────────────────────────
// Output
// ─────────────────────────────────────────────────────────────

bool RegisterAllocator::writeOutput(const AllocationResult& result,
                                    const std::string& filename) const {
    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "[RegisterAllocator] Error: cannot open output file '"
                  << filename << "'\n";
        return false;
    }

    const std::vector<Web>& webs = ig.getWebs();

    // ── Webs section ─────────────────────────────────────────────────────
    out << "# Total number of webs followed by the listing of the program points of each one\n";
    out << "# program points in each web are sorted in ascending order\n";
    out << "webs: " << webs.size() << "\n";
    for (const Web& w : webs)
        out << "web" << w.id << ": " << w.toString() << "\n";

    out << "# Total number of registers used, followed by assignment to webs\n";

    if (!result.feasible) {
        std::cerr << "[RegisterAllocator] Warning: register allocation was not possible "
                     "with the provided number of registers (" << config.numRegisters << ").\n";
        out << "registers: 0\n";
        for (const Web& w : webs)
            out << "M: web" << w.id << "\n";
    } else {
        out << "registers: " << result.registersUsed << "\n";

        // Group webs by register for clean output
        std::map<int, std::vector<int>> regToWebs;
        for (const auto& [wid, reg] : result.webToRegister) {
            if (reg == SPILLED) regToWebs[-1].push_back(wid);
            else                regToWebs[reg].push_back(wid);
        }

        for (auto& [reg, wids] : regToWebs) {
            std::sort(wids.begin(), wids.end());
            for (int wid : wids) {
                if (reg == SPILLED) out << "M: web"  << wid << "\n";
                else                out << "r" << reg << ": web" << wid << "\n";
            }
        }
    }

    out.close();
    return true;
}

void RegisterAllocator::printResult(const AllocationResult& result) const {
    const std::vector<Web>& webs = ig.getWebs();

    std::cout << "\n# Register Allocation Result\n";
    std::cout << "webs: " << webs.size() << "\n";
    for (const Web& w : webs)
        std::cout << "web" << w.id << " (" << w.variable << "): "
                  << w.toString() << "\n";

    if (!result.feasible) {
        std::cerr << "Warning: allocation not feasible with "
                  << config.numRegisters << " register(s).\n";
        std::cout << "registers: 0\n";
        for (const Web& w : webs)
            std::cout << "M: web" << w.id << "\n";
        return;
    }

    std::cout << "registers: " << result.registersUsed << "\n";

    std::map<int, std::vector<int>> regToWebs;
    for (const auto& [wid, reg] : result.webToRegister) {
        if (reg == SPILLED) regToWebs[-1].push_back(wid);
        else                regToWebs[reg].push_back(wid);
    }

    for (auto& [reg, wids] : regToWebs) {
        std::sort(wids.begin(), wids.end());
        for (int wid : wids) {
            if (reg == SPILLED)
                std::cout << "M: web" << wid
                          << " (" << webs[wid].variable << ")\n";
            else
                std::cout << "r" << reg << ": web" << wid
                          << " (" << webs[wid].variable << ")\n";
        }
    }
}
