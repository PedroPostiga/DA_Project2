#pragma once

#include "DataTypes.h"

#include <map>
#include <string>
#include <vector>

/**
 * @brief Parses the live ranges input file and the algorithm configuration file.
 *
 * Responsibilities:
 *   1. Read and tokenize the live ranges file, producing LiveRange objects.
 *   2. Merge overlapping live ranges of the same variable into Web objects
 *      using a greedy algorithm.
 *   3. Read the algorithm configuration file (register count, algorithm type).
 *
 * The webs produced here become the nodes of the interference graph, which
 * is built separately by InterferenceGraph using Graph<int> as its base.
 *
 * ### Live Ranges File Format
 * @code
 * # comment
 * varName: lineNum+, lineNum, ..., lineNum-
 * @endcode
 *
 * ### Config File Format
 * @code
 * # comment
 * registers: N
 * algorithm: basic | spilling, K | splitting, K | free
 * @endcode
 *
 * Time complexity of full parse: O(V * R^2 * P * log P)
 *   V = variables, R = ranges per variable, P = program points per range.
 */
class Parser {
public:
    Parser() = default;

    /**
     * @brief Parses the live ranges input file.
     *
     * Reads variable names and their associated live ranges, applies the
     * '+'/'-' markers, then merges overlapping ranges for each variable
     * into webs. The resulting webs are retrievable via getWebs().
     *
     * Time complexity: O(V * R^2 * P * log P)
     *
     * @param filename Path to the live ranges file.
     * @return true if parsing succeeded, false on file or format error.
     */
    bool parseLiveRanges(const std::string& filename);

    /**
     * @brief Parses the algorithm configuration file.
     *
     * Reads the number of registers and the algorithm specification.
     * The result is retrievable via getConfig().
     *
     * Time complexity: O(L), L = number of lines in the file.
     *
     * @param filename Path to the configuration file.
     * @return true if parsing succeeded, false on file or format error.
     */
    bool parseConfig(const std::string& filename);

    /**
     * @brief Returns the webs produced after parsing and merging live ranges.
     * @return Const reference to the vector of webs.
     */
    const std::vector<Web>& getWebs() const;

    /**
     * @brief Returns the parsed algorithm configuration.
     * @return Const reference to the AlgorithmConfig struct.
     */
    const AlgorithmConfig& getConfig() const;

    /**
     * @brief Prints a human-readable summary of all webs to stdout.
     */
    void printWebs() const;

    /**
     * @brief Prints the algorithm configuration to stdout.
     */
    void printConfig() const;

private:
    std::vector<Web> webs;   ///< Final webs, one node each in the interference graph
    AlgorithmConfig config;  ///< Parsed algorithm configuration

    /**
     * @brief Parses one line of the live ranges file into a LiveRange.
     *
     * Handles the "varName: p1+, p2, p3-" format, strips whitespace,
     * and extracts the '+'/'-' markers from program point tokens.
     *
     * Time complexity: O(P log P)
     *
     * @param line     Raw input line.
     * @param varName  Output: variable name extracted from the line.
     * @param range    Output: the populated LiveRange.
     * @return true on success, false on format error.
     */
    bool parseLiveRangeLine(const std::string& line,
                            std::string& varName,
                            LiveRange& range);

    /**
     * @brief Merges a list of raw live ranges for one variable into webs.
     *
     * Greedy strategy: for each range, if it overlaps any existing web for
     * this variable, merge it in; otherwise create a new web. After each
     * merge, a fixup pass chains together any webs that now transitively
     * overlap. Handles the fusion rule: a range ending and another starting
     * on the same line (the "i = i + 1" case) are always fused.
     *
     * Time complexity: O(R^2 * P * log P)
     *
     * @param variable The variable name.
     * @param ranges   Raw live ranges for that variable (may be modified).
     */
    void mergeRangesIntoWebs(const std::string& variable,
                             std::vector<LiveRange>& ranges);

    /**
     * @brief Trims leading and trailing whitespace from a string.
     * @param s Input string.
     * @return Trimmed copy.
     */
    static std::string trim(const std::string& s);

    /**
     * @brief Splits a string by a delimiter, trimming each token.
     * @param s   Input string.
     * @param del Delimiter character.
     * @return Vector of trimmed tokens.
     */
    static std::vector<std::string> split(const std::string& s, char del);
};
