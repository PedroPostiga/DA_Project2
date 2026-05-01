#ifndef DATATYPES_H
#define DATATYPES_H

#include <set>
#include <string>
#include <vector>
#include <sstream>

/**
 * @brief Represents a single live range for a variable.
 *
 * A live range is a set of program points (line numbers) along which a
 * variable holds a specific value. It must have at least one definition
 * point (marked '+' in the input) and one last-use point (marked '-').
 *
 * Live ranges for the same variable may later be merged into a Web if
 * they share any program point.
 */
struct LiveRange {
    std::string variable;        ///< Variable name this range belongs to
    std::set<int> programPoints; ///< Sorted set of program line numbers
    int defPoint;                ///< Line where the variable is defined ('+'), -1 if none
    int lastUsePoint;            ///< Line where the variable is last used ('-'), -1 if none

    LiveRange();

    /**
     * @brief Checks whether this live range overlaps with another.
     *
     * Two live ranges overlap if they share at least one program point,
     * with the exception: if one range starts at point P due to a definition
     * and the other ends at P due to a last use, they do NOT overlap at P.
     * This models the "i = i + 1" pattern where the old value is consumed
     * and a new value is defined at the same instruction.
     *
     * Time complexity: O(min(|A|,|B|) * log(max(|A|,|B|)))
     *
     * @param other The other live range.
     * @return true if the ranges overlap (and should be fused into one web).
     */
    bool overlapsWith(const LiveRange& other) const;

    /**
     * @brief Merges another live range into this one in-place.
     *
     * Unions all program points and updates defPoint / lastUsePoint.
     *
     * Time complexity: O(m log n)
     *
     * @param other The live range to absorb.
     */
    void merge(const LiveRange& other);
};

/**
 * @brief Represents a web: the union of all overlapping live ranges for one variable.
 *
 * A web is the primary node type in the interference graph. Each web is
 * identified by a numeric ID (its index in the interference graph's vertex
 * set) and tracks the full union of program points contributed by all
 * merged live ranges.
 *
 * The interference graph is built as Graph<int> where the integer stored
 * in each vertex is the web ID.
 */
struct Web {
    int id;                      ///< Unique ID — matches the vertex info in Graph<int>
    std::string variable;        ///< Variable name this web represents
    std::set<int> programPoints; ///< Union of all program points across merged ranges
    int defPoint;                ///< Earliest definition point, -1 if none
    int lastUsePoint;            ///< Latest last-use point, -1 if none

    Web();
    Web(int id, const LiveRange& lr);

    /**
     * @brief Checks whether this web interferes with another.
     *
     * Two webs interfere if they are simultaneously live at any program point.
     * The same def-start / last-use-end exception from LiveRange applies here.
     *
     * Used to decide whether to add an edge between the two corresponding
     * vertices in the interference graph.
     *
     * Time complexity: O(min(|A|,|B|) * log(max(|A|,|B|)))
     *
     * @param other The other web.
     * @return true if an interference edge should be added between them.
     */
    bool interferesWith(const Web& other) const;

    /**
     * @brief Merges a live range into this web in-place.
     *
     * Time complexity: O(m log n)
     *
     * @param lr The live range to absorb.
     */
    void merge(const LiveRange& lr);

    /**
     * @brief Returns the web's program points as a formatted output string.
     *
     * Points are sorted in ascending order; the defPoint is suffixed with '+'
     * and the lastUsePoint is suffixed with '-'.
     *
     * Time complexity: O(n)
     *
     * @return e.g. "1+,2,3,4,5,6-"
     */
    std::string toString() const;
};

/**
 * @brief Holds the parsed algorithm configuration from the registers/config input file.
 *
 * Populated by Parser::parseConfig() and consumed by the register allocator.
 */
struct AlgorithmConfig {
    int numRegisters;    ///< Number of physical registers available (K)
    std::string algorithm;   ///< One of: "basic", "spilling", "splitting", "free"
    int algorithmParam;  ///< Parameter K for spilling/splitting; -1 if not applicable

    AlgorithmConfig();
};

#endif
