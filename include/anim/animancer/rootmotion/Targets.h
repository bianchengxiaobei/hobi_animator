#pragma once

#include "anim/animancer/rootmotion/RootMotion.h"
#include "anim/core/Transform.h"

namespace anim {

class RootMotionToTransform : public IRootMotionTarget {
public:
    explicit RootMotionToTransform(Transform* target = nullptr) : target(target) {}

    Transform* target = nullptr;
    bool isEnabled = true;

    bool enabled() const override { return isEnabled; }

    void applyRootMotion(const RootMotionDelta& delta) override {
        if (target == nullptr) return;
        target->translation += delta.deltaPosition;
        target->rotation = safeNormalize(target->rotation * delta.deltaRotation);
    }
};

class ICharacterControllerLike {
public:
    virtual ~ICharacterControllerLike() = default;
    virtual void move(const Vec3& delta) = 0;
    virtual void rotate(const Quat& deltaRotation) = 0;
};

class RootMotionToCharacterController : public IRootMotionTarget {
public:
    explicit RootMotionToCharacterController(ICharacterControllerLike* controller = nullptr)
        : controller(controller) {}

    ICharacterControllerLike* controller = nullptr;

    void applyRootMotion(const RootMotionDelta& delta) override {
        if (controller == nullptr) return;
        controller->move(delta.deltaPosition);
        controller->rotate(delta.deltaRotation);
    }
};

class IRigidbodyLike {
public:
    virtual ~IRigidbodyLike() = default;
    virtual Vec3 position() const = 0;
    virtual Quat rotation() const = 0;
    virtual void movePosition(const Vec3& position) = 0;
    virtual void moveRotation(const Quat& rotation) = 0;
};

class RootMotionToRigidbody : public IRootMotionTarget {
public:
    explicit RootMotionToRigidbody(IRigidbodyLike* rigidbody = nullptr) : rigidbody(rigidbody) {}

    IRigidbodyLike* rigidbody = nullptr;

    void applyRootMotion(const RootMotionDelta& delta) override {
        if (rigidbody == nullptr) return;
        rigidbody->movePosition(rigidbody->position() + delta.deltaPosition);
        rigidbody->moveRotation(safeNormalize(rigidbody->rotation() * delta.deltaRotation));
    }
};

}  // namespace anim
