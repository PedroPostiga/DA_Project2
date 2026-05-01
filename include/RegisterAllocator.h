#ifndef REGISTERALLOCATOR_H
#define REGISTERALLOCATOR_H

#include "DataTypes.h"
#include "InterferenceGraph.h"

#include <map>
#include <string>
#include <vector>

/// Sentinel value meaning the web was spilled to memory, not assigned a register
static constexpr int SPILLED = -1;

/**
 * @brief Holds the result of a register allocation attempt.
 *
 * Maps each web ID to either a register number (0..K-1) or SPILLED (-1).
 * Also records how many registers were actually used and whether the
 * allocation succeeded within the requested register budget.
 */
struct AllocationResult {
    std::map<int, int> webToRegister; ///< web ID → register number, or SPILLED
    int registersUsed;                ///< Number of distinct registers actually assigned
    bool feasible;                    ///< true if all webs got a register (no spills forced)

    AllocationResult();
};

/**
 * @brief Performs register allocation via graph coloring on an InterferenceGraph.
 *
 * Implements four allocation strategies as required by the project spec:
 *
 *   - **basic**    (T2.1): Greedy coloring of the interference graph with N colors.
 *                          Reports failure if K registers are not enough.
 *
 *   - **spilling** (T2.2): If basic coloring fails with K registers, selects up to
 *                          K webs to spill to memory (removing their nodes from the
 *                          graph), then retries coloring on the simplified graph.
 *
 *   - **splitting** (T2.3): If basic coloring fails with K registers, splits up to
 *                           K webs into two derived webs with fewer interferences,
 *                           then retries coloring on the modified graph.
 *
 *   - **free**     (T2.4): A custom strategy combining degree-ordered coloring with
 *                          selective spilling, aiming to minimise the number of
 *                          registers used without an explicit spill budget.
 *
 * ### Algorithm overview (greedy coloring, Figure 9 of the spec)
 *
 * Phase 1 — Simplification:
 *   While the graph is not empty, repeatedly remove nodes whose degree < N
 *   and push them onto a stack. If all remaining nodes have degree >= N,
 *   select one node to spill and remove it (no register assigned).
 *   Nodes are "removed" by marking them disabled — the underlying Graph<int>
 *   is not structurally modified; degrees are recomputed on the active subgraph.
 *
 * Phase 2 — Coloring:
 *   Pop nodes from the stack and assign each the lowest-numbered color not
 *   already used by any of its active neighbors. Because the node had degree
 *   < N when it was pushed, a free color always exists.
 *
 * Time complexity of allocateBasic(): O(W^2 * N)
 *   W = number of webs, N = number of registers.
 */
class RegisterAllocator {
public:
    /**
     * @brief Constructs the allocator with a pre-built interference graph
     *        and the algorithm configuration.
     *
     * @param ig     The interference graph (will be copied internally so the
     *               original is never modified).
     * @param config The parsed algorithm configuration.
     */
    RegisterAllocator(const InterferenceGraph& ig, const AlgorithmConfig& config);

    /**
     * @brief Runs the allocation algorithm specified in the config.
     *
     * Dispatches to allocateBasic(), allocateSpilling(), allocateSplitting(),
     * or allocateFree() based on config.algorithm.
     *
     * @return The allocation result.
     */
    AllocationResult allocate();

    /**
     * @brief Writes the allocation result to a file in the spec output format.
     *
     * Outputs the webs section followed by the register assignment section.
     * Webs assigned to memory are written as "M: webN".
     * Issues a warning to stderr if the allocation was not feasible.
     *
     * Time complexity: O(W log W)
     *
     * @param result   The allocation result to write.
     * @param filename Output file path.
     * @return true on success, false if the file could not be opened.
     */
    bool writeOutput(const AllocationResult& result,
                     const std::string& filename) const;

    /**
     * @brief Prints the allocation result to stdout.
     * @param result The allocation result to display.
     */
    void printResult(const AllocationResult& result) const;

private:
    InterferenceGraph ig;      ///< Working copy of the interference graph
    AlgorithmConfig   config;  ///< Algorithm configuration

    // ── Core algorithms ───────────────────────────────────────────────────

    /**
     * @brief Greedy graph coloring with N colors (spec Figure 9).
     *
     * Runs one pass of the simplification + coloring algorithm on the
     * provided working graph. Nodes whose degree drops below N are pushed
     * onto a stack; nodes that cannot be simplified are spilled.
     *
     * @param workingIg  A copy of the interference graph to operate on.
     * @param N          Number of colors (registers) to attempt.
     * @param maxSpills  Maximum number of nodes allowed to be spilled
     *                   during simplification (-1 = unlimited).
     * @return AllocationResult with register assignments and spill markers.
     */
    AllocationResult greedyColor(InterferenceGraph& workingIg,
                                 int N,
                                 int maxSpills) const;

    /**
     * @brief T2.1 — Basic register allocation.
     *
     * Attempts greedy coloring with exactly config.numRegisters colors.
     * No spilling is allowed; reports infeasibility if coloring fails.
     *
     * Time complexity: O(W^2 * K)
     *
     * @return AllocationResult.
     */
    AllocationResult allocateBasic() const;

    /**
     * @brief T2.2 — Register allocation with web spilling.
     *
     * First attempts basic allocation. If it fails, iteratively spills
     * webs (up to config.algorithmParam at a time) until coloring succeeds.
     *
     * Spill selection heuristic: the web with the highest degree is spilled
     * first, as removing a high-degree node most reduces graph connectivity.
     *
     * Time complexity: O(S * W^2 * K) where S = number of spills attempted.
     *
     * @return AllocationResult.
     */
    AllocationResult allocateSpilling() const;

    /**
     * @brief T2.3 — Register allocation with web splitting.
     *
     * First attempts basic allocation. If it fails, iteratively splits
     * webs (up to config.algorithmParam at a time) by dividing a web's
     * program points into two halves, creating two derived webs with
     * potentially fewer interferences, then rebuilds and retries.
     *
     * Split selection heuristic: the web with the highest degree is split
     * first, as it has the most interferences to potentially reduce.
     *
     * Time complexity: O(S * W^2 * K) where S = number of splits attempted.
     *
     * @return AllocationResult.
     */
    AllocationResult allocateSplitting() const;

    /**
     * @brief T2.4 — Custom register allocation strategy.
     *
     * Combines smallest-last vertex ordering (nodes with lower degree are
     * colored last and pushed first) with selective spilling of the
     * highest-degree nodes when the graph cannot be colored. This tends to
     * produce fewer spills than the basic heuristic for dense graphs.
     *
     * Time complexity: O(W^2 * K)
     *
     * @return AllocationResult.
     */
    AllocationResult allocateFree() const;

    // ── Helpers ───────────────────────────────────────────────────────────

    /**
     * @brief Selects the best web to spill from the active set.
     *
     * Returns the web ID with the highest degree among active (non-disabled)
     * nodes in the working graph. Ties are broken by web ID (lower wins).
     *
     * @param workingIg  The current working graph.
     * @param disabled   Set of web IDs already removed from consideration.
     * @return Web ID to spill, or -1 if no active nodes remain.
     */
    int selectSpillCandidate(const InterferenceGraph& workingIg,
                             const std::vector<bool>& disabled) const;

    /**
     * @brief Selects the best web to split from the active set.
     *
     * Returns the web ID with the highest degree that also has more than
     * one program point (a single-point web cannot be split further).
     *
     * @param workingIg  The current working graph.
     * @param disabled   Set of web IDs already removed from consideration.
     * @return Web ID to split, or -1 if no splittable web exists.
     */
    int selectSplitCandidate(const InterferenceGraph& workingIg,
                             const std::vector<bool>& disabled) const;

    /**
     * @brief Splits a web into two derived webs at the midpoint of its
     *        sorted program points.
     *
     * The first half keeps the original web's ID and defPoint; the second
     * half receives a new ID and the lastUsePoint. The interference graph
     * is rebuilt after splitting.
     *
     * @param workingIg  The interference graph to modify.
     * @param webId      The web to split.
     */
    void splitWeb(InterferenceGraph& workingIg, int webId) const;

    /**
     * @brief Computes the effective degree of a node in the active subgraph.
     *
     * Counts only neighbors that are not in the disabled set.
     *
     * @param workingIg  The working graph.
     * @param webId      The web to query.
     * @param disabled   Set of disabled web IDs.
     * @return Effective degree.
     */
    int effectiveDegree(const InterferenceGraph& workingIg,
                        int webId,
                        const std::vector<bool>& disabled) const;
};


#endif //REGISTERALLOCATOR_H
