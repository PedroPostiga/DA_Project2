#pragma once

#include "DataTypes.h"
#include "Graph.h"

#include <string>
#include <vector>
#include <map>

/**
 * @brief Builds and manages the web interference graph for register allocation.
 *
 * InterferenceGraph is a domain-specific layer on top of Graph<int>.
 * Each vertex in the underlying Graph<int> stores a web ID (int), and the
 * full Web data is kept in a parallel vector indexed by that same ID.
 *
 * ### Why Graph<int> and not Graph<Web>?
 * Web objects are large and mutable — they are modified during spilling and
 * splitting (T2.2, T2.3). Storing only the integer ID in the graph avoids
 * copying and allows the full Web data to be looked up in O(1) via the
 * vector, from any graph traversal result.
 *
 * ### Why two directed edges per interference?
 * Graph<T> models directed edges. An interference relation is symmetric
 * (if web A interferes with web B, B interferes with A), so we add one
 * edge in each direction. The degree of a node is then the size of its
 * adjacency list, which correctly reflects the number of interfering webs.
 *
 * Time complexity of build(): O(W^2 * P * log P)
 *   W = number of webs, P = average program points per web.
 */
class InterferenceGraph {
public:
    InterferenceGraph() = default;

    /**
     * @brief Builds the interference graph from a list of webs.
     *
     * For each web, adds a vertex with info = web.id to the underlying
     * Graph<int>. Then, for every pair of webs, calls Web::interferesWith()
     * and adds a bidirectional edge if they interfere.
     *
     * Any previously built graph is cleared before building.
     *
     * Time complexity: O(W^2 * P * log P)
     *
     * @param webs The webs produced by the Parser.
     */
    void build(const std::vector<Web>& webs);

    /**
     * @brief Returns the degree of a web's node in the interference graph.
     *
     * Degree = number of other webs that interfere with this one.
     * Since edges are stored as directed pairs, the degree equals the size
     * of the adjacency list of the corresponding vertex.
     *
     * Time complexity: O(1) after build.
     *
     * @param webId The web ID to query.
     * @return Degree of the node, or -1 if the web ID does not exist.
     */
    int getDegree(int webId) const;

    /**
     * @brief Returns the maximum degree across all nodes in the graph.
     *
     * Used by the greedy coloring algorithm to set the upper bound on N.
     *
     * Time complexity: O(W)
     *
     * @return Maximum degree, or 0 if the graph is empty.
     */
    int getMaxDegree() const;

    /**
     * @brief Checks whether two webs interfere (i.e., share an edge).
     *
     * Time complexity: O(degree(webId1))
     *
     * @param webId1 First web ID.
     * @param webId2 Second web ID.
     * @return true if an interference edge exists between them.
     */
    bool interferes(int webId1, int webId2) const;

    /**
     * @brief Returns the list of web IDs that interfere with a given web.
     *
     * Time complexity: O(degree(webId))
     *
     * @param webId The web ID to query.
     * @return Vector of interfering web IDs.
     */
    std::vector<int> getNeighbors(int webId) const;

    /**
     * @brief Returns the total number of webs (nodes) in the graph.
     * @return Number of vertices.
     */
    int getNumWebs() const;

    /**
     * @brief Returns the Web object for a given web ID.
     *
     * Time complexity: O(1)
     *
     * @param webId The web ID.
     * @return Const reference to the Web.
     */
    const Web& getWeb(int webId) const;

    /**
     * @brief Returns all webs in the graph.
     * @return Const reference to the webs vector.
     */
    const std::vector<Web>& getWebs() const;

    /**
     * @brief Provides direct access to the underlying Graph<int>.
     *
     * Exposed so that the coloring algorithm can use Graph's traversal
     * primitives (DFS, BFS, getVertexSet) directly when needed.
     *
     * @return Reference to the underlying Graph<int>.
     */
    Graph<int>& getGraph();
    const Graph<int>& getGraph() const;

    /**
     * @brief Prints a human-readable representation of the interference graph.
     *
     * Lists each web with its variable name, program points, and the IDs of
     * all webs it interferes with.
     */
    void print() const;

    /**
     * @brief Emits a DOT file for graphical visualization.
     *
     * Wraps Graph<int>::emitDOTFile, naming nodes by "webN (varName)"
     * for readability.
     *
     * @param filename Output filename (without .gv extension).
     */
    void emitDOT(const std::string& filename) const;

private:
    Graph<int> graph;          ///< Underlying graph; vertex info = web ID
    std::vector<Web> webs;     ///< Parallel web data, indexed by web ID
};
