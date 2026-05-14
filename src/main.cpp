#include "Parser.h"
#include "DataTypes.h"
#include "RegisterAllocator.h"
#include "InterferenceGraph.h"
#include <iostream>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────
// Batch mode
// ─────────────────────────────────────────────────────────────

void runBatch(const std::string& rangesFile,
              const std::string& configFile,
              const std::string& outputFile) {
    Parser parser;

    if (!parser.parseLiveRanges(rangesFile)) return;
    if (!parser.parseConfig(configFile)) return;

    InterferenceGraph ig;
    ig.build(parser.getWebs());

    RegisterAllocator allocator(ig, parser.getConfig());
    AllocationResult result = allocator.allocate();

    if (!allocator.writeOutput(result, outputFile)) {
        std::cerr << "[Batch] Error: failed to write output file.\n";
    }
}

// ─────────────────────────────────────────────────────────────
// Interactive menu
// ─────────────────────────────────────────────────────────────

void runMenu() {
    Parser parser;
    AlgorithmConfig config;
    bool rangesLoaded = false;
    bool configLoaded = false;
    bool allocated = false;
    AllocationResult lastResult;

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
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore(1000, '\n');
            continue;
        }

        if (choice == 0) {
            break;

        } else if (choice == 1) {
            std::string filename;
            std::cout << "Enter live ranges file path: ";
            std::cin >> filename;
            if (parser.parseLiveRanges(filename)) {
                rangesLoaded = true;
                allocated = false;
                std::cout << "[OK] Loaded " << parser.getWebs().size() << " webs.\n";
            }

        } else if (choice == 2) {
            std::string filename;
            std::cout << "Enter config file path: ";
            std::cin >> filename;
            if (parser.parseConfig(filename)) {
                config = parser.getConfig();
                configLoaded = true;
                allocated = false;
                std::cout << "[OK] Config loaded.\n";
            }

        } else if (choice == 3) {
            if (!rangesLoaded) std::cout << "[Error] No live ranges loaded yet.\n";
            else parser.printWebs();

        } else if (choice == 4) {
            if (!configLoaded) std::cout << "[Error] No config loaded yet.\n";
            else parser.printConfig();

        } else if (choice == 5) {
            if (!rangesLoaded || !configLoaded) {
                std::cout << "[Error] Please load both files first.\n";
                continue;
            }

            InterferenceGraph ig;
            ig.build(parser.getWebs());
            RegisterAllocator allocator(ig, config);
            lastResult = allocator.allocate();
            allocated = true;
            
            std::cout << "[OK] Allocation complete.\n";
            allocator.printResult(lastResult);

        } else if (choice == 6) {
            if (!rangesLoaded || !configLoaded || !allocated) {
                std::cout << "[Error] Please load files and run allocation first.\n";
                continue;
            }
            std::string filename;
            std::cout << "Enter output file path: ";
            std::cin >> filename;
            
            InterferenceGraph ig;
            ig.build(parser.getWebs());
            RegisterAllocator allocator(ig, config);
            if (allocator.writeOutput(lastResult, filename)) {
                std::cout << "[OK] Output written to " << filename << "\n";
            }

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
