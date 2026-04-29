#include "InterferenceGraph.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────
// Build
// ─────────────────────────────────────────────────────────────

void InterferenceGraph::build(const std::vector<Web>& inputWebs) {
    // Clear any previously built state
    // Graph<T> has no clear(), so we replace it with a fresh instance
    graph = Graph<int>();
    webs  = inputWebs;

    // Step 1: add one vertex per web (vertex info = web ID)
    for (const Web& w : webs)
        graph.addVertex(w.id);

    // Step 2: for every pair of webs, add a bidirectional edge if they interfere
    // We iterate over the upper triangle only and add both directions,
    // avoiding duplicate interference checks.
    for (int i = 0; i < (int)webs.size(); i++) {
        for (int j = i + 1; j < (int)webs.size(); j++) {
            if (webs[i].interferesWith(webs[j])) {
                graph.addEdge(webs[i].id, webs[j].id, 1.0);  // i → j
                graph.addEdge(webs[j].id, webs[i].id, 1.0);  // j → i (symmetric)
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────

int InterferenceGraph::getDegree(int webId) const {
    Vertex<int>* v = graph.findVertex(webId);
    if (v == nullptr) return -1;
    return (int)v->getAdj().size();
}

int InterferenceGraph::getMaxDegree() const {
    int maxDeg = 0;
    for (Vertex<int>* v : graph.getVertexSet()) {
        int deg = (int)v->getAdj().size();
        if (deg > maxDeg) maxDeg = deg;
    }
    return maxDeg;
}

bool InterferenceGraph::interferes(int webId1, int webId2) const {
    Vertex<int>* v = graph.findVertex(webId1);
    if (v == nullptr) return false;
    for (const Edge<int>& e : v->getAdj())
        if (e.getDest()->getInfo() == webId2)
            return true;
    return false;
}

std::vector<int> InterferenceGraph::getNeighbors(int webId) const {
    std::vector<int> neighbors;
    Vertex<int>* v = graph.findVertex(webId);
    if (v == nullptr) return neighbors;
    for (const Edge<int>& e : v->getAdj())
        neighbors.push_back(e.getDest()->getInfo());
    return neighbors;
}

int InterferenceGraph::getNumWebs() const {
    return graph.getNumVertex();
}

const Web& InterferenceGraph::getWeb(int webId) const {
    if (webId < 0 || webId >= (int)webs.size())
        throw std::out_of_range("InterferenceGraph::getWeb — invalid web ID: "
                                + std::to_string(webId));
    return webs[webId];
}

const std::vector<Web>& InterferenceGraph::getWebs() const {
    return webs;
}

const Graph<int>& InterferenceGraph::getGraph() const {
    return graph;
}

// ─────────────────────────────────────────────────────────────
// Output
// ─────────────────────────────────────────────────────────────

void InterferenceGraph::print() const {
    std::cout << "# Interference Graph\n";
    std::cout << "# " << graph.getNumVertex() << " web(s)\n\n";

    for (const Web& w : webs) {
        Vertex<int>* v = graph.findVertex(w.id);
        if (v == nullptr) continue;

        std::cout << "web" << w.id
                  << " (" << w.variable << ")"
                  << "  points: " << w.toString()
                  << "  degree: " << v->getAdj().size();

        if (!v->getAdj().empty()) {
            std::cout << "  interferes with: [";
            bool first = true;
            for (const Edge<int>& e : v->getAdj()) {
                if (!first) std::cout << ", ";
                first = false;
                int neighborId = e.getDest()->getInfo();
                std::cout << "web" << neighborId
                          << "(" << webs[neighborId].variable << ")";
            }
            std::cout << "]";
        }
        std::cout << "\n";
    }
}

void InterferenceGraph::emitDOT(const std::string& filename) const {
    std::ofstream dot(filename + ".gv");
    if (!dot.is_open()) {
        std::cerr << "[InterferenceGraph] Error: cannot open '" << filename << ".gv'\n";
        return;
    }

    dot << "graph interference {\n";
    dot << "  labelloc  = top;\n";
    dot << "  fontname  = calibri;\n";
    dot << "  fontsize  = 16;\n\n";

    // Declare nodes with descriptive labels
    for (const Web& w : webs)
        dot << "  " << w.id
            << " [label=\"web" << w.id << "\\n(" << w.variable << ")\"];\n";

    dot << "\n";

    // Emit edges — only once per pair (upper triangle)
    for (int i = 0; i < (int)webs.size(); i++)
        for (int j = i + 1; j < (int)webs.size(); j++)
            if (interferes(webs[i].id, webs[j].id))
                dot << "  " << webs[i].id << " -- " << webs[j].id << ";\n";

    dot << "}\n";
    dot.close();
}
