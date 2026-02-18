#include "Engine.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_set>

namespace {
constexpr uint64_t kHashOffset = 1469598103934665603ull;
constexpr uint64_t kHashPrime = 1099511628211ull;

static void hashCombine(uint64_t& hash, uint64_t value) {
    hash ^= value;
    hash *= kHashPrime;
}

static uint64_t quantizedFloat(float value, float scale = 1000.0f) {
    const double v = static_cast<double>(value) * static_cast<double>(scale);
    const int64_t q = static_cast<int64_t>(std::llround(v));
    return static_cast<uint64_t>(q);
}

static uint64_t computeAIPathSourceHash(const std::vector<SceneObject>& objects,
                                        float cellSize,
                                        float obstaclePadding,
                                        bool allowDiagonal,
                                        int maxGridResolution) {
    uint64_t hash = kHashOffset;
    hashCombine(hash, quantizedFloat(cellSize));
    hashCombine(hash, quantizedFloat(obstaclePadding));
    hashCombine(hash, static_cast<uint64_t>(allowDiagonal ? 1 : 0));
    hashCombine(hash, static_cast<uint64_t>(std::max(1, maxGridResolution)));

    for (const auto& obj : objects) {
        if (!obj.enabled) continue;

        if (obj.hasGroundBakedType) {
            hashCombine(hash, 0xA11F00D1ull);
            hashCombine(hash, static_cast<uint64_t>(obj.id));
            hashCombine(hash, static_cast<uint64_t>(obj.groundBakedType.enabled ? 1 : 0));
            hashCombine(hash, static_cast<uint64_t>(obj.groundBakedType.includeInBake ? 1 : 0));
            hashCombine(hash, quantizedFloat(obj.groundBakedType.areaCost));
            hashCombine(hash, quantizedFloat(obj.position.x));
            hashCombine(hash, quantizedFloat(obj.position.y));
            hashCombine(hash, quantizedFloat(obj.position.z));
            hashCombine(hash, quantizedFloat(obj.rotation.y));
            hashCombine(hash, quantizedFloat(obj.scale.x));
            hashCombine(hash, quantizedFloat(obj.scale.y));
            hashCombine(hash, quantizedFloat(obj.scale.z));
        }

        if (obj.hasObsticleObject) {
            hashCombine(hash, 0x0B57AC13ull);
            hashCombine(hash, static_cast<uint64_t>(obj.id));
            hashCombine(hash, static_cast<uint64_t>(obj.obsticleObject.enabled ? 1 : 0));
            hashCombine(hash, static_cast<uint64_t>(obj.obsticleObject.carve ? 1 : 0));
            hashCombine(hash, quantizedFloat(obj.obsticleObject.padding));
            hashCombine(hash, quantizedFloat(obj.position.x));
            hashCombine(hash, quantizedFloat(obj.position.y));
            hashCombine(hash, quantizedFloat(obj.position.z));
            hashCombine(hash, quantizedFloat(obj.rotation.y));
            hashCombine(hash, quantizedFloat(obj.scale.x));
            hashCombine(hash, quantizedFloat(obj.scale.y));
            hashCombine(hash, quantizedFloat(obj.scale.z));
        }
    }

    return hash;
}

static bool getObjectBoundsXZ(const SceneObject& obj, float padding, glm::vec2& outMin, glm::vec2& outMax) {
    glm::vec2 halfExtents(
        std::max(0.05f, std::abs(obj.scale.x) * 0.5f),
        std::max(0.05f, std::abs(obj.scale.z) * 0.5f)
    );

    if (obj.hasCollider && obj.collider.enabled) {
        halfExtents.x = std::max(halfExtents.x, std::max(0.05f, std::abs(obj.collider.boxSize.x) * 0.5f));
        halfExtents.y = std::max(halfExtents.y, std::max(0.05f, std::abs(obj.collider.boxSize.z) * 0.5f));
    }

    const float pad = std::max(0.0f, padding);
    halfExtents += glm::vec2(pad);
    if (!std::isfinite(halfExtents.x) || !std::isfinite(halfExtents.y)) return false;

    const glm::vec2 center(obj.position.x, obj.position.z);
    outMin = center - halfExtents;
    outMax = center + halfExtents;
    return std::isfinite(outMin.x) && std::isfinite(outMin.y) &&
           std::isfinite(outMax.x) && std::isfinite(outMax.y);
}
} // namespace

#pragma region AI Pathfinding
bool Engine::bakeAIPathGrid(bool logResult) {
    aiPathGrid = AIPathGrid{};
    aiAgentRuntimeStates.clear();

    aiPathBakeSettings.cellSize = std::max(0.05f, aiPathBakeSettings.cellSize);
    aiPathBakeSettings.obstaclePadding = std::clamp(aiPathBakeSettings.obstaclePadding, 0.0f, 25.0f);
    aiPathBakeSettings.maxGridResolution = std::clamp(aiPathBakeSettings.maxGridResolution, 32, 4096);

    aiPathLastSourceHash = computeAIPathSourceHash(
        sceneObjects,
        aiPathBakeSettings.cellSize,
        aiPathBakeSettings.obstaclePadding,
        aiPathBakeSettings.allowDiagonal,
        aiPathBakeSettings.maxGridResolution
    );

    std::vector<glm::vec2> groundMins;
    std::vector<glm::vec2> groundMaxs;
    std::vector<glm::vec2> obstacleMins;
    std::vector<glm::vec2> obstacleMaxs;
    groundMins.reserve(sceneObjects.size());
    groundMaxs.reserve(sceneObjects.size());
    obstacleMins.reserve(sceneObjects.size());
    obstacleMaxs.reserve(sceneObjects.size());

    glm::vec2 boundsMin(FLT_MAX);
    glm::vec2 boundsMax(-FLT_MAX);

    for (const auto& obj : sceneObjects) {
        if (!obj.enabled) continue;

        if (obj.hasGroundBakedType && obj.groundBakedType.enabled && obj.groundBakedType.includeInBake) {
            glm::vec2 localMin(0.0f), localMax(0.0f);
            if (getObjectBoundsXZ(obj, 0.0f, localMin, localMax)) {
                groundMins.push_back(localMin);
                groundMaxs.push_back(localMax);
                aiPathGrid.sourceGroundIds.push_back(obj.id);
                boundsMin.x = std::min(boundsMin.x, localMin.x);
                boundsMin.y = std::min(boundsMin.y, localMin.y);
                boundsMax.x = std::max(boundsMax.x, localMax.x);
                boundsMax.y = std::max(boundsMax.y, localMax.y);
            }
        }

        if (obj.hasObsticleObject && obj.obsticleObject.enabled && obj.obsticleObject.carve) {
            const float pad = aiPathBakeSettings.obstaclePadding + std::max(0.0f, obj.obsticleObject.padding);
            glm::vec2 localMin(0.0f), localMax(0.0f);
            if (getObjectBoundsXZ(obj, pad, localMin, localMax)) {
                obstacleMins.push_back(localMin);
                obstacleMaxs.push_back(localMax);
                aiPathGrid.sourceObstacleIds.push_back(obj.id);
            }
        }
    }

    if (groundMins.empty()) {
        if (logResult) {
            addConsoleMessage("AI bake skipped: no enabled GroundBakedType surfaces found.", ConsoleMessageType::Warning);
        }
        return false;
    }

    glm::vec2 span = boundsMax - boundsMin;
    span.x = std::max(span.x, aiPathBakeSettings.cellSize);
    span.y = std::max(span.y, aiPathBakeSettings.cellSize);

    float bakeCellSize = aiPathBakeSettings.cellSize;
    int width = static_cast<int>(std::ceil(span.x / bakeCellSize));
    int height = static_cast<int>(std::ceil(span.y / bakeCellSize));
    const int maxRes = std::max(1, aiPathBakeSettings.maxGridResolution);

    if (width > maxRes || height > maxRes) {
        const float largestSpan = std::max(span.x, span.y);
        bakeCellSize = std::max(bakeCellSize, largestSpan / static_cast<float>(maxRes));
        width = static_cast<int>(std::ceil(span.x / bakeCellSize));
        height = static_cast<int>(std::ceil(span.y / bakeCellSize));
    }

    width = std::clamp(width, 1, maxRes);
    height = std::clamp(height, 1, maxRes);

    aiPathGrid.baked = true;
    aiPathGrid.origin = boundsMin;
    aiPathGrid.width = width;
    aiPathGrid.height = height;
    aiPathGrid.cellSize = bakeCellSize;
    aiPathGrid.walkable.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0u);

    auto fillCells = [&](const glm::vec2& rmin, const glm::vec2& rmax, bool walkable) {
        const int minX = std::clamp(static_cast<int>(std::floor((rmin.x - aiPathGrid.origin.x) / aiPathGrid.cellSize)), 0, aiPathGrid.width - 1);
        const int minY = std::clamp(static_cast<int>(std::floor((rmin.y - aiPathGrid.origin.y) / aiPathGrid.cellSize)), 0, aiPathGrid.height - 1);
        const int maxX = std::clamp(static_cast<int>(std::floor((rmax.x - aiPathGrid.origin.x) / aiPathGrid.cellSize)), 0, aiPathGrid.width - 1);
        const int maxY = std::clamp(static_cast<int>(std::floor((rmax.y - aiPathGrid.origin.y) / aiPathGrid.cellSize)), 0, aiPathGrid.height - 1);
        for (int y = minY; y <= maxY; ++y) {
            const int row = y * aiPathGrid.width;
            for (int x = minX; x <= maxX; ++x) {
                aiPathGrid.walkable[static_cast<size_t>(row + x)] = walkable ? 1u : 0u;
            }
        }
    };

    for (size_t i = 0; i < groundMins.size(); ++i) {
        fillCells(groundMins[i], groundMaxs[i], true);
    }
    for (size_t i = 0; i < obstacleMins.size(); ++i) {
        fillCells(obstacleMins[i], obstacleMaxs[i], false);
    }

    size_t walkableCount = 0;
    for (uint8_t cell : aiPathGrid.walkable) {
        if (cell != 0u) ++walkableCount;
    }
    if (walkableCount == 0) {
        aiPathGrid.baked = false;
        if (logResult) {
            addConsoleMessage("AI bake failed: generated nav map has no walkable cells.", ConsoleMessageType::Warning);
        }
        return false;
    }

    if (logResult) {
        addConsoleMessage(
            "AI bake complete: " +
            std::to_string(aiPathGrid.width) + "x" + std::to_string(aiPathGrid.height) +
            " cells, walkable=" + std::to_string(walkableCount) +
            ", ground=" + std::to_string(aiPathGrid.sourceGroundIds.size()) +
            ", obstacles=" + std::to_string(aiPathGrid.sourceObstacleIds.size()),
            ConsoleMessageType::Success
        );
    }

    return true;
}

bool Engine::findAIPath(const glm::vec3& start, const glm::vec3& goal, std::vector<glm::vec3>& outPath) const {
    outPath.clear();
    if (!aiPathGrid.baked || aiPathGrid.width <= 0 || aiPathGrid.height <= 0 || aiPathGrid.walkable.empty()) {
        return false;
    }

    const int width = aiPathGrid.width;
    const int height = aiPathGrid.height;
    const int cellCount = width * height;
    auto inBounds = [&](int x, int y) { return x >= 0 && y >= 0 && x < width && y < height; };
    auto toIndex = [&](int x, int y) { return y * width + x; };
    auto toCell = [&](const glm::vec3& p, int& outX, int& outY) {
        outX = static_cast<int>(std::floor((p.x - aiPathGrid.origin.x) / aiPathGrid.cellSize));
        outY = static_cast<int>(std::floor((p.z - aiPathGrid.origin.y) / aiPathGrid.cellSize));
        outX = std::clamp(outX, 0, width - 1);
        outY = std::clamp(outY, 0, height - 1);
    };
    auto cellCenter = [&](int idx) {
        const int cx = idx % width;
        const int cy = idx / width;
        return glm::vec3(
            aiPathGrid.origin.x + (static_cast<float>(cx) + 0.5f) * aiPathGrid.cellSize,
            start.y,
            aiPathGrid.origin.y + (static_cast<float>(cy) + 0.5f) * aiPathGrid.cellSize
        );
    };
    auto isWalkable = [&](int x, int y) {
        if (!inBounds(x, y)) return false;
        return aiPathGrid.walkable[static_cast<size_t>(toIndex(x, y))] != 0u;
    };
    auto nearestWalkable = [&](int startIdx) {
        if (startIdx < 0 || startIdx >= cellCount) return -1;
        if (aiPathGrid.walkable[static_cast<size_t>(startIdx)] != 0u) return startIdx;
        std::queue<int> open;
        std::vector<uint8_t> visited(static_cast<size_t>(cellCount), 0u);
        open.push(startIdx);
        visited[static_cast<size_t>(startIdx)] = 1u;
        constexpr std::array<std::array<int, 2>, 4> dirs = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        while (!open.empty()) {
            const int idx = open.front();
            open.pop();
            const int x = idx % width;
            const int y = idx / width;
            for (const auto& d : dirs) {
                const int nx = x + d[0];
                const int ny = y + d[1];
                if (!inBounds(nx, ny)) continue;
                const int nidx = toIndex(nx, ny);
                if (visited[static_cast<size_t>(nidx)] != 0u) continue;
                visited[static_cast<size_t>(nidx)] = 1u;
                if (aiPathGrid.walkable[static_cast<size_t>(nidx)] != 0u) {
                    return nidx;
                }
                open.push(nidx);
            }
        }
        return -1;
    };

    int sx = 0, sy = 0;
    int gx = 0, gy = 0;
    toCell(start, sx, sy);
    toCell(goal, gx, gy);

    int startIdx = nearestWalkable(toIndex(sx, sy));
    int goalIdx = nearestWalkable(toIndex(gx, gy));
    if (startIdx < 0 || goalIdx < 0) {
        return false;
    }

    if (startIdx == goalIdx) {
        outPath.push_back(start);
        outPath.push_back(goal);
        return true;
    }

    struct OpenNode {
        int idx = -1;
        float f = 0.0f;
    };
    struct OpenNodeCmp {
        bool operator()(const OpenNode& a, const OpenNode& b) const { return a.f > b.f; }
    };

    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeCmp> open;
    std::vector<float> gScore(static_cast<size_t>(cellCount), std::numeric_limits<float>::infinity());
    std::vector<int> cameFrom(static_cast<size_t>(cellCount), -1);
    std::vector<uint8_t> closed(static_cast<size_t>(cellCount), 0u);

    auto heuristic = [&](int idx) {
        const int x = idx % width;
        const int y = idx / width;
        const int tx = goalIdx % width;
        const int ty = goalIdx / width;
        const int dx = std::abs(tx - x);
        const int dy = std::abs(ty - y);
        if (aiPathBakeSettings.allowDiagonal) {
            const int dmin = std::min(dx, dy);
            const int dmax = std::max(dx, dy);
            return static_cast<float>((dmax - dmin) + dmin * std::sqrt(2.0f));
        }
        return static_cast<float>(dx + dy);
    };

    gScore[static_cast<size_t>(startIdx)] = 0.0f;
    open.push({startIdx, heuristic(startIdx)});

    constexpr std::array<std::array<int, 2>, 8> allDirs = {{
        { 1, 0}, {-1, 0}, {0, 1}, {0,-1},
        { 1, 1}, {-1, 1}, {1,-1}, {-1,-1}
    }};

    bool found = false;
    while (!open.empty()) {
        const OpenNode current = open.top();
        open.pop();
        if (closed[static_cast<size_t>(current.idx)] != 0u) continue;
        if (current.idx == goalIdx) {
            found = true;
            break;
        }
        closed[static_cast<size_t>(current.idx)] = 1u;

        const int cx = current.idx % width;
        const int cy = current.idx / width;
        for (const auto& d : allDirs) {
            const bool diagonal = (d[0] != 0 && d[1] != 0);
            if (diagonal && !aiPathBakeSettings.allowDiagonal) continue;

            const int nx = cx + d[0];
            const int ny = cy + d[1];
            if (!isWalkable(nx, ny)) continue;

            if (diagonal) {
                if (!isWalkable(cx + d[0], cy) || !isWalkable(cx, cy + d[1])) continue;
            }

            const int nidx = toIndex(nx, ny);
            if (closed[static_cast<size_t>(nidx)] != 0u) continue;

            const float stepCost = diagonal ? std::sqrt(2.0f) : 1.0f;
            const float tentative = gScore[static_cast<size_t>(current.idx)] + stepCost;
            if (tentative < gScore[static_cast<size_t>(nidx)]) {
                cameFrom[static_cast<size_t>(nidx)] = current.idx;
                gScore[static_cast<size_t>(nidx)] = tentative;
                open.push({nidx, tentative + heuristic(nidx)});
            }
        }
    }

    if (!found) {
        return false;
    }

    std::vector<int> rev;
    int cur = goalIdx;
    while (cur >= 0) {
        rev.push_back(cur);
        if (cur == startIdx) break;
        cur = cameFrom[static_cast<size_t>(cur)];
    }
    if (rev.empty() || rev.back() != startIdx) {
        return false;
    }

    outPath.reserve(rev.size() + 2);
    outPath.push_back(start);
    for (auto it = rev.rbegin(); it != rev.rend(); ++it) {
        outPath.push_back(cellCenter(*it));
    }
    if (glm::distance(outPath.back(), goal) > (aiPathGrid.cellSize * 0.35f)) {
        outPath.push_back(goal);
    } else {
        outPath.back().y = goal.y;
    }

    return true;
}

void Engine::updateAIAgents(float delta) {
    if (delta <= 0.0f) return;

    std::unordered_map<int, SceneObject*> objectById;
    objectById.reserve(sceneObjects.size());
    for (auto& obj : sceneObjects) {
        objectById[obj.id] = &obj;
    }

    std::unordered_set<int> activeAgents;
    activeAgents.reserve(sceneObjects.size());
    for (const auto& obj : sceneObjects) {
        if (obj.enabled && obj.hasAIAgent && obj.aiAgent.enabled) {
            activeAgents.insert(obj.id);
        }
    }
    if (activeAgents.empty()) {
        aiAgentRuntimeStates.clear();
        return;
    }

    const uint64_t currentHash = computeAIPathSourceHash(
        sceneObjects,
        aiPathBakeSettings.cellSize,
        aiPathBakeSettings.obstaclePadding,
        aiPathBakeSettings.allowDiagonal,
        aiPathBakeSettings.maxGridResolution
    );

    if (aiPathBakeSettings.autoRebake && (!aiPathGrid.baked || currentHash != aiPathLastSourceHash)) {
        bakeAIPathGrid(false);
    }
    if (!aiPathGrid.baked) return;

    const float pathSlack = std::max(0.05f, aiPathGrid.cellSize * 0.3f);

    for (auto& obj : sceneObjects) {
        if (!obj.enabled || !obj.hasAIAgent || !obj.aiAgent.enabled) continue;

        auto& agent = obj.aiAgent;
        auto& state = aiAgentRuntimeStates[obj.id];
        state.repathTimer -= delta;

        glm::vec3 goal = agent.destination;
        if (agent.useTargetObject && agent.targetId >= 0) {
            auto itTarget = objectById.find(agent.targetId);
            if (itTarget != objectById.end() && itTarget->second && itTarget->second->enabled) {
                goal = itTarget->second->position;
            }
        }

        const float goalShiftThreshold = std::max(0.1f, aiPathGrid.cellSize * 0.5f);
        bool needsRepath = state.path.empty() || state.nextIndex >= static_cast<int>(state.path.size());
        glm::vec3 goalDelta = goal - state.lastGoal;
        if (!state.hasLastGoal || glm::dot(goalDelta, goalDelta) > goalShiftThreshold * goalShiftThreshold) {
            needsRepath = true;
        }
        if (agent.autoRepath && state.repathTimer <= 0.0f) {
            needsRepath = true;
        }

        if (needsRepath) {
            std::vector<glm::vec3> newPath;
            if (findAIPath(obj.position, goal, newPath)) {
                state.path.swap(newPath);
                state.nextIndex = (state.path.size() > 1) ? 1 : 0;
            } else {
                state.path.clear();
                state.nextIndex = 0;
            }
            state.repathTimer = std::max(0.05f, agent.repathInterval);
            state.lastGoal = goal;
            state.hasLastGoal = true;
        }

        if (state.path.empty() || state.nextIndex >= static_cast<int>(state.path.size())) {
            continue;
        }

        auto planarDistance = [](const glm::vec3& a, const glm::vec3& b) {
            const glm::vec2 d(a.x - b.x, a.z - b.z);
            return glm::length(d);
        };

        const float stopDistance = std::max(0.0f, agent.stoppingDistance);
        while (state.nextIndex < static_cast<int>(state.path.size()) &&
               planarDistance(obj.position, state.path[static_cast<size_t>(state.nextIndex)]) <= (stopDistance + pathSlack)) {
            ++state.nextIndex;
        }
        if (state.nextIndex >= static_cast<int>(state.path.size())) {
            continue;
        }

        glm::vec3 targetPos = state.path[static_cast<size_t>(state.nextIndex)];
        glm::vec3 toTarget = targetPos - obj.position;
        toTarget.y = 0.0f;
        float distanceToTarget = glm::length(toTarget);
        glm::vec3 moveDir(0.0f);
        if (distanceToTarget > 1e-4f) {
            moveDir = toTarget / distanceToTarget;
        }

        if (agent.alignToPath && glm::length(moveDir) > 0.001f) {
            const float yaw = glm::degrees(std::atan2(moveDir.x, -moveDir.z));
            obj.rotation.y = yaw;
            if (obj.hasRigidbody && obj.rigidbody.enabled && !obj.rigidbody.isKinematic) {
                physics.setActorYaw(obj.id, yaw);
            }
        }

        const float speed = std::max(0.05f, agent.speed);
        glm::vec3 desiredPlanarVelocity = moveDir * speed;
        if (distanceToTarget <= stopDistance) {
            desiredPlanarVelocity = glm::vec3(0.0f);
        }

        bool movedByPhysics = false;
        if (obj.hasRigidbody && obj.rigidbody.enabled && !obj.rigidbody.isKinematic) {
            glm::vec3 currentVelocity(0.0f);
            physics.getLinearVelocity(obj.id, currentVelocity);
            currentVelocity.x = desiredPlanarVelocity.x;
            currentVelocity.z = desiredPlanarVelocity.z;
            movedByPhysics = physics.setLinearVelocity(obj.id, currentVelocity);
        }

        if (!movedByPhysics) {
            if (glm::length(desiredPlanarVelocity) > 0.0f) {
                const float step = speed * delta;
                const float moveAmount = std::min(step, std::max(0.0f, distanceToTarget - stopDistance));
                obj.position += moveDir * moveAmount;
            }
            syncLocalTransform(obj);
            if (obj.hasRigidbody && obj.rigidbody.enabled && obj.rigidbody.isKinematic) {
                physics.setActorPose(obj.id, obj.position, obj.rotation);
            }
        }
    }

    for (auto it = aiAgentRuntimeStates.begin(); it != aiAgentRuntimeStates.end(); ) {
        if (activeAgents.find(it->first) == activeAgents.end()) {
            it = aiAgentRuntimeStates.erase(it);
        } else {
            ++it;
        }
    }
}

void Engine::renderAIPathfindingWindow() {
    if (!showAIPathfindingWindow) return;

    if (!ImGui::Begin("AI Pathfinding", &showAIPathfindingWindow)) {
        ImGui::End();
        return;
    }

    std::vector<SceneObject*> grounds;
    std::vector<SceneObject*> obstacles;
    std::vector<SceneObject*> agents;
    grounds.reserve(sceneObjects.size());
    obstacles.reserve(sceneObjects.size());
    agents.reserve(sceneObjects.size());
    for (auto& obj : sceneObjects) {
        if (!obj.enabled) continue;
        if (obj.hasGroundBakedType && obj.groundBakedType.enabled && obj.groundBakedType.includeInBake) {
            grounds.push_back(&obj);
        }
        if (obj.hasObsticleObject && obj.obsticleObject.enabled && obj.obsticleObject.carve) {
            obstacles.push_back(&obj);
        }
        if (obj.hasAIAgent && obj.aiAgent.enabled) {
            agents.push_back(&obj);
        }
    }

    const uint64_t sourceHash = computeAIPathSourceHash(
        sceneObjects,
        aiPathBakeSettings.cellSize,
        aiPathBakeSettings.obstaclePadding,
        aiPathBakeSettings.allowDiagonal,
        aiPathBakeSettings.maxGridResolution
    );

    ImGui::TextDisabled("Sources");
    ImGui::Text("Ground: %zu  Obstacles: %zu  Agents: %zu",
                grounds.size(), obstacles.size(), agents.size());
    ImGui::Separator();

    bool settingsChanged = false;
    ImGui::TextDisabled("Bake Settings");
    if (ImGui::DragFloat("Cell Size", &aiPathBakeSettings.cellSize, 0.01f, 0.05f, 10.0f, "%.2f")) {
        aiPathBakeSettings.cellSize = std::clamp(aiPathBakeSettings.cellSize, 0.05f, 10.0f);
        settingsChanged = true;
    }
    if (ImGui::DragFloat("Obstacle Padding", &aiPathBakeSettings.obstaclePadding, 0.01f, 0.0f, 25.0f, "%.2f")) {
        aiPathBakeSettings.obstaclePadding = std::clamp(aiPathBakeSettings.obstaclePadding, 0.0f, 25.0f);
        settingsChanged = true;
    }
    if (ImGui::DragInt("Max Resolution", &aiPathBakeSettings.maxGridResolution, 1.0f, 32, 4096)) {
        aiPathBakeSettings.maxGridResolution = std::clamp(aiPathBakeSettings.maxGridResolution, 32, 4096);
        settingsChanged = true;
    }
    if (ImGui::Checkbox("Allow Diagonal", &aiPathBakeSettings.allowDiagonal)) {
        settingsChanged = true;
    }
    if (ImGui::Checkbox("Auto Rebake", &aiPathBakeSettings.autoRebake)) {
        settingsChanged = true;
    }
    if (settingsChanged) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }

    if (aiPathBakeSettings.autoRebake && sourceHash != aiPathLastSourceHash) {
        bakeAIPathGrid(false);
    }

    if (ImGui::Button("Bake Navigation")) {
        bakeAIPathGrid(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Bake")) {
        aiPathGrid = AIPathGrid{};
        aiAgentRuntimeStates.clear();
        aiPathLastSourceHash = sourceHash;
    }

    ImGui::SameLine();
    ImGui::Checkbox("Draw Grid", &aiPathDrawGrid);
    ImGui::SameLine();
    ImGui::Checkbox("Draw Path", &aiPathDrawPath);

    if (aiPathGrid.baked) {
        size_t walkableCount = 0;
        for (uint8_t cell : aiPathGrid.walkable) {
            if (cell != 0u) ++walkableCount;
        }
        ImGui::TextDisabled(
            "Baked: %dx%d (cell %.2f), walkable %zu/%zu",
            aiPathGrid.width,
            aiPathGrid.height,
            aiPathGrid.cellSize,
            walkableCount,
            aiPathGrid.walkable.size()
        );
    } else {
        ImGui::TextDisabled("No baked navigation grid.");
    }

    ImGui::Separator();
    ImGui::TextDisabled("Preview");

    if (!agents.empty()) {
        auto findAgentById = [&](int id) -> SceneObject* {
            for (SceneObject* a : agents) {
                if (a && a->id == id) return a;
            }
            return nullptr;
        };

        if (!findAgentById(aiPreviewAgentId)) {
            aiPreviewAgentId = agents.front()->id;
        }

        SceneObject* previewAgent = findAgentById(aiPreviewAgentId);
        const char* selectedLabel = previewAgent ? previewAgent->name.c_str() : "<none>";
        if (ImGui::BeginCombo("Agent", selectedLabel)) {
            for (SceneObject* candidate : agents) {
                if (!candidate) continue;
                const bool selected = (candidate->id == aiPreviewAgentId);
                if (ImGui::Selectable(candidate->name.c_str(), selected)) {
                    aiPreviewAgentId = candidate->id;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (previewAgent) {
            bool overrideTarget = aiPreviewTargetId >= 0;
            if (ImGui::Checkbox("Override Preview Target", &overrideTarget)) {
                if (!overrideTarget) {
                    aiPreviewTargetId = -1;
                } else if (previewAgent->aiAgent.targetId >= 0) {
                    aiPreviewTargetId = previewAgent->aiAgent.targetId;
                }
            }

            if (overrideTarget) {
                SceneObject* selectedTarget = findObjectById(aiPreviewTargetId);
                const char* label = selectedTarget ? selectedTarget->name.c_str() : "<missing>";
                if (ImGui::BeginCombo("Target Object", label)) {
                    for (auto& obj : sceneObjects) {
                        if (!obj.enabled || obj.id == previewAgent->id) continue;
                        const bool selected = (obj.id == aiPreviewTargetId);
                        if (ImGui::Selectable(obj.name.c_str(), selected)) {
                            aiPreviewTargetId = obj.id;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
        }
    } else {
        aiPreviewAgentId = -1;
        aiPreviewTargetId = -1;
        ImGui::TextDisabled("No enabled AI Agent components in scene.");
    }

    std::vector<glm::vec3> previewPath;
    glm::vec3 previewStart(0.0f);
    glm::vec3 previewGoal(0.0f);
    bool hasPreviewPath = false;
    if (aiPathGrid.baked && aiPreviewAgentId >= 0) {
        SceneObject* previewAgent = findObjectById(aiPreviewAgentId);
        if (previewAgent && previewAgent->enabled && previewAgent->hasAIAgent && previewAgent->aiAgent.enabled) {
            previewStart = previewAgent->position;
            previewGoal = previewAgent->aiAgent.destination;

            if (aiPreviewTargetId >= 0) {
                if (SceneObject* target = findObjectById(aiPreviewTargetId)) {
                    if (target->enabled) {
                        previewGoal = target->position;
                    }
                }
            } else if (previewAgent->aiAgent.useTargetObject && previewAgent->aiAgent.targetId >= 0) {
                if (SceneObject* target = findObjectById(previewAgent->aiAgent.targetId)) {
                    if (target->enabled) {
                        previewGoal = target->position;
                    }
                }
            }

            hasPreviewPath = findAIPath(previewStart, previewGoal, previewPath);
        }
    }

    const float minCanvasHeight = 180.0f;
    const float maxCanvasHeight = 360.0f;
    float canvasHeight = std::clamp(ImGui::GetContentRegionAvail().y, minCanvasHeight, maxCanvasHeight);
    ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, canvasHeight);
    if (canvasSize.x < 32.0f) canvasSize.x = 32.0f;

    ImGui::InvisibleButton("##AIPathPreview", canvasSize);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 canvasMax(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y);
    draw->AddRectFilled(canvasMin, canvasMax, IM_COL32(17, 21, 27, 255), 6.0f);
    draw->AddRect(canvasMin, canvasMax, IM_COL32(60, 72, 84, 255), 6.0f);

    if (aiPathGrid.baked && aiPathGrid.width > 0 && aiPathGrid.height > 0 && aiPathDrawGrid) {
        const int maxPreviewCells = 140;
        const int stepX = std::max(1, static_cast<int>(std::ceil(static_cast<float>(aiPathGrid.width) / static_cast<float>(maxPreviewCells))));
        const int stepY = std::max(1, static_cast<int>(std::ceil(static_cast<float>(aiPathGrid.height) / static_cast<float>(maxPreviewCells))));
        const int previewW = static_cast<int>(std::ceil(static_cast<float>(aiPathGrid.width) / static_cast<float>(stepX)));
        const int previewH = static_cast<int>(std::ceil(static_cast<float>(aiPathGrid.height) / static_cast<float>(stepY)));

        const float cellW = canvasSize.x / static_cast<float>(std::max(1, previewW));
        const float cellH = canvasSize.y / static_cast<float>(std::max(1, previewH));
        for (int py = 0; py < previewH; ++py) {
            for (int px = 0; px < previewW; ++px) {
                const int x0 = px * stepX;
                const int y0 = py * stepY;
                const int x1 = std::min(aiPathGrid.width, x0 + stepX);
                const int y1 = std::min(aiPathGrid.height, y0 + stepY);
                int walkableCount = 0;
                const int total = std::max(1, (x1 - x0) * (y1 - y0));
                for (int y = y0; y < y1; ++y) {
                    const int row = y * aiPathGrid.width;
                    for (int x = x0; x < x1; ++x) {
                        if (aiPathGrid.walkable[static_cast<size_t>(row + x)] != 0u) {
                            ++walkableCount;
                        }
                    }
                }

                ImU32 color = IM_COL32(86, 36, 36, 210);
                if (walkableCount == total) {
                    color = IM_COL32(50, 125, 84, 220);
                } else if (walkableCount > 0) {
                    color = IM_COL32(90, 112, 74, 220);
                }

                const ImVec2 p0(canvasMin.x + px * cellW, canvasMin.y + py * cellH);
                const ImVec2 p1(canvasMin.x + (px + 1) * cellW, canvasMin.y + (py + 1) * cellH);
                draw->AddRectFilled(p0, p1, color);
            }
        }
    }

    auto worldToCanvas = [&](const glm::vec3& p) {
        const float spanX = std::max(0.001f, aiPathGrid.cellSize * static_cast<float>(std::max(1, aiPathGrid.width)));
        const float spanY = std::max(0.001f, aiPathGrid.cellSize * static_cast<float>(std::max(1, aiPathGrid.height)));
        float u = (p.x - aiPathGrid.origin.x) / spanX;
        float v = (p.z - aiPathGrid.origin.y) / spanY;
        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);
        return ImVec2(
            canvasMin.x + u * canvasSize.x,
            canvasMax.y - v * canvasSize.y
        );
    };

    if (aiPathGrid.baked && aiPathDrawPath && hasPreviewPath && previewPath.size() >= 2) {
        std::vector<ImVec2> polyline;
        polyline.reserve(previewPath.size());
        for (const auto& p : previewPath) {
            polyline.push_back(worldToCanvas(p));
        }
        draw->AddPolyline(polyline.data(), static_cast<int>(polyline.size()), IM_COL32(235, 206, 92, 255), false, 2.5f);
        draw->AddCircleFilled(worldToCanvas(previewStart), 4.0f, IM_COL32(104, 194, 255, 255), 12);
        draw->AddCircleFilled(worldToCanvas(previewGoal), 4.5f, IM_COL32(255, 122, 122, 255), 12);
    }

    if (!aiPathGrid.baked) {
        draw->AddText(ImVec2(canvasMin.x + 10.0f, canvasMin.y + 10.0f), IM_COL32(180, 190, 200, 255), "Bake navigation to preview cells.");
    } else if (aiPathDrawPath && !hasPreviewPath) {
        draw->AddText(ImVec2(canvasMin.x + 10.0f, canvasMin.y + 10.0f), IM_COL32(196, 170, 120, 255), "No path found for current preview agent/goal.");
    }

    if (ImGui::CollapsingHeader("Baked Sources")) {
        ImGui::TextDisabled("Ground IDs");
        if (aiPathGrid.sourceGroundIds.empty()) {
            ImGui::TextUnformatted("None");
        } else {
            for (int id : aiPathGrid.sourceGroundIds) {
                SceneObject* obj = findObjectById(id);
                ImGui::BulletText("%d  %s", id, obj ? obj->name.c_str() : "<missing>");
            }
        }

        ImGui::TextDisabled("Obstacle IDs");
        if (aiPathGrid.sourceObstacleIds.empty()) {
            ImGui::TextUnformatted("None");
        } else {
            for (int id : aiPathGrid.sourceObstacleIds) {
                SceneObject* obj = findObjectById(id);
                ImGui::BulletText("%d  %s", id, obj ? obj->name.c_str() : "<missing>");
            }
        }
    }

    ImGui::End();
}
#pragma endregion
