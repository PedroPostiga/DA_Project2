#include "Parser.h"
#include "DataTypes.h"

#include <vector>
#include <unordered_map>

/**
 * @brief Register allocation using an improved linear-scan algorithm.
 *
 *  - Scan the timeline in execution order.
 *  - At each point, free registers from webs that end there first.
 *  - Then assign registers to webs that start (are defined) there.
 *  - If no register is free, spill the currently active web with the
 *    fewest program points
 *
 *   Instead of scanning ALL webs at every timeline point to check
 *   programPoints.contains(point), we pre-build two maps:
 *     - endings[point]   → list of webs whose lastUsePoint == point
 *     - startings[point] → list of webs whose defPoint     == point
 *   This way, each timeline point only touches the webs that actually
 *   start or end there, instead of checking every web every time.
 *
 * Time complexity: O(T + W) 
 *   T = timeline length, W = number of webs, R = number of registers
 *   (pre-building the maps is O(W), scanning the timeline is O(T + W)
 *    since across all points the total work equals the number of webs)
 *
 * @param webs         Webs produced by the Parser (modified in-place: web.reg set).
 * @param timeline     Program points in execution order.
 * @param numRegisters Number of available physical registers.
 */
void freeAllocate(std::vector<Web>& webs,
                  const std::vector<int>& timeline,
                  int numRegisters) {

    // ── 1. Initialize registers — all free (same as original) ────────────────
    std::vector<Register> registers;
    for (int i = 0; i < numRegisters; i++)
        registers.push_back({i, "", false});

    // ── 2. Pre-build point → web maps (the key improvement) ──────────────────
    // endings[p]   = indices of webs whose lastUsePoint == p
    // startings[p] = indices of webs whose defPoint     == p
    std::unordered_map<int, std::vector<int>> endings;
    std::unordered_map<int, std::vector<int>> startings;

    for (int i = 0; i < (int)webs.size(); i++) {
        if (webs[i].lastUsePoint != -1)
            endings[webs[i].lastUsePoint].push_back(i);
        if (webs[i].defPoint != -1)
            startings[webs[i].defPoint].push_back(i);
    }

    // ── 3. Scan the timeline (same structure as original) ────────────────────
    for (int point : timeline) {

        // Pass 1: free registers from webs ending at this point (same as original)
        if (endings.count(point)) {
            for (int i : endings[point]) {
                Web& web = webs[i];
                if (web.reg >= 0) {
                    registers[web.reg].allocated = false;
                    registers[web.reg].current_variable = "";
                }
            }
        }

        // Pass 2: assign registers to webs starting at this point (same as original)
        if (startings.count(point)) {
            for (int i : startings[point]) {
                Web& web = webs[i];
                if (web.reg != -1) continue;  // already assigned (shouldn't happen)

                // Try to find a free register (same as original)
                for (Register& reg : registers) {
                    if (!reg.allocated) {
                        reg.allocated = true;
                        reg.current_variable = web.variable;
                        web.reg = reg.reg_id;
                        break;
                    }
                }

                // If no register was assigned, spill the shortest web (same as original)
                if (web.reg == -1) {
                    Web* spillCandidate = nullptr;
                    for (Web& w : webs) {
                        if (w.reg >= 0) {
                            if (!spillCandidate ||
                                w.programPoints.size() < spillCandidate->programPoints.size())
                                spillCandidate = &w;
                        }
                    }
                    if (spillCandidate) {
                        registers[spillCandidate->reg].allocated = false;
                        spillCandidate->reg = -2;  // spilled to memory
                        for (Register& reg : registers) {
                            if (!reg.allocated) {
                                reg.allocated = true;
                                reg.current_variable = web.variable;
                                web.reg = reg.reg_id;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

// Time complexity: O(T + W) 