# DA_Project2

## Task Implementation Map

### T1.1 — Command-Line Menu & Batch Interface

- **`main.cpp`** — Entry point; detects the `-b` flag to route between batch and interactive mode. Interactive menu (options 1-6) is implemented directly in `runMenu()`. Batch mode accepts: `myProg -b ranges.txt registers.txt allocation.txt`
- **`RegisterAllocator.cpp`** — `writeOutput()` handles console warnings when allocation is infeasible (per spec §3.5)
- **`Parser.cpp`** — All parsing errors are directed to `std::cerr` for console output as required

---

### T1.2 — Read and Parse Input Data

- **`Parser.h` / `Parser.cpp`** — `parseLiveRanges()` reads the variable ranges file; `parseConfig()` reads the algorithm configuration. Helpers `parseLiveRangeLine()`, `trim()`, and `split()` handle tokenization
- **`DataTypes.h`** — Defines all core data structures: `LiveRange`, `Web`, and `AlgorithmConfig`
- **`InterferenceGraph.h` / `InterferenceGraph.cpp`** — `build()` constructs the interference graph from webs. Uses `Graph<int>` as the underlying representation (vertex info = web ID)
- **`main.cpp`** — `runMenu()` provides options 1 (load ranges), 2 (load config), 3 (display webs), 4 (display config). Option 3 calls `parser.printWebs()`, option 4 calls `parser.printConfig()`

---

### T2.1 — Basic Register Allocation (Greedy Coloring)

- **`RegisterAllocator.cpp`** — `allocateBasic()` dispatches to the core `greedyColor()` algorithm
- **`Helpers.cpp`** — `greedyColor()` implements the Figure 9 algorithm from the spec:
    - **Phase 1 (Simplification):** Repeatedly removes nodes with degree < N, pushes to stack. When none remain, spills selected nodes if budget permits
    - **Phase 2 (Coloring):** Pops nodes from stack, assigns lowest-numbered color not used by already-colored neighbors
- **`InterferenceGraph.cpp`** — `getDegree()`, `getNeighbors()`, and `interferes()` support the coloring algorithm
- **`AllocationResult.cpp`** — Stores coloring results with `webToRegister` map and `feasible` flag

---

### T2.2 — Register Allocation with Web Spilling

- **`RegisterAllocator.cpp`** — `allocateSpilling()` implements the spilling strategy:
    - Attempts basic allocation first (0 spills)
    - Iteratively increases spill budget from 1 to `config.algorithmParam`
    - Returns the first successful allocation
- **`Helpers.cpp`** — `selectSpillCandidate()` chooses the web with the highest degree-to-size ratio (degree / programPoints). This heuristic prioritizes webs that maximally reduce graph connectivity when removed
- **`greedyColor()`** — Accepts `maxSpills` parameter; spills selected candidates during simplification phase

---

### T2.3 — Register Allocation with Web Splitting

- **`RegisterAllocator.cpp`** — `allocateSplitting()` implements the splitting strategy:
    - Attempts basic allocation first (0 splits)
    - Iteratively splits webs (up to `algorithmParam` times) until coloring succeeds
    - After each split, rebuilds the interference graph and retries
- **`Helpers.cpp`** — `selectSplitCandidate()` evaluates each splittable web (≥2 program points) by simulating splits at each possible cut point and measuring interference reduction. Returns the web with the greatest reduction
- **`splitWeb()`** — Splits a web at its midpoint (or optimal cut point), creating two derived webs. Updates the interference graph via `InterferenceGraph::build()`

---

### T2.4 — Free Strategy (Smallest-Last + Selective Spill)

- **`Algorithms.cpp`** — `allocateFree()` implements a custom strategy combining:
    - **Smallest-last ordering:** Processes webs by earliest definition point first (defPoint order)
    - **Greedy assignment with eviction:** When no register is free for a web, spills the interfering neighbor with the fewest program points (cheapest to reload), then retries assignment
    - **No explicit spill budget:** Eviction decisions are made dynamically based on program point count
- **Rationale:** This approach minimizes spills by prioritizing long-lived webs (many program points) for registers, while short-lived webs (few program points) are cheaper to spill/reload. The defPoint ordering approximates program execution order, improving spatial locality

---

## How to run

If you are on Windows, you have to do this in the wsl terminal.

1. Create a folder `build` with `mkdir build`
2. Go to folder "build" with `cd build`
3. Run `cmake ..` followed by `cmake --build .`
4. Come to the project folder with `cd ..`
5. Run `./build/DA_Project2` for the interactive menu
6. Run `./build/DA_Project2 -b input/ranges.txt input/registers.txt output/allocation.txt` for the batch mode

When loading data from a file, in option 1 and 2 of the interactive menu, you have to use `input/filename.txt`