#include "DataTypes.h"

#include <algorithm>
#include <sstream>

// ─────────────────────────────────────────────────────────────
// Web
// ─────────────────────────────────────────────────────────────

Web::Web() : id(-1), defPoint(-1), lastUsePoint(-1), reg(-1) {}

Web::Web(int id, const LiveRange& lr)
    : id(id), variable(lr.variable),
      programPoints(lr.programPoints),
      defPoint(lr.defPoint),
      lastUsePoint(lr.lastUsePoint),
      reg(-1),
      originalRanges({lr}) {}

bool Web::interferesWith(const Web& other) const {
    for (int pt : programPoints) {
        if (!other.programPoints.count(pt)) continue;

        if (pt == defPoint      && pt == other.lastUsePoint) continue;
        if (pt == other.defPoint && pt == lastUsePoint)      continue;

        return true;
    }
    return false;
}

void Web::merge(const LiveRange& lr) {
    originalRanges.push_back(lr);
    for (int pt : lr.programPoints)
        programPoints.insert(pt);

    if (lr.defPoint != -1) {
        if (defPoint == -1) defPoint = lr.defPoint;
        else defPoint = std::min(defPoint, lr.defPoint);
    }
    if (lr.lastUsePoint != -1) {
        if (lastUsePoint == -1) lastUsePoint = lr.lastUsePoint;
        else lastUsePoint = std::max(lastUsePoint, lr.lastUsePoint);
    }
}

std::string Web::toString() const {
    std::ostringstream oss;
    bool first = true;
    for (int pt : programPoints) {   // std::set iterates in sorted order
        if (!first) oss << ",";
        first = false;
        oss << pt;

        // Check if any original live range had a marker at this point
        bool hasDef = false;
        bool hasLastUse = false;
        for (const auto& lr : originalRanges) {
            if (lr.defPoint == pt) hasDef = true;
            if (lr.lastUsePoint == pt) hasLastUse = true;
        }

        if (hasDef)      oss << "+";
        if (hasLastUse)  oss << "-";
    }
    return oss.str();
}
