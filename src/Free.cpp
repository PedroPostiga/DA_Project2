#include "Parser.h"
#include "DataTypes.h"
#include "Free.h"
#include <vector>
#include <set>
#include <algorithm>

void freeAllocate(std::vector<Web>& webs, int numRegisters) {

    int n;
    n = webs.size();

    // building interference matrix
    std::vector interferes(n, std::vector(n, false));
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (webs[i].interferesWith(webs[j])) {
                interferes[i][j] = true;
                interferes[j][i] = true;
            }
        }
    }

    // Sort webs by defPoint — process earliest definitions first
    std::vector<int> order(n);
    for (int i = 0; i < n; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return webs[a].defPoint < webs[b].defPoint;
    });

    // assign registers in greedy way
    for (int idx : order) {
        Web& web = webs[idx];

        // Find registers used by interfering neighbors
        std::set<int> usedByNeighbors;
        for (int j = 0; j < n; j++) {
            if (interferes[idx][j] && webs[j].reg >= 0) {
                usedByNeighbors.insert(webs[j].reg);
            }
        }

        // Assign lowest numbered free register not used by neighbors
        bool assigned = false;
        for (int r = 0; r < numRegisters; r++) {
            if (!usedByNeighbors.count(r)) {
                web.reg = r;
                assigned = true;
                break;
            }
        }

        // If no register available, put smallest web in memory
        // that is already assigned and interferes with this one
        if (!assigned) {
            Web* spillCandidate = nullptr;
            for (int j = 0; j < n; j++) {
                if (interferes[idx][j] && webs[j].reg >= 0) {
                    if (!spillCandidate ||
                        webs[j].programPoints.size() < spillCandidate->programPoints.size()) {
                        spillCandidate = &webs[j];
                    }
                }
            }
            if (spillCandidate) {
                spillCandidate->reg = -2;  // spill to memory

                // retry assignment
                usedByNeighbors.clear();
                for (int j = 0; j < n; j++) {
                    if (interferes[idx][j] && webs[j].reg >= 0) {
                        usedByNeighbors.insert(webs[j].reg);
                    }
                }
                for (int r = 0; r < numRegisters; r++) {
                    if (!usedByNeighbors.count(r)) {
                        web.reg = r;
                        break;
                    }
                }
            } else {
                // no interfering web to spill — spill current web
                web.reg = -2;
            }
        }
    }
}

// Time complexity: O(W^2 * P + W^2 * R)
// W = number of webs, P = program points per web, R = number of registers