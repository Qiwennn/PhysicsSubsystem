# CSCI 522 Assignment 4 — Physics Subsystem
# Demo Video
https://youtu.be/htW1qmgt1zQ
# What’s Implemented
# 1) Physics component & flat graph
- Where: `MeshManager::getAsset()`, `MeshInstance::createPhysicsManager()`, `SkeletonInstance::createPhysicsManager()`.
- What:
  - `PhysicsManager` is a component tracked via a flat list.
  - Cloned from `Mesh` onto each `MeshInstance`; also added to `SkeletonInstance` for animated actors.
# 2) Bounding volumes (build once, update per-frame)
- Where: build at load in `MeshManager::getAsset()`; update in `PhysicsManager::buildBoundingVolumeAfterTransform()` and `DefaultAnimationSM::do_PRE_RENDER_needsRC()`.
- What:
  - Compute local-space AABB from vertex positions at load; store 8 corners and 4 helper matrices.
  - Per frame, transform corners by the world matrix to get OBB data
# 3) OBB–OBB collision
- Where: `PhysicsManager::checkCollisionPhysicsManager()`, `checkSegmentIntersect()`.
- What:
  - Use 6 face normals (3 from each box) as separating axes.
  - Project 8 vertices of both boxes onto each axis; test interval overlap; early out on a separating axis.
# 4) Contact plane gathering
- Where: `PhysicsManager::addCollisionPlane()`, `checkCollisionPlane()`, `checkStuck()`. 
- What:
  - From the intersecting OBB, collect the most relevant planes that constrain motion.
  - Track current stand plane and detect “inside” cases to prevent tunneling/stuck states.
# 5) Gravity & falling
- Where: `SoldierNPCMovementSM::do_UPDATE()`.
- What:
  - Integrate a constant acceleration when `m_collisionCount == 0`; `accumulate m_fallingTime`.
  - When grounded, reset fall timer and clamp Y to the current stand plane height.
# 6) Sliding / avoidance response
- Where: `SoldierNPCMovementSM::do_UPDATE()`.
- What:
  - On wall contact, compute a slide direction parallel to the contact plane (left/right choice by target proximity).
  - Brief “backward” phase to clear penetration, then continue along the selected tangent.
# 7) Engine wiring
- Where: `MeshManager::getAsset()`, `MeshInstance::createPhysicsManager()`, `SkeletonInstance::createPhysicsManager()`.
- What:
  - Physics bounds built at load for static meshes; per-instance physics copied to each `MeshInstance`.
  - Animated soldiers own a `PhysicsManager` so their bounds follow the current pose.
