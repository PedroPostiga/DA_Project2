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

std::pair<int, int> RegisterAllocator::selectSplitCandidate(const InterferenceGraph& workingIg,
                                            const std::vector<bool>& disabled) const {
    const std::vector<Web>& webs = workingIg.getWebs();
    int bestId = -1;
    int bestReduction = INT_MIN;
    int bestOrigDeg = -1;
    int globalBestSplit = -1;

    for (const Web& w : webs) {
        if (disabled[w.id]) continue;
        if ((int)w.programPoints.size() <= 1) continue;

        int origDeg = effectiveDegree(workingIg, w.id, disabled);
        std::vector<int> pts(w.programPoints.begin(), w.programPoints.end());

        Web first;
        first.id = w.id; first.variable = w.variable;
        first.defPoint = w.defPoint;

        Web second;
        second.id = (int)webs.size(); second.variable = w.variable;
        second.lastUsePoint = w.lastUsePoint;
        for (int p : pts) second.programPoints.insert(p);

        int bestCost = INT_MAX;
        int localBestSplit = -1;
        for (int split = 1; split < (int)pts.size(); split++) {
            first.lastUsePoint = pts[split - 1];
            second.defPoint = pts[split];
            first.programPoints.insert(pts[split - 1]);
            second.programPoints.erase(pts[split - 1]);

            int cost = 0;
            for (const Web& other : webs) {
                if (other.id == w.id) continue;
                if (first.interferesWith(other)) cost++;
                if (second.interferesWith(other)) cost++;
            }
            if (cost < bestCost) { 
                bestCost = cost; 
                localBestSplit = split; 
            }
        }

        int reduction = origDeg - bestCost;
        if (reduction > bestReduction ||
            (reduction == bestReduction && origDeg > bestOrigDeg) ||
            (reduction == bestReduction && origDeg == bestOrigDeg && w.id < bestId)) {
            bestReduction = reduction;
            bestOrigDeg = origDeg;
            bestId = w.id;
            globalBestSplit = localBestSplit;
        }
    }

    if (bestId == -1) {
        for (const Web& w : webs) {
            if (disabled[w.id]) continue;
            if ((int)w.programPoints.size() <= 1) continue;
            int deg = effectiveDegree(workingIg, w.id, disabled);
            if (bestId == -1 || deg > bestOrigDeg || (deg == bestOrigDeg && w.id < bestId)) {
                bestOrigDeg = deg; bestId = w.id;
                globalBestSplit = 1;
            }
        }
    }
    return {bestId, globalBestSplit};
}

void RegisterAllocator::splitWeb(InterferenceGraph& workingIg, int webId, int splitIndex) const {
    std::vector<Web> webs = workingIg.getWebs();
    Web& original = webs[webId];

    if ((int)original.programPoints.size() <= 1 || splitIndex < 1 || splitIndex >= (int)original.programPoints.size()) return;

    std::vector<int> pts(original.programPoints.begin(), original.programPoints.end());

    Web first;
    first.id = original.id; first.variable = original.variable;
    first.defPoint = original.defPoint; first.lastUsePoint = pts[splitIndex - 1];
    for (int i = 0; i < splitIndex; i++) first.programPoints.insert(pts[i]);

    Web second;
    second.id = (int)webs.size(); second.variable = original.variable;
    second.defPoint = pts[splitIndex]; second.lastUsePoint = original.lastUsePoint;
    for (int i = splitIndex; i < (int)pts.size(); i++) second.programPoints.insert(pts[i]);

    webs[webId] = first;
    webs.push_back(second);
    workingIg.build(webs);
}

// ─────────────────────────────────────────────────────────────
// greedyColor  (spec Figure 9)
//
// Phase 1: push nodes with degree < N onto stack.
//          If none exist and budget allows, spill the worst node
//          (removed entirely, no color assigned).
//          When budget is exhausted, push ALL remaining active
//          nodes onto the stack so phase 2 can still attempt to
//          color as many as possible.
//
// Phase 2: pop and assign lowest free color; if none available,
//          record as a phase-2 spill.
//
// maxSpills >= 0 : hard budget (0 = basic, K = spilling mode)
// maxSpills == -1: NEVER used — callers pass W (total webs) as
//                  an "unlimited" budget instead, which still
//                  triggers the budget-exhausted push so phase 2
//                  can color whatever is left.
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::greedyColor(InterferenceGraph& workingIg,
                                                int N,
                                                int maxSpills) const {
    const std::vector<Web>& webs = workingIg.getWebs();
    int W = (int)webs.size();

    std::vector<bool> disabled(W, false);
    std::vector<int>  spilledIds;
    std::stack<int>   stk;
    int spillsUsed = 0;
    int active     = W;

    // ── 0. Precompute current degrees ─────────────────────────────────────
    std::vector<int> currentDeg(W, 0);
    for (int i = 0; i < W; ++i) {
        currentDeg[i] = effectiveDegree(workingIg, i, disabled);
    }

    // ── Phase 1: Simplification ──────────────────────────────────────────
    while (active > 0) {
        int candidate = -1;
        for (int i = 0; i < W; ++i) {
            if (!disabled[i] && currentDeg[i] < N) {
                candidate = i;
                break;
            }
        }

        if (candidate != -1) {
            disabled[candidate] = true;
            stk.push(candidate);
            active--;
            for (int nb : workingIg.getNeighbors(candidate)) {
                if (!disabled[nb]) currentDeg[nb]--;
            }
            continue;
        }

        // No node has degree < N — check spill budget
        if (spillsUsed >= maxSpills) {
            // Budget exhausted: push remaining nodes onto stack so phase 2
            // can still attempt to color them. Push ascending by degree so
            // highest-degree nodes are popped first (colored first), giving
            // lower-degree nodes the best chance of finding a free color.
            std::vector<std::pair<int,int>> remaining;
            for (int i = 0; i < W; ++i) {
                if (!disabled[i]) {
                    remaining.emplace_back(currentDeg[i], i);
                }
            }
            std::sort(remaining.begin(), remaining.end());
            for (auto& [deg, id] : remaining) {
                disabled[id] = true;
                stk.push(id);
                active--;
            }
            break;
        }

        int victim = selectSpillCandidate(workingIg, disabled);
        if (victim == -1) break;

        disabled[victim] = true;
        spilledIds.push_back(victim);
        active--;
        spillsUsed++;
        for (int nb : workingIg.getNeighbors(victim)) {
            if (!disabled[nb]) currentDeg[nb]--;
        }
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
            spilledIds.push_back(id);
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
    result.feasible = spilledIds.empty();
    return result;
}