#include "Parser.h"
#include "DataTypes.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include "Free.h"

// Forward declarations
void writeOutput(const std::vector<Web>& webs, int numRegisters, const std::string& filename);

// ─────────────────────────────────────────────────────────────
// Output
// ─────────────────────────────────────────────────────────────

void writeOutput(const std::vector<Web>& webs, int numRegisters, const std::string& filename) {
    std::ostream* out = &std::cout;
    std::ofstream fileOut;

    if (!filename.empty()) {
        fileOut.open(filename);
        if (!fileOut.is_open()) {
            std::cerr << "[Output] Error: cannot open output file '" << filename << "'\n";
            return;
        }
        out = &fileOut;
    }

    // Count how many registers were actually used
    std::set<int> usedRegs;
    bool anySpilled = false;
    for (const Web& w : webs) {
        if (w.reg >= 0) usedRegs.insert(w.reg);
        if (w.reg == -2) anySpilled = true;
    }

    if (anySpilled)
        std::cerr << "[Warning] Register allocation was not fully possible — some webs spilled to memory.\n";

    // Print webs
    *out << "webs: " << webs.size() << "\n";
    for (const Web& w : webs)
        *out << "web" << w.id << ": " << w.toString() << "\n";

    *out << "\n";

    // Print register assignments
    *out << "registers: " << usedRegs.size() << "\n";
    for (int r : usedRegs) {
        for (const Web& w : webs) {
            if (w.reg == r)
                *out << "r" << r << ": web" << w.id << "\n";
        }
    }

    // Print spilled webs
    for (const Web& w : webs) {
        if (w.reg == -2)
            *out << "M: web" << w.id << "\n";
    }
}

// ─────────────────────────────────────────────────────────────
// Batch mode
// ─────────────────────────────────────────────────────────────

void runBatch(const std::string& rangesFile,
              const std::string& configFile,
              const std::string& outputFile) {
    Parser parser;

    if (!parser.parseLiveRanges(rangesFile)) {
        std::cerr << "[Batch] Error: failed to parse live ranges file.\n";
        return;
    }

    if (!parser.parseConfig(configFile)) {
        std::cerr << "[Batch] Error: failed to parse config file.\n";
        return;
    }

    std::vector<Web> webs = parser.getWebs();
    const std::vector<int>& timeline = parser.getTimeline();
    const AlgorithmConfig& config = parser.getConfig();

    if (config.algorithm == "free") {
        freeAllocate(webs, timeline, config.numRegisters);
    } else {
        std::cerr << "[Batch] Error: algorithm '" << config.algorithm
                  << "' not yet implemented.\n";
        return;
    }

    writeOutput(webs, config.numRegisters, outputFile);
}

// ─────────────────────────────────────────────────────────────
// Interactive menu
// ─────────────────────────────────────────────────────────────

void runMenu() {
    Parser parser;
    std::vector<Web> webs;
    std::vector<int> timeline;
    AlgorithmConfig config;
    bool rangesLoaded = false;
    bool configLoaded = false;

    while (true) {
        std::cout << "\n=============================\n";
        std::cout << "  Register Allocation Tool\n";
        std::cout << "=============================\n";
        std::cout << "1. Load live ranges file\n";
        std::cout << "2. Load config file\n";
        std::cout << "3. Display webs\n";
        std::cout << "4. Display config\n";
        std::cout << "5. Run allocation\n";
        std::cout << "6. Write output to file\n";
        std::cout << "0. Exit\n";
        std::cout << "Choice: ";

        int choice;
        std::cin >> choice;

        if (choice == 0) {
            std::cout << "Exiting.\n";
            break;

        } else if (choice == 1) {
            std::string filename;
            std::cout << "Enter live ranges file path: ";
            std::cin >> filename;
            if (parser.parseLiveRanges(filename)) {
                webs = parser.getWebs();
                timeline = parser.getTimeline();
                rangesLoaded = true;
                std::cout << "[OK] Loaded " << webs.size() << " webs.\n";
            }

        } else if (choice == 2) {
            std::string filename;
            std::cout << "Enter config file path: ";
            std::cin >> filename;
            if (parser.parseConfig(filename)) {
                config = parser.getConfig();
                configLoaded = true;
                std::cout << "[OK] Config loaded.\n";
                parser.printConfig();
            }

        } else if (choice == 3) {
            if (!rangesLoaded) {
                std::cout << "[Error] No live ranges loaded yet.\n";
            } else {
                parser.printWebs();
            }

        } else if (choice == 4) {
            if (!configLoaded) {
                std::cout << "[Error] No config loaded yet.\n";
            } else {
                parser.printConfig();
            }

        } else if (choice == 5) {
            if (!rangesLoaded || !configLoaded) {
                std::cout << "[Error] Please load both files first.\n";
                continue;
            }

            if (config.algorithm == "free") {
                freeAllocate(webs, timeline, config.numRegisters);
                std::cout << "[OK] Allocation complete.\n";
                writeOutput(webs, config.numRegisters, "");
            } else {
                std::cout << "[Error] Algorithm '" << config.algorithm
                          << "' not yet implemented.\n";
            }

        } else if (choice == 6) {
            if (!rangesLoaded || !configLoaded) {
                std::cout << "[Error] Please load both files and run allocation first.\n";
                continue;
            }
            std::string filename;
            std::cout << "Enter output file path: ";
            std::cin >> filename;
            writeOutput(webs, config.numRegisters, filename);
            std::cout << "[OK] Output written to " << filename << "\n";

        } else {
            std::cout << "[Error] Invalid choice.\n";
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc == 5 && std::string(argv[1]) == "-b") {
        runBatch(argv[2], argv[3], argv[4]);
    } else if (argc == 1) {
        runMenu();
    } else {
        std::cerr << "Usage:\n";
        std::cerr << "  Interactive: myProg\n";
        std::cerr << "  Batch:       myProg -b ranges.txt registers.txt allocation.txt\n";
        return 1;
    }
    return 0;
}