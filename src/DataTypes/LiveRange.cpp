#include "DataTypes.h"

#include <algorithm>

// ─────────────────────────────────────────────────────────────
// LiveRange
// ─────────────────────────────────────────────────────────────

LiveRange::LiveRange() : defPoint(-1), lastUsePoint(-1) {}

bool LiveRange::overlapsWith(const LiveRange& other) const {
    for (int pt : programPoints) {
        if (!other.programPoints.count(pt)) continue;

        // Exception: this starts here (def) and other ends here (last use) → no interference
        if (pt == defPoint && pt == other.lastUsePoint) continue;
        // Symmetric
        if (pt == other.defPoint && pt == lastUsePoint) continue;

        return true;
    }
    return false;
}

void LiveRange::merge(const LiveRange& other) {
    for (int pt : other.programPoints)
        programPoints.insert(pt);

    if (other.defPoint != -1) {
        if (defPoint == -1) defPoint = other.defPoint;
        else defPoint = std::min(defPoint, other.defPoint);
    }
    if (other.lastUsePoint != -1) {
        if (lastUsePoint == -1) lastUsePoint = other.lastUsePoint;
        else lastUsePoint = std::max(lastUsePoint, other.lastUsePoint);
    }
}
