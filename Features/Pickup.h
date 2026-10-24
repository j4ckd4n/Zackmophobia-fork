#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Pickup {
    inline std::atomic<bool> desired{false};

    struct Snapshot {
        bool available = false;
        bool active = false;
        bool restorePending = false;
        bool networked = false;
        bool faulted = false;
        uint64_t generation = 0;
        std::string message;
    };

    // Initialize on an IL2CPP-attached setup thread before activating hooks.
    // Update/HandleInput/Restore: Unity game thread only. desired/Read: any thread.
    // allowed is caller-provided context eligibility; a live camera is required.
    bool Initialize(void* image, void* unityImage, void* physicsImage, void* inputImage);
    void Update(void* camera, void* localPlayer, bool allowed, bool inputCaptured, uint64_t generation);
    bool HandleInput(void* callbackContext);
    bool Restore();
    Snapshot Read();

    // Experimental hierarchy grab, not an inventory/equipment conversion.
    // Existing verified Photon position observers are used without inventing RPCs.
    // Ownership is requested best-effort while holding, without a second press.
    // Unknown/denied synchronization permits explicitly unverified local movement.
    // Actor/equipment/root colliders are not excluded; dropping never restores position.
    namespace Detail {
        // Corrupt metadata must not cause unchecked allocation/traversal.
        inline constexpr size_t MaxElements = 1024 * 1024;

        struct Candidate {
            void* transform = nullptr;
            void* body = nullptr;
            bool hasParent = false;
            int children = -1;
            bool validHierarchy = false;
            bool ownershipAllowed = false;
            bool compoundBody = true;
            bool networked = false;
            bool synchronizationSupported = false;
            bool networkingKnown = true;
            std::vector<void*> bodies;
            std::vector<void*> views;
            std::vector<void*> requestableViews;
        };

        inline bool Selectable(const Candidate& value) {
            return value.transform && value.children >= 0 && value.validHierarchy &&
                value.bodies.size() <= MaxElements && value.views.size() <= MaxElements;
        }

        inline bool SameScope(const Candidate& left, const Candidate& right) {
            return left.transform == right.transform && left.bodies == right.bodies;
        }

        struct Lease {
            Candidate object;
            bool active = false;
            bool restoreKinematic = false;
            std::vector<bool> originals;
            std::vector<bool> restoreBodies;
            std::vector<bool> reassertBodies;
            bool acquiredOwnership = false;
            bool baselinePending = false;
            size_t baselineCursor = 0;
            uint64_t generation = 0;

            template<class Backend>
            bool Begin(Backend& backend, const Candidate& candidate, uint64_t currentGeneration) {
                if (active || !Selectable(candidate)) return false;
                Candidate selected = candidate;
                if (selected.bodies.empty() && selected.body) selected.bodies.push_back(selected.body);
                originals.assign(selected.bodies.size(), false);
                restoreBodies.assign(selected.bodies.size(), false);
                reassertBodies.assign(selected.bodies.size(), false);
                acquiredOwnership = baselinePending = false;
                baselineCursor = 0;
                for (size_t i = 0; i < selected.bodies.size(); ++i) {
                    bool original = false;
                    if (!backend.GetKinematic(selected.bodies[i], original)) return false;
                    originals[i] = original;
                }
                if (!backend.Pin(selected)) return false;
                object = std::move(selected);
                generation = currentGeneration;
                active = true;
                // Record restoration BEFORE the native setter: it can fault after
                // partially applying the change, so failure must retain this lease.
                for (size_t i = 0; i < object.bodies.size(); ++i) {
                    restoreBodies[i] = true;
                    restoreKinematic = true;
                    if (!backend.SetKinematic(object.bodies[i], true)) return false;
                }
                return true;
            }

            template<class Backend>
            bool CaptureNativeBaseline(Backend& backend) {
                if (!baselinePending) return true;
                while (baselineCursor < object.bodies.size()) {
                    bool alive = false;
                    if (!backend.Alive(object.bodies[baselineCursor], alive)) return false;
                    if (!alive) {
                        restoreBodies[baselineCursor] = reassertBodies[baselineCursor] = false;
                        ++baselineCursor;
                        continue;
                    }
                    bool actual = true;
                    if (!backend.GetKinematic(object.bodies[baselineCursor], actual)) return false;
                    // Our last write was true. An observed false is external;
                    // unchanged true cannot distinguish a fixture from our grab.
                    if (!actual) originals[baselineCursor] = false;
                    reassertBodies[baselineCursor] = !actual;
                    ++baselineCursor;
                }
                baselinePending = false;
                return true;
            }

            template<class Backend>
            bool ObserveOwnership(Backend& backend, const Candidate& observed, bool keepHeld = true) {
                if (!active || !observed.networkingKnown || !observed.networked ||
                    !observed.ownershipAllowed || object.views != observed.views) return true;
                if (object.networkingKnown && object.networked && !object.ownershipAllowed)
                    acquiredOwnership = true;
                if (!acquiredOwnership) return true;
                if (!baselinePending) {
                    baselinePending = true;
                    baselineCursor = 0;
                }
                // Capture every body before reasserting any flag. Failed reads
                // retain their cursor so release must finish capture first.
                if (!CaptureNativeBaseline(backend)) return false;
                if (keepHeld) {
                    for (size_t i = 0; i < object.bodies.size(); ++i) {
                        if (!reassertBodies[i]) continue;
                        if (!backend.SetKinematic(object.bodies[i], true)) return false;
                        reassertBodies[i] = false;
                    }
                }
                return true;
            }

            template<class Backend>
            bool Release(Backend& backend) {
                if (!active) return true;
                if (!CaptureNativeBaseline(backend)) return false;
                if (restoreKinematic) {
                    for (size_t i = 0; i < object.bodies.size(); ++i) {
                        if (!restoreBodies[i]) continue;
                        bool alive = false;
                        if (!backend.Alive(object.bodies[i], alive)) return false;
                        if (alive && !backend.SetKinematic(object.bodies[i], originals[i])) return false;
                        restoreBodies[i] = false;
                    }
                    restoreKinematic = false;
                }
                if (!backend.Unpin()) return false;
                object = {};
                active = false;
                return true;
            }

            template<class Backend>
            bool Tick(Backend& backend, bool enabled, bool allowed, bool captured,
                uint64_t currentGeneration, bool eligible) {
                if (active && (!enabled || !allowed || captured ||
                    currentGeneration != generation || !eligible))
                    return Release(backend);
                return true;
            }
        };
    }
}
