#include "RegisterAllocator.h"

#include <algorithm>
#include <climits>
#include <set>
#include <stack>

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
    int bestId = -1;
    int bestDeg = -1;
    double bestScore = -1.0;

    for (const Web& w : workingIg.getWebs()) {
        if (disabled[w.id]) continue;
        int deg = effectiveDegree(workingIg, w.id, disabled);
        int size = std::max(1, (int)w.programPoints.size());
        double score = static_cast<double>(deg) / size;
        if (bestId == -1 || score > bestScore ||
            (score == bestScore && (deg > bestDeg || (deg == bestDeg && w.id < bestId)))) {
            bestScore = score;
            bestDeg = deg;
            bestId = w.id;
        }
    }
    return bestId;
}

int RegisterAllocator::selectSplitCandidate(const InterferenceGraph& workingIg,
                                            const std::vector<bool>& disabled) const {
    const std::vector<Web>& webs = workingIg.getWebs();
    int bestId = -1;
    int bestReduction = INT_MIN;
    int bestOrigDeg = -1;

    for (const Web& w : webs) {
        if (disabled[w.id]) continue;
        if ((int)w.programPoints.size() <= 1) continue;

        int origDeg = effectiveDegree(workingIg, w.id, disabled);
        std::vector<int> pts(w.programPoints.begin(), w.programPoints.end());

        int bestCost = INT_MAX;
        for (int split = 1; split < (int)pts.size(); split++) {
            Web first;
            first.id = w.id; first.variable = w.variable;
            first.defPoint = w.defPoint; first.lastUsePoint = pts[split - 1];
            for (int i = 0; i < split; i++) first.programPoints.insert(pts[i]);

            Web second;
            second.id = (int)webs.size(); second.variable = w.variable;
            second.defPoint = pts[split]; second.lastUsePoint = w.lastUsePoint;
            for (int i = split; i < (int)pts.size(); i++) second.programPoints.insert(pts[i]);

            int cost = 0;
            for (const Web& other : webs) {
                if (other.id == w.id) continue;
                if (first.interferesWith(other)) cost++;
                if (second.interferesWith(other)) cost++;
            }
            bestCost = std::min(bestCost, cost);
        }

        int reduction = origDeg - bestCost;
        if (reduction > bestReduction ||
            (reduction == bestReduction && origDeg > bestOrigDeg) ||
            (reduction == bestReduction && origDeg == bestOrigDeg && w.id < bestId)) {
            bestReduction = reduction;
            bestOrigDeg = origDeg;
            bestId = w.id;
        }
    }

    if (bestId == -1) {
        for (const Web& w : webs) {
            if (disabled[w.id]) continue;
            if ((int)w.programPoints.size() <= 1) continue;
            int deg = effectiveDegree(workingIg, w.id, disabled);
            if (bestId == -1 || deg > bestOrigDeg || (deg == bestOrigDeg && w.id < bestId)) {
                bestOrigDeg = deg; bestId = w.id;
            }
        }
    }
    return bestId;
}

void RegisterAllocator::splitWeb(InterferenceGraph& workingIg, int webId) const {
    std::vector<Web> webs = workingIg.getWebs();
    Web& original = webs[webId];

    if ((int)original.programPoints.size() <= 1) return;

    std::vector<int> pts(original.programPoints.begin(), original.programPoints.end());

    int bestSplit = 1, bestCost = INT_MAX;
    for (int split = 1; split < (int)pts.size(); split++) {
        Web first;
        first.id = original.id; first.variable = original.variable;
        first.defPoint = original.defPoint; first.lastUsePoint = pts[split - 1];
        for (int i = 0; i < split; i++) first.programPoints.insert(pts[i]);

        Web second;
        second.id = (int)webs.size(); second.variable = original.variable;
        second.defPoint = pts[split]; second.lastUsePoint = original.lastUsePoint;
        for (int i = split; i < (int)pts.size(); i++) second.programPoints.insert(pts[i]);

        int cost = 0;
        for (const Web& other : webs) {
            if (other.id == webId) continue;
            if (first.interferesWith(other)) cost++;
            if (second.interferesWith(other)) cost++;
        }
        if (cost < bestCost) { bestCost = cost; bestSplit = split; }
    }

    Web first;
    first.id = original.id; first.variable = original.variable;
    first.defPoint = original.defPoint; first.lastUsePoint = pts[bestSplit - 1];
    for (int i = 0; i < bestSplit; i++) first.programPoints.insert(pts[i]);

    Web second;
    second.id = (int)webs.size(); second.variable = original.variable;
    second.defPoint = pts[bestSplit]; second.lastUsePoint = original.lastUsePoint;
    for (int i = bestSplit; i < (int)pts.size(); i++) second.programPoints.insert(pts[i]);

    webs[webId] = first;
    webs.push_back(second);
    workingIg.build(webs);
}

// ─────────────────────────────────────────────────────────────
// greedyColor  (spec Figure 9)
//
// Phase 1: push nodes with degree < N onto stack.
//          If none exist and budget allows, spill the highest-
//          degree node (remove it entirely, no color assigned).
//          When budget is exhausted, push ALL remaining active
//          nodes onto the stack (highest degree first) so phase 2
//          can still attempt to color as many as possible.
//
// Phase 2: pop and assign lowest free color; if none available,
//          spill (mark as -1).
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::greedyColor(InterferenceGraph& workingIg,
                                                int N,
                                                int maxSpills) const {
    const std::vector<Web>& webs = workingIg.getWebs();
    int W = (int)webs.size();

    std::vector<bool> disabled(W, false);
    std::vector<int>  spilledIds;   // webs spilled in phase 1 (no color attempted)
    std::stack<int>   stk;
    int spillsUsed = 0;
    int active     = W;

    // ── Phase 1: Simplification ──────────────────────────────────────────
    while (active > 0) {
        // Look for any active node with effective degree < N
        int candidate = -1;
        for (const Web& w : webs) {
            if (disabled[w.id]) continue;
            if (effectiveDegree(workingIg, w.id, disabled) < N) {
                candidate = w.id;
                break;
            }
        }

        if (candidate != -1) {
            disabled[candidate] = true;
            stk.push(candidate);
            active--;
            continue;
        }

        // No node has degree < N.
        // If we still have spill budget, spill the worst node.
        bool canSpill = (maxSpills < 0) || (spillsUsed < maxSpills);
        if (!canSpill) {
            // Budget exhausted: push remaining nodes onto stack so phase 2
            // can still try to color them (it will spill those it can't color).
            // Push in ascending degree order so highest-degree nodes are
            // popped first and colored first, giving lower-degree nodes
            // (which have fewer conflicts) a better chance at a free color.
            std::vector<std::pair<int,int>> remaining;
            for (const Web& w : webs) {
                if (disabled[w.id]) continue;
                remaining.emplace_back(effectiveDegree(workingIg, w.id, disabled), w.id);
            }
            std::sort(remaining.begin(), remaining.end()); // ascending degree → push low first → pop high first
            for (auto& [deg, id] : remaining) {
                disabled[id] = true;
                stk.push(id);
                active--;
            }
            break;
        }

        // Spill the highest-scoring candidate
        int victim = selectSpillCandidate(workingIg, disabled);
        if (victim == -1) break;

        disabled[victim] = true;
        spilledIds.push_back(victim);
        active--;
        spillsUsed++;
    }

    // ── Phase 2: Coloring ────────────────────────────────────────────────
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
        for (int c = 0; c < N; c++) {
            if (!usedColors.count(c)) { chosen = c; break; }
        }

        if (chosen == -1)
            spilledIds.push_back(id);  // phase-2 spill
        else
            color[id] = chosen;
    }

    AllocationResult result;
    std::set<int> usedRegs;
    for (const Web& w : webs) {
        result.webToRegister[w.id] = color[w.id];
        if (color[w.id] != -1)
            usedRegs.insert(color[w.id]);
    }

    result.registersUsed = (int)usedRegs.size();
    // feasible = true only if NO spills occurred at all (phase 1 or phase 2)
    result.feasible = spilledIds.empty();

    return result;
}