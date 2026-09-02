// Runtime behaviour for the XR scene components.
//
// Everything here reads Modularity::XR::InputState() - the snapshot XRInput fills
// once per XR frame - and writes SceneObject transforms. It never touches OpenXR
// directly, which is the layering rule from section 48 applied to the component
// layer as well as to scripts.
//
// Ordering matters and is fixed by updateXRComponents():
//
//   1. XR Origin      - resolve the rig's world transform, hand it to XRSystem.
//   2. XR Camera      - head pose onto the camera object.
//   3. XR Controller  - grip/aim pose onto controller objects.
//   4. Action-Based Controller - edge-detect select/activate/UI press.
//   5. Interaction    - interactors act on the state produced by 1-4.
//
// Poses are written to *local* transforms, never world ones. The hierarchy pass
// then composes them under the XR Origin, which is what makes "move the origin,
// the whole rig follows" work without this file knowing anything about parenting.

#include "../Engine.h"

#include "XRInput.h"
#include "XRSystem.h"

#include <algorithm>
#include <cmath>

namespace {

using Modularity::XR::XRDevice;
using Modularity::XR::XRPoseKind;

// SceneObject's XRHand maps 1:1 onto the XR layer's device enum. Converting here
// rather than storing an XRDevice in SceneObject keeps SceneObject.h free of any
// XR include.
XRDevice ToXRDevice(XRHand hand) {
    return (hand == XRHand::Right) ? XRDevice::Right : XRDevice::Left;
}

XRPoseKind ToXRPoseKind(XRControllerPoseSource source) {
    return (source == XRControllerPoseSource::Aim) ? XRPoseKind::Aim : XRPoseKind::Grip;
}

// Writes a tracked pose into an object's local transform. Position and rotation
// are gated separately because a controller that only drives rotation (a mounted
// turret, say) is a legitimate setup.
void ApplyPoseToLocalTransform(SceneObject& obj, const Modularity::XR::XRPose& pose,
                               bool trackPosition, bool trackRotation, float scale) {
    if (trackPosition && pose.positionValid) {
        obj.localPosition = pose.position * scale;
    }
    if (trackRotation && pose.orientationValid) {
        obj.localRotation = NormalizeEulerDegrees(QuatToEulerXYZDegrees(pose.rotation));
    }
    obj.localInitialized = true;
}

// Squared distance helper; interaction tests never need the square root.
float DistanceSquared(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 d = a - b;
    return glm::dot(d, d);
}

} // namespace

// ---------------------------------------------------------------------------
// XR Origin
// ---------------------------------------------------------------------------

const SceneObject* Engine::findXROriginObject() const {
    const SceneObject* found = nullptr;
    int extras = 0;
    for (const SceneObject& obj : sceneObjects) {
        if (!obj.hasXROrigin || !obj.xrOrigin.enabled) continue;
        if (!IsObjectEnabledInHierarchy(obj)) continue;
        if (!found) {
            found = &obj;
        } else {
            ++extras;
        }
    }
    if (extras > 0) {
        // Two origins would silently fight over the same tracking space, which
        // looks like the world randomly jumping. Say so once rather than picking
        // arbitrarily and leaving the user to guess.
        // stderr rather than the console: this is a const query called every
        // frame, and addConsoleMessage is not const. The count guard keeps it to
        // one line per change instead of one per frame.
        static int warnedForCount = -1;
        if (warnedForCount != extras) {
            warnedForCount = extras;
            std::fprintf(stderr,
                         "[OpenXR Warning] Scene has %d enabled XR Origins; using the first "
                         "and ignoring the rest.\n",
                         extras + 1);
        }
    }
    return found;
}

const SceneObject* Engine::findXRCameraObject() const {
    for (const SceneObject& obj : sceneObjects) {
        if (!obj.hasXRCamera || !obj.xrCamera.enabled) continue;
        if (!IsObjectEnabledInHierarchy(obj)) continue;
        if (!obj.hasCamera) continue; // an XR Camera component without a Camera tracks nothing
        return &obj;
    }
    return nullptr;
}

void Engine::updateXRComponents(float deltaTime) {
#if !MODULARITY_HAS_OPENXR
    (void)deltaTime;
#else
    // Nothing below is free - it walks the scene several times - and a project
    // that never enables XR must not pay for any of it. This is the guard that
    // keeps "OpenXR compiled in" from costing non-XR projects anything per frame
    // (sections 42 and 43).
    if (!isXRRequestedByProject()) return;

    const Modularity::XR::XRInputState& input = Modularity::XR::InputState();

    // --- 1. XR Origin -----------------------------------------------------
    // The rig's world transform is what every tracked pose is expressed
    // relative to. With no XR Origin in the scene this stays identity, which
    // puts tracking space at the world origin - a working default, not an error.
    glm::mat4 originToWorld(1.0f);
    float rigScale = 1.0f;
    float cameraYOffset = 0.0f;
    if (const SceneObject* origin = findXROriginObject()) {
        rigScale = std::max(0.01f, origin->xrOrigin.rigScale);
        cameraYOffset = origin->xrOrigin.cameraYOffset;
        // Built from the origin's world transform, deliberately without its own
        // scale: scaling the rig is expressed by rigScale on the tracked poses,
        // and folding a non-uniform object scale in here would shear the eyes
        // apart from each other.
        originToWorld = glm::translate(glm::mat4(1.0f), origin->position) *
                        glm::mat4_cast(glm::quat(glm::radians(origin->rotation))) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(rigScale));
    }
    xrSystem.setOriginToWorld(originToWorld);

    if (!input.sessionActive) {
        // No session: leave every authored transform exactly as the user placed
        // it. Zeroing them would make the rig collapse to the origin in the
        // editor whenever XR is not running.
        return;
    }

    // --- 2. XR Camera -----------------------------------------------------
    for (SceneObject& obj : sceneObjects) {
        if (!obj.hasXRCamera || !obj.xrCamera.enabled) continue;
        if (!obj.xrCamera.applyTracking) continue;
        if (!IsObjectEnabledInHierarchy(obj)) continue;
        ApplyPoseToLocalTransform(obj, input.headPose, obj.xrCamera.trackPosition,
                                  obj.xrCamera.trackRotation, rigScale);
        // The seated-height offset rides on top of the tracked pose rather than
        // on the origin, so switching between Floor and Eye Level does not move
        // the controllers with the head.
        if (obj.xrCamera.trackPosition) {
            obj.localPosition.y += cameraYOffset;
        }
    }

    // --- 3. XR Controller -------------------------------------------------
    for (SceneObject& obj : sceneObjects) {
        if (!obj.hasXRController || !obj.xrController.enabled) continue;
        const XRControllerComponent& controller = obj.xrController;
        const Modularity::XR::XRControllerState& state =
            input.controller(ToXRDevice(controller.hand));

        if (controller.hideWhenNotTracked) {
            // Disabling the object takes its children with it, which is the point:
            // a hand model parented under the controller vanishes too.
            const bool tracked = state.active && state.gripPose.isTracked();
            if (obj.enabled != tracked) obj.enabled = tracked;
            if (!tracked) continue;
        }
        if (!IsObjectEnabledInHierarchy(obj)) continue;

        ApplyPoseToLocalTransform(obj, state.pose(ToXRPoseKind(controller.poseSource)),
                                  controller.trackPosition, controller.trackRotation, rigScale);
    }

    // --- 4. Action-Based Controller ---------------------------------------
    // Edge detection happens once, here, so every interactor reads the same
    // answer for the frame instead of each polling the button itself.
    for (SceneObject& obj : sceneObjects) {
        if (!obj.hasXRActionBasedController || !obj.xrActionBasedController.enabled) continue;
        XRActionBasedControllerComponent& c = obj.xrActionBasedController;
        if (!IsObjectEnabledInHierarchy(obj)) {
            c.selectHeld = c.selectStarted = c.selectEnded = false;
            c.activateHeld = c.activateStarted = c.activateEnded = false;
            c.uiPressHeld = false;
            continue;
        }

        const Modularity::XR::XRControllerState& state = input.controller(ToXRDevice(c.hand));
        const auto readButton = [&state](int button) {
            if (button < 0 || button >= static_cast<int>(Modularity::XR::XRButton::Count)) {
                return std::pair<bool, bool>(false, false);
            }
            const size_t index = static_cast<size_t>(button);
            return std::pair<bool, bool>(state.buttons[index], state.previousButtons[index]);
        };

        const auto [selectNow, selectPrev] = readButton(c.selectButton);
        c.selectHeld = selectNow;
        c.selectStarted = selectNow && !selectPrev;
        c.selectEnded = !selectNow && selectPrev;

        const auto [activateNow, activatePrev] = readButton(c.activateButton);
        c.activateHeld = activateNow;
        c.activateStarted = activateNow && !activatePrev;
        c.activateEnded = !activateNow && activatePrev;

        c.uiPressHeld = readButton(c.uiPressButton).first;
    }

    // --- 5. Interaction ---------------------------------------------------
    updateXRInteraction(deltaTime);
#endif
}

// ---------------------------------------------------------------------------
// XR Interaction Manager
//
// Deliberately a single pass over the scene rather than each interactor scanning
// for itself (section 23): the interactable list is gathered once and shared, so
// N interactors cost one walk instead of N.
// ---------------------------------------------------------------------------

void Engine::updateXRInteraction(float deltaTime) {
#if !MODULARITY_HAS_OPENXR
    (void)deltaTime;
#else
    if (!projectManager.currentProject.openXRSettings.interaction.enabled) return;

    // Gather candidates once, and clear last frame's one-shot events while we are
    // already walking the list.
    xrInteractableScratch.clear();
    for (SceneObject& obj : sceneObjects) {
        if (!obj.hasXRGrabInteractable) continue;
        XRGrabInteractableComponent& grab = obj.xrGrabInteractable;
        grab.hoverEnteredThisFrame = false;
        grab.hoverExitedThisFrame = false;
        grab.selectEnteredThisFrame = false;
        grab.selectExitedThisFrame = false;
        grab.activatedThisFrame = false;
        grab.deactivatedThisFrame = false;
        if (!grab.enabled || !IsObjectEnabledInHierarchy(obj)) continue;
        xrInteractableScratch.push_back(obj.id);
    }

    const auto layerAllowed = [](uint32_t mask, int layer) {
        if (layer < 0 || layer > 31) return true;
        return (mask & (1u << static_cast<uint32_t>(layer))) != 0;
    };

    // The action-based controller that drives a given interactor object. Looked
    // up on the interactor itself first, then on its parent, so the common rig
    // (controller object carries both) and a split setup both work.
    const auto findController = [this](const SceneObject& interactor) -> XRActionBasedControllerComponent* {
        if (interactor.hasXRActionBasedController) {
            return &const_cast<SceneObject&>(interactor).xrActionBasedController;
        }
        int parentId = interactor.parentId;
        for (int depth = 0; depth < 8 && parentId >= 0; ++depth) {
            SceneObject* parent = findObjectById(parentId);
            if (!parent) break;
            if (parent->hasXRActionBasedController) return &parent->xrActionBasedController;
            parentId = parent->parentId;
        }
        return nullptr;
    };

    for (SceneObject& interactorObj : sceneObjects) {
        const bool isRay = interactorObj.hasXRRayInteractor && interactorObj.xrRayInteractor.enabled;
        const bool isDirect =
            interactorObj.hasXRDirectInteractor && interactorObj.xrDirectInteractor.enabled;
        if (!isRay && !isDirect) continue;
        if (!IsObjectEnabledInHierarchy(interactorObj)) continue;

        XRActionBasedControllerComponent* controller = findController(interactorObj);

        int& hoveredId = isRay ? interactorObj.xrRayInteractor.hoveredObjectId
                               : interactorObj.xrDirectInteractor.hoveredObjectId;
        int& selectedId = isRay ? interactorObj.xrRayInteractor.selectedObjectId
                                : interactorObj.xrDirectInteractor.selectedObjectId;
        const uint32_t mask = isRay ? interactorObj.xrRayInteractor.interactionMask
                                    : interactorObj.xrDirectInteractor.interactionMask;

        // --- find what this interactor is pointing at / touching ----------
        int newHover = -1;
        if (isRay) {
            XRRayInteractorComponent& ray = interactorObj.xrRayInteractor;
            ray.hasHit = false;
            // The ray starts at the interactor's own transform, which on the
            // standard rig is the Aim-pose controller object.
            const glm::vec3 origin = interactorObj.position;
            const glm::quat rotation = glm::quat(glm::radians(interactorObj.rotation));
            const glm::vec3 direction = glm::normalize(rotation * glm::vec3(0.0f, 0.0f, -1.0f));

            float bestDistance = ray.maxDistance;
            for (int candidateId : xrInteractableScratch) {
                SceneObject* candidate = findObjectById(candidateId);
                if (!candidate || !layerAllowed(mask, candidate->layer)) continue;
                // Sphere test against the object's bounds. Cheap, stable, and does
                // not depend on the physics backend having a collider for every
                // interactable - which a decorative grabbable often will not.
                const float largestAxis = std::max(candidate->scale.x,
                                                   std::max(candidate->scale.y, candidate->scale.z));
                const float radius = std::max(0.05f, largestAxis * 0.5f);
                const glm::vec3 toTarget = candidate->position - origin;
                const float along = glm::dot(toTarget, direction);
                if (along < 0.0f || along > bestDistance) continue;
                const glm::vec3 closest = origin + direction * along;
                if (DistanceSquared(closest, candidate->position) > radius * radius) continue;
                bestDistance = along;
                newHover = candidateId;
                ray.hitPoint = closest;
                ray.hasHit = true;
            }
        } else {
            const XRDirectInteractorComponent& direct = interactorObj.xrDirectInteractor;
            const float radius = direct.interactionRadius;
            float bestDistanceSq = radius * radius;
            for (int candidateId : xrInteractableScratch) {
                SceneObject* candidate = findObjectById(candidateId);
                if (!candidate || !layerAllowed(mask, candidate->layer)) continue;
                const float distanceSq = DistanceSquared(candidate->position, interactorObj.position);
                if (distanceSq > bestDistanceSq) continue;
                bestDistanceSq = distanceSq;
                newHover = candidateId;
            }
        }

        // --- hover transitions --------------------------------------------
        if (newHover != hoveredId) {
            if (SceneObject* previous = findObjectById(hoveredId)) {
                if (previous->hasXRGrabInteractable) {
                    previous->xrGrabInteractable.hoverExitedThisFrame = true;
                    previous->xrGrabInteractable.hoveredByObjectId = -1;
                }
            }
            if (SceneObject* current = findObjectById(newHover)) {
                if (current->hasXRGrabInteractable) {
                    current->xrGrabInteractable.hoverEnteredThisFrame = true;
                    current->xrGrabInteractable.hoveredByObjectId = interactorObj.id;
                }
            }
            hoveredId = newHover;
        }

        // --- selection ----------------------------------------------------
        const bool selectStarted = controller && controller->selectStarted;
        const bool selectEnded = controller && controller->selectEnded;

        if (selectStarted && selectedId < 0 && hoveredId >= 0) {
            if (SceneObject* target = findObjectById(hoveredId)) {
                if (beginXRGrab(*target, interactorObj, controller)) {
                    selectedId = hoveredId;
                    target->xrGrabInteractable.selectEnteredThisFrame = true;
                }
            }
        } else if (selectEnded && selectedId >= 0) {
            if (SceneObject* target = findObjectById(selectedId)) {
                endXRGrab(*target, interactorObj);
                target->xrGrabInteractable.selectExitedThisFrame = true;
            }
            selectedId = -1;
        }

        // --- activation ---------------------------------------------------
        if (controller && selectedId >= 0) {
            if (SceneObject* target = findObjectById(selectedId)) {
                if (controller->activateStarted) {
                    target->xrGrabInteractable.activatedThisFrame = true;
                } else if (controller->activateEnded) {
                    target->xrGrabInteractable.deactivatedThisFrame = true;
                }
            }
        }

        // --- held-object follow -------------------------------------------
        if (selectedId >= 0) {
            if (SceneObject* target = findObjectById(selectedId)) {
                updateXRGrabFollow(*target, interactorObj, deltaTime);
            } else {
                selectedId = -1; // the interactable was deleted mid-grab
            }
        }
    }
#endif
}

bool Engine::beginXRGrab(SceneObject& interactable, SceneObject& interactor,
                         XRActionBasedControllerComponent* controller) {
    XRGrabInteractableComponent& grab = interactable.xrGrabInteractable;
    if (grab.heldByObjectId >= 0) return false; // already held by someone else

    const bool isRight = controller && controller->hand == XRHand::Right;
    if (isRight && !grab.allowRightHand) return false;
    if (!isRight && !grab.allowLeftHand) return false;

    grab.heldByObjectId = interactor.id;

    // Remember the physics state so release can put it back exactly. Without
    // this, grabbing a non-gravity object and dropping it would quietly turn
    // gravity on.
    if (interactable.hasRigidbody) {
        grab.wasKinematic = interactable.rigidbody.isKinematic;
        grab.hadGravity = interactable.rigidbody.useGravity;
        grab.stateSaved = true;
        if (grab.movementType != XRGrabInteractableComponent::MovementType::VelocityTracking) {
            // Instant/Kinematic drive the transform directly, so the body must
            // stop being simulated or the two fight every frame.
            interactable.rigidbody.isKinematic = true;
            interactable.rigidbody.useGravity = false;
        } else {
            interactable.rigidbody.useGravity = false;
        }
    }

    if (controller && controller->enableHaptics) {
        Modularity::XR::XRHapticRequest pulse;
        pulse.device = (controller->hand == XRHand::Right) ? Modularity::XR::XRDevice::Right
                                                           : Modularity::XR::XRDevice::Left;
        pulse.amplitude = controller->hapticAmplitude;
        pulse.duration = controller->hapticDuration;
        Modularity::XR::RequestHapticPulse(pulse);
    }
    return true;
}

void Engine::updateXRGrabFollow(SceneObject& interactable, SceneObject& interactor,
                                float deltaTime) {
    const XRGrabInteractableComponent& grab = interactable.xrGrabInteractable;

    // Where the object should be: the interactor's transform, offset by the
    // attach transform when one is set.
    glm::vec3 targetPosition = interactor.position;
    glm::quat targetRotation = glm::quat(glm::radians(interactor.rotation));
    if (grab.attachTransformId >= 0) {
        if (const SceneObject* attach = findObjectById(grab.attachTransformId)) {
            // The attach point is expressed in the interactable's own space, so
            // the object hangs off the hand by the inverse of that offset.
            const glm::vec3 offset = attach->position - interactable.position;
            targetPosition -= targetRotation * offset;
        }
    }

    switch (grab.movementType) {
        case XRGrabInteractableComponent::MovementType::VelocityTracking: {
            // Drive velocity toward the target instead of teleporting, so the held
            // object still collides with the world - you can knock things over
            // with it, and it cannot be shoved through a wall.
            if (interactable.hasRigidbody && deltaTime > 1e-5f) {
                const glm::vec3 delta = targetPosition - interactable.position;
                const glm::vec3 velocity = delta / deltaTime;
                setRigidbodyVelocityFromScript(interactable.id, velocity);
                if (grab.trackRotation) {
                    interactable.rotation =
                        NormalizeEulerDegrees(QuatToEulerXYZDegrees(targetRotation));
                    syncLocalTransform(interactable);
                }
                break;
            }
            // No rigidbody to drive: fall through to the direct path rather than
            // leaving the object stuck in mid-air.
            [[fallthrough]];
        }
        case XRGrabInteractableComponent::MovementType::Instant:
        case XRGrabInteractableComponent::MovementType::Kinematic:
        default: {
            if (grab.trackPosition) interactable.position = targetPosition;
            if (grab.trackRotation) {
                interactable.rotation = NormalizeEulerDegrees(QuatToEulerXYZDegrees(targetRotation));
            }
            syncLocalTransform(interactable);
            if (interactable.hasRigidbody) {
                teleportPhysicsActorFromScript(interactable.id, interactable.position,
                                               interactable.rotation);
            }
            break;
        }
    }
}

void Engine::endXRGrab(SceneObject& interactable, SceneObject& interactor) {
    XRGrabInteractableComponent& grab = interactable.xrGrabInteractable;
    grab.heldByObjectId = -1;

    if (interactable.hasRigidbody && grab.stateSaved) {
        interactable.rigidbody.isKinematic = grab.wasKinematic;
        interactable.rigidbody.useGravity = grab.hadGravity;
        grab.stateSaved = false;
    }

    if (!grab.throwOnDetach || !interactable.hasRigidbody) return;

#if MODULARITY_HAS_OPENXR
    // Throw with the controller's own velocity rather than a frame-to-frame
    // difference of the object's position: the runtime's velocity is smoothed and
    // sampled at display time, which is what makes a thrown object go where the
    // player expects instead of jittering off at a tangent.
    const Modularity::XR::XRInputState& input = Modularity::XR::InputState();
    XRHand hand = XRHand::Right;
    if (interactor.hasXRActionBasedController) {
        hand = interactor.xrActionBasedController.hand;
    } else if (interactor.hasXRController) {
        hand = interactor.xrController.hand;
    }
    const Modularity::XR::XRControllerState& state =
        input.controller((hand == XRHand::Right) ? Modularity::XR::XRDevice::Right
                                                 : Modularity::XR::XRDevice::Left);
    if (state.hasVelocity) {
        setRigidbodyVelocityFromScript(interactable.id,
                                       state.velocity * grab.throwVelocityScale);
        setRigidbodyAngularVelocityFromScript(
            interactable.id, state.angularVelocity * grab.throwAngularVelocityScale);
    }
#else
    (void)interactor;
#endif
}
