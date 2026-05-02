#include "Parser.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

// ─────────────────────────────────────────────────────────────
// LiveRange
// ─────────────────────────────────────────────────────────────

LiveRange::LiveRange() : defPoint(-1), lastUsePoint(-1) {}

bool LiveRange::overlapsWith(const LiveRange& other) const {
    std::set<int> otherPoints(other.programPoints.begin(), other.programPoints.end());

    for (int pt : programPoints) {
        if (!otherPoints.count(pt)) continue;

        if (pt == defPoint && pt == other.lastUsePoint) continue;
        if (pt == other.defPoint && pt == lastUsePoint) continue;

        return true;
    }
    return false;
}

void LiveRange::merge(const LiveRange& other) {
    std::set<int> existing(programPoints.begin(), programPoints.end());
    for (int pt : other.programPoints) {
        if (!existing.count(pt)) {
            programPoints.push_back(pt);
            existing.insert(pt);
        }
    }

    if (other.defPoint != -1) {
        if (defPoint == -1) defPoint = other.defPoint;
        else defPoint = std::min(defPoint, other.defPoint);
    }
    if (other.lastUsePoint != -1) {
        if (lastUsePoint == -1) lastUsePoint = other.lastUsePoint;
        else lastUsePoint = std::max(lastUsePoint, other.lastUsePoint);
    }
}

// ─────────────────────────────────────────────────────────────
// Web
// ─────────────────────────────────────────────────────────────

Web::Web() : id(-1), defPoint(-1), lastUsePoint(-1), reg(-1) {}

Web::Web(int id, const LiveRange& lr)
    : id(id), variable(lr.variable),
      programPoints(lr.programPoints.begin(), lr.programPoints.end()),
      defPoint(lr.defPoint),
      lastUsePoint(lr.lastUsePoint),
      reg(-1) {
    originalRanges.push_back(lr);
}

bool Web::interferesWith(const Web& other) const {
    for (int pt : programPoints) {
        if (!other.programPoints.count(pt)) continue;

        if (pt == defPoint       && pt == other.lastUsePoint) continue;
        if (pt == other.defPoint && pt == lastUsePoint)       continue;

        return true;
    }
    return false;
}

void Web::merge(const LiveRange& lr) {
    for (int pt : lr.programPoints)
        programPoints.insert(pt);
    originalRanges.push_back(lr);

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
    for (int pt : programPoints) {
        if (!first) oss << ",";
        first = false;
        oss << pt;
        if (pt == defPoint)     oss << "+";
        if (pt == lastUsePoint) oss << "-";
    }
    return oss.str();
}

// ─────────────────────────────────────────────────────────────
// AlgorithmConfig
// ─────────────────────────────────────────────────────────────

AlgorithmConfig::AlgorithmConfig()
    : numRegisters(0), algorithm("basic"), algorithmParam(-1) {}

// ─────────────────────────────────────────────────────────────
// Parser – private helpers
// ─────────────────────────────────────────────────────────────

std::string Parser::trim(const std::string& s) {
    const std::string ws = " \t\r\n";
    size_t start = s.find_first_not_of(ws);
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

std::vector<std::string> Parser::split(const std::string& s, char del) {
    std::vector<std::string> tokens;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, del))
        tokens.push_back(trim(token));
    return tokens;
}

bool Parser::parseLiveRangeLine(const std::string& line,
                                std::string& varName,
                                LiveRange& range) {
    size_t colonPos = line.find(':');
    if (colonPos == std::string::npos) {
        std::cerr << "[Parser] Error: missing ':' in line: " << line << "\n";
        return false;
    }

    varName = trim(line.substr(0, colonPos));
    if (varName.empty()) {
        std::cerr << "[Parser] Error: empty variable name in line: " << line << "\n";
        return false;
    }

    range.variable     = varName;
    range.defPoint     = -1;
    range.lastUsePoint = -1;
    range.programPoints.clear();

    std::string pointsPart = trim(line.substr(colonPos + 1));
    if (pointsPart.empty()) {
        std::cerr << "[Parser] Error: no program points for variable '"
                  << varName << "'\n";
        return false;
    }

    for (const std::string& tok : split(pointsPart, ',')) {
        if (tok.empty()) continue;

        bool hasDef     = (tok.back() == '+');
        bool hasLastUse = (tok.back() == '-');
        std::string numStr = (hasDef || hasLastUse)
                             ? tok.substr(0, tok.size() - 1) : tok;
        numStr = trim(numStr);

        if (numStr.empty()) {
            std::cerr << "[Parser] Warning: empty token in line: " << line << "\n";
            continue;
        }

        int lineNum;
        try {
            lineNum = std::stoi(numStr);
        } catch (...) {
            std::cerr << "[Parser] Error: invalid program point '"
                      << numStr << "' in line: " << line << "\n";
            return false;
        }

        if (lineNum <= 0) {
            std::cerr << "[Parser] Error: program point must be positive, got "
                      << lineNum << " in line: " << line << "\n";
            return false;
        }

        range.programPoints.push_back(lineNum);

        if (hasDef) {
            if (range.defPoint != -1)
                std::cerr << "[Parser] Warning: multiple '+' markers for '"
                          << varName << "' — keeping first.\n";
            else
                range.defPoint = lineNum;
        }
        if (hasLastUse)
            range.lastUsePoint = lineNum;
    }

    if (range.programPoints.empty()) {
        std::cerr << "[Parser] Error: no valid program points for '"
                  << varName << "'\n";
        return false;
    }

    return true;
}

void Parser::mergeRangesIntoWebs(const std::string& variable,
                                 std::vector<LiveRange>& ranges) {
    (void)variable;
    std::vector<Web> varWebs;

    for (LiveRange& lr : ranges) {
        int mergeTarget = -1;

        std::set<int> lrPoints(lr.programPoints.begin(), lr.programPoints.end());

        for (int i = 0; i < (int)varWebs.size(); i++) {
            Web& w = varWebs[i];
            bool touches = false;

            for (int pt : lrPoints) {
                if (!w.programPoints.count(pt)) continue;

                bool lrStartsHere = (pt == lr.defPoint);
                bool wEndsHere    = (pt == w.lastUsePoint);
                bool wStartsHere  = (pt == w.defPoint);
                bool lrEndsHere   = (pt == lr.lastUsePoint);

                if ((lrStartsHere && wEndsHere) || (wStartsHere && lrEndsHere)) {
                    touches = true;
                    break;
                }

                touches = true;
                break;
            }

            if (touches) {
                mergeTarget = i;
                break;
            }
        }

        if (mergeTarget == -1) {
            int newId = (int)webs.size() + (int)varWebs.size();
            varWebs.emplace_back(newId, lr);
        } else {
            varWebs[mergeTarget].merge(lr);

            bool changed = true;
            while (changed) {
                changed = false;
                for (int i = 0; i < (int)varWebs.size(); i++) {
                    if (i == mergeTarget) continue;

                    bool touches = false;
                    for (int pt : varWebs[i].programPoints) {
                        if (varWebs[mergeTarget].programPoints.count(pt)) {
                            touches = true;
                            break;
                        }
                    }

                    if (touches) {
                        Web& absorbed = varWebs[i];
                        for (int pt : absorbed.programPoints)
                            varWebs[mergeTarget].programPoints.insert(pt);

                        for (const LiveRange& lr : absorbed.originalRanges)
                            varWebs[mergeTarget].originalRanges.push_back(lr);

                        if (absorbed.defPoint != -1) {
                            if (varWebs[mergeTarget].defPoint == -1)
                                varWebs[mergeTarget].defPoint = absorbed.defPoint;
                            else
                                varWebs[mergeTarget].defPoint =
                                    std::min(varWebs[mergeTarget].defPoint,
                                             absorbed.defPoint);
                        }
                        if (absorbed.lastUsePoint != -1) {
                            if (varWebs[mergeTarget].lastUsePoint == -1)
                                varWebs[mergeTarget].lastUsePoint = absorbed.lastUsePoint;
                            else
                                varWebs[mergeTarget].lastUsePoint =
                                    std::max(varWebs[mergeTarget].lastUsePoint,
                                             absorbed.lastUsePoint);
                        }
                        varWebs.erase(varWebs.begin() + i);
                        if (i < mergeTarget) mergeTarget--;
                        changed = true;
                        break;
                    }
                }
            }
        }
    }

    for (Web& w : varWebs) {
        w.id = (int)webs.size();
        webs.push_back(w);
    }
}

// ─────────────────────────────────────────────────────────────
// Parser – public interface
// ─────────────────────────────────────────────────────────────

bool Parser::parseLiveRanges(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[Parser] Error: cannot open live ranges file '"
                  << filename << "'\n";
        return false;
    }

    std::map<std::string, std::vector<LiveRange>> rawRanges;
    std::vector<std::string> varOrder;

    std::string line;
    int lineNo = 0;
    while (std::getline(file, line)) {
        lineNo++;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        std::string varName;
        LiveRange lr;
        if (!parseLiveRangeLine(trimmed, varName, lr)) {
            std::cerr << "[Parser] Error at line " << lineNo << "\n";
            return false;
        }

        if (!rawRanges.count(varName))
            varOrder.push_back(varName);
        rawRanges[varName].push_back(lr);
    }
    file.close();

    if (rawRanges.empty()) {
        std::cerr << "[Parser] Warning: no live ranges found in '"
                  << filename << "'\n";
        return true;
    }

    webs.clear();
    for (const std::string& var : varOrder)
        mergeRangesIntoWebs(var, rawRanges[var]);

    return true;
}

bool Parser::parseConfig(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[Parser] Error: cannot open config file '"
                  << filename << "'\n";
        return false;
    }

    config = AlgorithmConfig();
    bool foundRegisters = false;
    bool foundAlgorithm = false;

    std::string line;
    int lineNo = 0;
    while (std::getline(file, line)) {
        lineNo++;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        size_t colonPos = trimmed.find(':');
        if (colonPos == std::string::npos) {
            std::cerr << "[Parser] Warning: ignoring malformed config line "
                      << lineNo << ": " << trimmed << "\n";
            continue;
        }

        std::string key   = trim(trimmed.substr(0, colonPos));
        std::string value = trim(trimmed.substr(colonPos + 1));

        if (key == "registers") {
            try {
                config.numRegisters = std::stoi(value);
                if (config.numRegisters < 0) {
                    std::cerr << "[Parser] Error: registers must be >= 0\n";
                    return false;
                }
                foundRegisters = true;
            } catch (...) {
                std::cerr << "[Parser] Error: invalid register count '"
                          << value << "'\n";
                return false;
            }

        } else if (key == "algorithm") {
            std::vector<std::string> parts = split(value, ',');
            if (parts.empty()) {
                std::cerr << "[Parser] Error: empty algorithm specification\n";
                return false;
            }

            config.algorithm = trim(parts[0]);

            if (config.algorithm != "basic"     &&
                config.algorithm != "spilling"  &&
                config.algorithm != "splitting" &&
                config.algorithm != "free") {
                std::cerr << "[Parser] Error: unknown algorithm '"
                          << config.algorithm << "'\n";
                return false;
            }

            bool needsParam = (config.algorithm == "spilling" ||
                               config.algorithm == "splitting");

            if (parts.size() >= 2) {
                if (!needsParam) {
                    std::cerr << "[Parser] Warning: numeric parameter ignored for '"
                              << config.algorithm << "'\n";
                } else {
                    try {
                        config.algorithmParam = std::stoi(parts[1]);
                        if (config.algorithmParam < 0) {
                            std::cerr << "[Parser] Error: algorithm parameter must be >= 0\n";
                            return false;
                        }
                    } catch (...) {
                        std::cerr << "[Parser] Error: invalid algorithm parameter '"
                                  << parts[1] << "'\n";
                        return false;
                    }
                }
            } else if (needsParam) {
                std::cerr << "[Parser] Error: '" << config.algorithm
                          << "' requires a numeric parameter (e.g., 'spilling, 2')\n";
                return false;
            }

            foundAlgorithm = true;

        } else {
            std::cerr << "[Parser] Warning: unknown config key '" << key
                      << "' at line " << lineNo << " — ignored\n";
        }
    }
    file.close();

    if (!foundRegisters) {
        std::cerr << "[Parser] Error: 'registers' field missing from config file\n";
        return false;
    }
    if (!foundAlgorithm) {
        std::cerr << "[Parser] Warning: 'algorithm' field missing — defaulting to 'basic'\n";
    }

    return true;
}

const std::vector<Web>& Parser::getWebs()    const { return webs; }
const AlgorithmConfig&  Parser::getConfig()  const { return config; }

void Parser::printWebs() const {
    std::cout << "webs: " << webs.size() << "\n";
    for (const Web& w : webs)
        std::cout << "web" << w.id << " (" << w.variable << "): "
                  << w.toString() << "\n";
}

void Parser::printConfig() const {
    std::cout << "registers: " << config.numRegisters << "\n";
    std::cout << "algorithm: " << config.algorithm;
    if (config.algorithmParam != -1)
        std::cout << ", " << config.algorithmParam;
    std::cout << "\n";
}