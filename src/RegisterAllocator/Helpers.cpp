#include "RegisterAllocator.h"

#include <algorithm>
#include <climits>
#include <set>
#include <stack>

// ─────────────────────────────────────────────────────────────
// effectiveDegree
//
// Counts only neighbors that are not in the disabled set,
// reflecting the degree of the node in the current active subgraph.
// ─────────────────────────────────────────────────────────────

int RegisterAllocator::effectiveDegree(const InterferenceGraph& workingIg,
                                       int webId,
                                       const std::vector<bool>& disabled) const {
    int deg = 0;
    for (int nb : workingIg.getNeighbors(webId))
        if (!disabled[nb]) deg++;
    return deg;
}

// ─────────────────────────────────────────────────────────────
// selectSpillCandidate
//
// Returns the active web with the highest effective degree.
// Ties broken by web ID (lower wins) for determinism.
// ─────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────
// selectSplitCandidate
//
// Like selectSpillCandidate, but only considers webs with more
// than one program point (single-point webs cannot be split).
// ─────────────────────────────────────────────────────────────

int RegisterAllocator::selectSplitCandidate(const InterferenceGraph& workingIg,
                                            const std::vector<bool>& disabled) const {
    int bestId  = -1;
    int bestDeg = -1;
    for (const Web& w : workingIg.getWebs()) {
        if (disabled[w.id]) continue;
        if ((int)w.programPoints.size() <= 1) continue;
        int deg = effectiveDegree(workingIg, w.id, disabled);
        if (deg > bestDeg || (deg == bestDeg && w.id < bestId)) {
            bestDeg = deg;
            bestId  = w.id;
        }
    }
    return bestId;
}

// ─────────────────────────────────────────────────────────────
// splitWeb
//
// Splits the chosen web at the midpoint of its sorted program
// points, creating two derived webs. The first half keeps the
// original web's ID; the second half is appended as a new web.
// The interference graph is rebuilt after the split.
// ─────────────────────────────────────────────────────────────

void RegisterAllocator::splitWeb(InterferenceGraph& workingIg, int webId) const {
    std::vector<Web> webs = workingIg.getWebs();

    Web& original = webs[webId];
    std::vector<int> pts(original.programPoints.begin(),
                         original.programPoints.end());
    // pts is already sorted (came from std::set)

    int mid = (int)pts.size() / 2;

    // First half: keeps the original ID and defPoint
    Web first;
    first.id           = original.id;
    first.variable     = original.variable;
    for (int i = 0; i < mid; i++)
        first.programPoints.insert(pts[i]);
    first.defPoint     = original.defPoint;
    first.lastUsePoint = pts[mid - 1];

    // Second half: new ID appended at the end of the webs vector
    Web second;
    second.id           = (int)webs.size();
    second.variable     = original.variable;
    for (int i = mid; i < (int)pts.size(); i++)
        second.programPoints.insert(pts[i]);
    second.defPoint     = pts[mid];           // synthetic definition at the split point
    second.lastUsePoint = original.lastUsePoint;

    webs[webId] = first;
    webs.push_back(second);

    workingIg.build(webs);
}

// ─────────────────────────────────────────────────────────────
// greedyColor  (spec Figure 9)
//
// Phase 1 — Simplification:
//   Repeatedly remove nodes with effective degree < N and push
//   them onto a stack. If no such node exists, spill the
//   highest-degree node (subject to maxSpills budget).
//
// Phase 2 — Coloring:
//   Pop nodes from the stack; assign the lowest color not used
//   by any already-reinserted neighbor.
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

    // ── Phase 1: Simplification ──────────────────────────────────────────
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
    // Re-enable all non-spilled nodes by resetting disabled;
    // spilled nodes remain permanently disabled.
    std::fill(disabled.begin(), disabled.end(), true);
    for (int sid : spilledIds)
        disabled[sid] = true;

    std::vector<int>  color(W, -1);
    std::vector<bool> reinserted(W, false);

    while (!stk.empty()) {
        int id = stk.top(); stk.pop();

        reinserted[id] = true;

        // Collect colors already used by reinserted neighbors
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
        if (chosen == -1) spilledIds.push_back(id);
        else              color[id] = chosen;
    }

    // ── Build result ─────────────────────────────────────────────────────
    AllocationResult result;
    std::set<int> usedRegs;

    for (const Web& w : webs) {
        result.webToRegister[w.id] = color[w.id];  // -1 == SPILLED
        if (color[w.id] != -1)
            usedRegs.insert(color[w.id]);
    }

    result.registersUsed = (int)usedRegs.size();
    result.feasible      = spilledIds.empty();

    return result;
}
