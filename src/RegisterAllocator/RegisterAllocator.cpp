#include "RegisterAllocator.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>

// ─────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────

RegisterAllocator::RegisterAllocator(const InterferenceGraph& ig,
                                     const AlgorithmConfig& config)
    : ig(ig), config(config) {}

// ─────────────────────────────────────────────────────────────
// Public entry point — dispatches to the chosen strategy
// ─────────────────────────────────────────────────────────────

AllocationResult RegisterAllocator::allocate() {
    if      (config.algorithm == "basic")     return allocateBasic();
    else if (config.algorithm == "spilling")  return allocateSpilling();
    else if (config.algorithm == "splitting") return allocateSplitting();
    else if (config.algorithm == "free")      return allocateFree();
    else {
        std::cerr << "[RegisterAllocator] Unknown algorithm '"
                  << config.algorithm << "' — falling back to basic.\n";
        return allocateBasic();
    }
}

// ─────────────────────────────────────────────────────────────
// Output
// ─────────────────────────────────────────────────────────────

bool RegisterAllocator::writeOutput(const AllocationResult& result,
                                    const std::string& filename) const {
    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "[RegisterAllocator] Error: cannot open output file '"
                  << filename << "'\n";
        return false;
    }

    // Use the webs stored in the result (may differ from ig after splitting)
    const std::vector<Web>& webs = result.webs.empty() ? ig.getWebs() : result.webs;

    // ── Webs section ─────────────────────────────────────────────────────
    out << "# Total number of webs followed by the listing of the program points of each one\n";
    out << "# program points in each web are sorted in ascending order\n";
    out << "webs: " << webs.size() << "\n";
    for (const Web& w : webs)
        out << "web" << w.id << ": " << w.toString() << "\n";

    out << "# Total number of registers used, followed by assignment to webs\n";

    if (!result.feasible) {
        std::cerr << "[RegisterAllocator] Warning: register allocation was not possible "
                     "with the provided number of registers (" << config.numRegisters << ").\n";
        out << "registers: 0\n";
        for (const Web& w : webs)
            out << "M: web" << w.id << "\n";
    } else {
        out << "registers: " << result.registersUsed << "\n";

        // Group webs by register for clean output
        std::map<int, std::vector<int>> regToWebs;
        for (const auto& [wid, reg] : result.webToRegister) {
            if (reg == SPILLED) regToWebs[-1].push_back(wid);
            else                regToWebs[reg].push_back(wid);
        }

        for (auto& [reg, wids] : regToWebs) {
            std::sort(wids.begin(), wids.end());
            for (int wid : wids) {
                if (reg == SPILLED) out << "M: web"  << wid << "\n";
                else                out << "r" << reg << ": web" << wid << "\n";
            }
        }
    }

    out.close();
    return true;
}

void RegisterAllocator::printResult(const AllocationResult& result) const {
    // Use the webs stored in the result (may differ from ig after splitting)
    const std::vector<Web>& webs = result.webs.empty() ? ig.getWebs() : result.webs;

    std::cout << "\n# Register Allocation Result\n";
    std::cout << "webs: " << webs.size() << "\n";
    for (const Web& w : webs)
        std::cout << "web" << w.id << " (" << w.variable << "): "
                  << w.toString() << "\n";

    if (!result.feasible) {
        std::cerr << "Warning: allocation not feasible with "
                  << config.numRegisters << " register(s).\n";
        std::cout << "registers: 0\n";
        for (const Web& w : webs)
            std::cout << "M: web" << w.id << "\n";
        return;
    }

    std::cout << "registers: " << result.registersUsed << "\n";

    std::map<int, std::vector<int>> regToWebs;
    for (const auto& [wid, reg] : result.webToRegister) {
        if (reg == SPILLED) regToWebs[-1].push_back(wid);
        else                regToWebs[reg].push_back(wid);
    }

    for (auto& [reg, wids] : regToWebs) {
        std::sort(wids.begin(), wids.end());
        for (int wid : wids) {
            if (reg == SPILLED)
                std::cout << "M: web" << wid
                          << " (" << webs[wid].variable << ")\n";
            else
                std::cout << "r" << reg << ": web" << wid
                          << " (" << webs[wid].variable << ")\n";
        }
    }
}