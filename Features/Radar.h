#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Radar {
    struct Vector3 {
        float x = 0;
        float y = 0;
        float z = 0;
    };

    struct Quaternion {
        float x = 0;
        float y = 0;
        float z = 0;
        float w = 1;
    };

    struct Pose {
        Vector3 position;
        float heading = 0;
        bool positionValid = false;
        bool headingValid = false;
    };

    struct Player {
        int actorId = 0;
        uintptr_t instance = 0;
        std::string name;
        Pose pose;
        float sanity = 0;
        bool sanityValid = false;
        bool lifeValid = false;
        bool dead = false;
        bool dying = false;
        bool local = false;
        bool pending = false;
    };

    struct Snapshot {
        uint64_t generation = 0;
        uint64_t sampledAt = 0;
        Pose center;
        Pose ghost;
        Pose bone;
        std::vector<Player> players;
        struct CursedObject {
            std::string name;
            Pose pose;
        };
        std::vector<CursedObject> cursedObjects;
        bool ghostVisibilityAvailable = false;
        bool deadInteractionAvailable = false;
        bool ghostVisibilityActive = false;
        bool deadInteractionActive = false;
        std::string worldOptionsMessage;
        bool actionsAllowed = false;
        bool sanityAvailable = false;
        bool remoteSanityAllowed = false;
        std::string actionRestriction = "Waiting for game data.";
        std::string message;
    };

    enum class Action { Kill, Revive, SetSanity };

    struct Request {
        int actorId;
        uintptr_t instance;
        uint64_t generation;
        uint64_t requestedAt;
        Action action;
        int sanity = 100;
    };

    inline std::mutex mutex;
    inline Snapshot snapshot;
    inline std::vector<Request> requests;

    inline bool IsFresh(const Snapshot& value, uint64_t now) {
        return value.sampledAt != 0 && now >= value.sampledAt && now - value.sampledAt <= 1500;
    }

    inline bool MayAct(bool inRoom, bool offline, bool host, bool privateRoom, bool inMatch) {
        return inRoom && inMatch && (offline || (host && privateRoom));
    }

    inline bool Heading(const Quaternion& q, float& radians) {
        const float norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (!std::isfinite(norm) || norm < 0.000001f) return false;
        const float x = 2.0f * (q.x * q.z + q.w * q.y) / norm;
        const float z = 1.0f - 2.0f * (q.x * q.x + q.y * q.y) / norm;
        if (x * x + z * z < 0.000001f) return false;
        radians = std::atan2(x, z);
        return true;
    }

    inline bool Project(const Vector3& origin, const Vector3& target, float range, float& x, float& y,
        float viewHeading = 0) {
        if (!std::isfinite(range) || range <= 0 || !std::isfinite(viewHeading)) return false;
        x = (target.x - origin.x) / range;
        y = -(target.z - origin.z) / range;
        if (!std::isfinite(x) || !std::isfinite(y) || x * x + y * y > 1.0f) return false;
        const float rotatedX = x * std::cos(viewHeading) + y * std::sin(viewHeading);
        y = -x * std::sin(viewHeading) + y * std::cos(viewHeading);
        x = rotatedX;
        return true;
    }

    inline Snapshot Read() {
        std::lock_guard<std::mutex> lock(mutex);
        return snapshot;
    }

    inline bool Queue(int actorId, uint64_t generation, Action action, uint64_t now, int sanity = 100) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto reject = [](const char* reason) {
            snapshot.message = reason;
            return false;
        };
        if (!IsFresh(snapshot, now) || generation != snapshot.generation)
            return reject("Request rejected: radar data is stale.");
        if (!snapshot.actionsAllowed)
            return reject("Request rejected: private/offline host authority is required.");
        if (action != Action::Kill && action != Action::Revive && action != Action::SetSanity)
            return reject("Request rejected: unknown player action.");
        auto player = std::find_if(snapshot.players.begin(), snapshot.players.end(),
            [actorId](const Player& entry) { return entry.actorId == actorId; });
        if (player == snapshot.players.end() || !player->instance || !player->lifeValid)
            return reject("Request rejected: player is unavailable.");
        const bool alreadyQueued = std::any_of(requests.begin(), requests.end(), [&](const Request& request) {
            return request.actorId == actorId && request.generation == generation;
        });
        if (alreadyQueued || player->pending || player->dying ||
            (action == Action::Revive) != player->dead)
            return reject("Request rejected: player state changed or an action is pending.");
        if (action == Action::SetSanity && (!snapshot.sanityAvailable || !player->sanityValid ||
            (!player->local && !snapshot.remoteSanityAllowed) || sanity < 0 || sanity > 100))
            return reject("Request rejected: sanity value or native sanity authority is unavailable.");
        if (requests.size() >= 4)
            return reject("Request rejected: action queue is full.");
        requests.push_back({ actorId, player->instance, generation, now, action, sanity });
        player->pending = true;
        snapshot.message = "Action queued for the game thread.";
        return true;
    }
}
