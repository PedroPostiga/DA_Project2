#include "DataTypes.h"

#include <algorithm>
#include <sstream>

// ─────────────────────────────────────────────────────────────
// Web
// ─────────────────────────────────────────────────────────────

Web::Web() : id(-1), defPoint(-1), lastUsePoint(-1) {}

Web::Web(int id, const LiveRange& lr)
    : id(id), variable(lr.variable),
      programPoints(lr.programPoints),
      defPoint(lr.defPoint),
      lastUsePoint(lr.lastUsePoint) {}

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
        if (pt == defPoint)      oss << "+";
        if (pt == lastUsePoint)  oss << "-";
    }
    return oss.str();
}
