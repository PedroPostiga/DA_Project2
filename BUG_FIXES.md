# Bug Analysis and Fixes for Split Function Crash

## Problem
The program crashes when the necessary registers (`numRegisters`) is lower than ideal in the splitting algorithm.

## Root Causes Found and Fixed

### Bug 1: Improper Comparison in `selectSplitCandidate` Fallback
**Location**: `src/RegisterAllocator/Helpers.cpp` - `selectSplitCandidate` function fallback

**Issue**: When no good split is found in the main optimization loop, the fallback code tries to find ANY splittable web. However, it uses the condition:
```cpp
if (deg > bestOrigDeg || (deg == bestOrigDeg && w.id < bestId))
```

When `bestId` starts as `-1` (uninitialized), comparing `w.id < -1` doesn't make sense and can cause incorrect behavior or crashes when accessing the `disabled` vector with potentially invalid indices.

**Fix**: Added explicit check for uninitialized state:
```cpp
if (bestId == -1 || deg > bestOrigDeg || (deg == bestOrigDeg && w.id < bestId))
```

This ensures that the first valid candidate is selected before attempting tie-breaking comparisons.

### Bug 2: Similar Issue in `selectSpillCandidate`
**Location**: `src/RegisterAllocator/Helpers.cpp` - `selectSpillCandidate` function

**Issue**: Same root cause as Bug 1. When `bestId == -1` and comparing `w.id < bestId`, the logic fails.

**Fix**: Applied the same defensive check:
```cpp
if (bestId == -1 || score > bestScore || ...)
```

### Bug 3: No Guard for K <= 0 in `allocateSplitting`
**Location**: `src/RegisterAllocator/Algorithms.cpp` - `allocateSplitting` function

**Issue**: When `numRegisters` (K) is 0 or negative, the algorithm should handle this edge case gracefully by immediately spilling all webs, rather than attempting complex splitting logic that may crash.

**Fix**: Added early exit guard:
```cpp
if (K <= 0) {
    InterferenceGraph workingIg = ig;
    AllocationResult r = greedyColor(workingIg, K, -1);
    r.feasible = true;
    return r;
}
```

## Why These Bugs Cause Crashes

1. **Array Index Out of Bounds**: When `disabled[w.id]` is accessed with an uninitialized or invalid `bestId`, and the comparison logic fails to properly bound-check, it can lead to accessing memory outside the `disabled` vector.

2. **Memory Safety**: The condition `w.id < bestId` where `bestId == -1` bypasses normal tie-breaking and can cause the algorithm to select invalid candidates or skip valid ones.

3. **Edge Case Handling**: When `K` is 0, the algorithm should immediately spill everything rather than attempting to split webs, which could lead to inefficient state transitions or infinite loops in certain graph configurations.

## Testing

To test these fixes:

1. Use input with very low `numRegisters` (e.g., 0 or 1)
2. Use `algorithm: splitting, 2` to trigger the splitting code path
3. Create test cases with densely-connected interference graphs
4. Verify that the program handles these cases gracefully instead of crashing
