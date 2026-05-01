#include "Parser.h"
#include "DataTypes.h"
#include <vector>
#include "Free.h"

void freeAllocate(std::vector<Web>& webs, const std::vector<int>& timeline, int numRegisters) {

    // Initialize registers — all free
    std::vector<Register> registers;
    for (int i = 0; i < numRegisters; i++) {
        registers.push_back({i, "", false});
    }

    // Scan through timeline in execution order
    for (int point : timeline) {
        for (Web& web : webs) {
            if (!web.programPoints.contains(point)) continue;

            // Free before assign — handle endings first
            if (point == web.lastUsePoint && web.reg >= 0) {
                registers[web.reg].allocated = false;
                registers[web.reg].current_variable = "";
            }
        }

        for (Web& web : webs) {
            if (!web.programPoints.contains(point)) continue;

            // Web starts at this point — assign a free register
            if (point == web.defPoint && web.reg == -1) {
                for (Register& reg : registers) {
                    if (!reg.allocated) {
                        reg.allocated = true;
                        reg.current_variable = web.variable;
                        web.reg = reg.reg_id;
                        break;
                    }
                }

                // if no register was assigned, spill the shortest web
                if (web.reg == -1) {
                    Web* spillCandidate = nullptr;
                    for (Web& w : webs) {
                        if (w.reg >= 0) {
                            if (!spillCandidate || w.programPoints.size() < spillCandidate->programPoints.size()) {
                                spillCandidate = &w;
                            }
                        }
                    }
                    if (spillCandidate) {
                        registers[spillCandidate->reg].allocated = false;
                        spillCandidate->reg = -2;  // spilled to memory
                        // now assign the freed register to current web
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

// O(T * W * R)