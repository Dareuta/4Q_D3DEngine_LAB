#include "PhysXWorld_Internal.h"

// ============================================================
//  Wrapper types
// ============================================================

class PhysXActor : public IPhysicsActor
{
public:
	PhysXActor(PxRigidActor* a, std::weak_ptr<PhysXWorld::Impl> w)
		: actor(a), world(std::move(w))
	{
	}

	~PhysXActor() override
	{
		auto s = world.lock();
		if (s && actor)
			s->EnqueueRelease(actor);
		actor = nullptr;
	}

	bool IsValid() const override { return actor != nullptr; }

	bool IsInWorld() const override
	{
		return actor && actor->getScene() != nullptr;
	}

	void SetInWorld(bool inWorld) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		// Defer-safe: queue adds/removes and let FlushPending handle ordering.
		if (inWorld)
		{
			if (!actor->getScene())
				s->EnqueueAdd(actor);
		}
		else
		{
			if (actor->getScene())
				s->EnqueueRemove(actor);
		}
	}

	void SetTransform(const Vec3& p, const Quat& q) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		actor->setGlobalPose(ToPxTransform(p, q));
	}

	Vec3 GetPosition() const override
	{
		if (!actor) return Vec3::Zero;
		auto s = world.lock();
		if (!s || !s->scene) return Vec3::Zero;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return FromPx(actor->getGlobalPose().p);
	}

	Quat GetRotation() const override
	{
		if (!actor) return Quat::Identity;
		auto s = world.lock();
		if (!s || !s->scene) return Quat::Identity;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return FromPx(actor->getGlobalPose().q);
	}

	void SetUserData(void* ptr) override
	{
		if (!actor) return;
		actor->userData = ptr;
	}

	void* GetUserData() const override
	{
		return actor ? actor->userData : nullptr;
	}

	void SetLayerMasks(uint32_t layerBits, uint32_t collideMask, uint32_t queryMask) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		FilterDesc f;
		f.layerBits = layerBits;
		f.collideMask = collideMask;
		f.queryMask = queryMask;

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			// preserve trigger flag
			PxShapeFlags sf = sh->getFlags();
			f.isTrigger = HasShapeFlag(sf, PxShapeFlag::eTRIGGER_SHAPE);
			ApplyFilterToShape(*sh, f);
		}
	}

	void SetTrigger(bool isTrigger) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;

			FilterDesc f;
			const PxFilterData qd = sh->getQueryFilterData();
			const PxFilterData sd = sh->getSimulationFilterData();
			f.layerBits = qd.word0;
			f.queryMask = qd.word2;
			f.collideMask = sd.word1;
			f.isTrigger = isTrigger;
			ApplyFilterToShape(*sh, f);
		}
	}

	void SetMaterial(float staticFriction, float dynamicFriction, float restitution) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		MaterialDesc md;
		md.staticFriction = staticFriction;
		md.dynamicFriction = dynamicFriction;
		md.restitution = restitution;

		PxMaterial* mat = s->GetOrCreateMaterial(md);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			sh->setMaterials(&mat, 1);
		}
	}

	void SetCollisionEnabled(bool enabled) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			PxShapeFlags f = sh->getFlags();
			if (HasShapeFlag(f, PxShapeFlag::eTRIGGER_SHAPE))
				continue; // triggers never participate in simulation contacts
			// PhysX 5.x: PxFlags doesn't expose a .set() helper. Use PxShape::setFlag().
			sh->setFlag(PxShapeFlag::eSIMULATION_SHAPE, enabled);
		}
	}

	bool IsCollisionEnabled() const override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;
		SceneReadLock rl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			PxShapeFlags f = sh->getFlags();
			if (HasShapeFlag(f, PxShapeFlag::eTRIGGER_SHAPE))
				continue;
			return HasShapeFlag(f, PxShapeFlag::eSIMULATION_SHAPE);
		}
		return false;
	}

	void SetQueryEnabled(bool enabled) override
	{
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			// PhysX 5.x: PxFlags doesn't expose a .set() helper. Use PxShape::setFlag().
			sh->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, enabled);
		}
	}

	bool IsQueryEnabled() const override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;
		SceneReadLock rl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		if (n == 0) return false;
		PxShape* sh = nullptr;
		actor->getShapes(&sh, 1);
		if (!sh) return false;
		return HasShapeFlag(sh->getFlags(), PxShapeFlag::eSCENE_QUERY_SHAPE);
	}

	void Destroy() override
	{
		if (!actor) return;
		auto s = world.lock();
		if (s)
		{
			// Remove first (if present), then release.
			if (actor->getScene())
				s->EnqueueRemove(actor);
			s->EnqueueRelease(actor);
		}
		actor = nullptr;
	}

	// ---- Shapes

	bool AddBoxShape(const BoxColliderDesc& box, const Vec3& localPos, const Quat& localRot) override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;

		const PxBoxGeometry geom(ToPx(box.halfExtents));
		return AddShapeCommon(geom, box, localPos, localRot);
	}

	bool AddSphereShape(const SphereColliderDesc& sphere, const Vec3& localPos, const Quat& localRot) override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;

		const PxSphereGeometry geom(sphere.radius);
		return AddShapeCommon(geom, sphere, localPos, localRot);
	}

	bool AddCapsuleShape(const CapsuleColliderDesc& capsule, const Vec3& localPos, const Quat& localRot) override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;

		const PxCapsuleGeometry geom(capsule.radius, capsule.halfHeight);
		// Optional alignment to +Y
		Quat q = localRot;
		if (capsule.alignYAxis)
		{
			Quat align = FromPx(CapsuleAlignQuatPx());
			q = align * q;
		}
		return AddShapeCommon(geom, capsule, localPos, q);
	}

	bool AddTriangleMeshShape(const TriangleMeshColliderDesc& mesh, const Vec3& localPos, const Quat& localRot) override
	{
#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;

		PxTriangleMesh* tm = s->GetOrCreateTriangleMesh(mesh);
		if (!tm) return false;

		PxMeshGeometryFlags gflags;
		if (mesh.doubleSidedQueries) gflags |= PxMeshGeometryFlag::eDOUBLE_SIDED;

		const PxMeshScale scale(ToPx(mesh.scale));
		const PxTriangleMeshGeometry geom(tm, scale, gflags);
		return AddShapeCommon(geom, mesh, localPos, localRot);
#else
		(void)mesh; (void)localPos; (void)localRot;
		return false;
#endif
	}

	bool AddConvexMeshShape(const ConvexMeshColliderDesc& mesh, const Vec3& localPos, const Quat& localRot) override
	{
#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;

		PxConvexMesh* cm = s->GetOrCreateConvexMesh(mesh);
		if (!cm) return false;

		const PxMeshScale scale(ToPx(mesh.scale));
		const PxConvexMeshGeometry geom(cm, scale);
		return AddShapeCommon(geom, mesh, localPos, localRot);
#else
		(void)mesh; (void)localPos; (void)localRot;
		return false;
#endif
	}

	bool ClearShapes() override
	{
		if (!actor) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);

		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			actor->detachShape(*sh);
		}
		return true;
	}

	uint32_t GetShapeCount() const override
	{
		return actor ? actor->getNbShapes() : 0u;
	}

	void* GetNativeActor() const override { return actor; }

protected:
	template<class PxGeomT, class DescT>
	bool AddShapeCommon(const PxGeomT& geom, const DescT& desc, const Vec3& localPos, const Quat& localRot)
	{
		auto s = world.lock();
		if (!s || !s->scene || !s->physics) return false;

		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		PxMaterial* mat = s->GetOrCreateMaterial(desc);
		PxShape* sh = s->physics->createShape(geom, *mat, true);
		if (!sh) return false;

		ApplyFilterToShape(*sh, desc);
		// Optional per-shape tag (useful for compound bodies / per-collider identification).
		sh->userData = desc.userData;
		sh->setLocalPose(ToPxTransform(localPos, localRot));

		actor->attachShape(*sh);
		sh->release();

		return true;
	}

	PxRigidActor* actor = nullptr;
	std::weak_ptr<PhysXWorld::Impl> world;
};


class PhysXRigidBody final : public IRigidBody
{
public:
	PhysXRigidBody(PxRigidDynamic* b, std::weak_ptr<PhysXWorld::Impl> w, const RigidBodyDesc& rb)
		: base(b, w), body(b), world(std::move(w)), cachedRb(rb)
	{
	}

	// ------------------------------------------------------------
	// IPhysicsActor (forwarded to shared actor wrapper)
	// ------------------------------------------------------------
	bool IsValid() const override { return base.IsValid(); }
	bool IsInWorld() const override { return base.IsInWorld(); }
	void SetInWorld(bool inWorld) override { base.SetInWorld(inWorld); }

	void SetTransform(const Vec3& p, const Quat& q) override { base.SetTransform(p, q); }
	Vec3 GetPosition() const override { return base.GetPosition(); }
	Quat GetRotation() const override { return base.GetRotation(); }

	void SetUserData(void* ptr) override { base.SetUserData(ptr); }
	void* GetUserData() const override { return base.GetUserData(); }

	void SetLayerMasks(uint32_t layerBits, uint32_t collideMask, uint32_t queryMask) override
	{
		base.SetLayerMasks(layerBits, collideMask, queryMask);
	}

	void SetTrigger(bool isTrigger) override { base.SetTrigger(isTrigger); }

	void SetCollisionEnabled(bool enabled) override { base.SetCollisionEnabled(enabled); }
	bool IsCollisionEnabled() const override { return base.IsCollisionEnabled(); }
	void SetQueryEnabled(bool enabled) override { base.SetQueryEnabled(enabled); }
	bool IsQueryEnabled() const override { return base.IsQueryEnabled(); }

	void Destroy() override
	{
		base.Destroy();
		body = nullptr;
	}

	void SetMaterial(float staticFriction, float dynamicFriction, float restitution) override
	{
		base.SetMaterial(staticFriction, dynamicFriction, restitution);
	}

	bool AddBoxShape(const BoxColliderDesc& box, const Vec3& localPos, const Quat& localRot) override
	{
		return base.AddBoxShape(box, localPos, localRot);
	}

	bool AddSphereShape(const SphereColliderDesc& sphere, const Vec3& localPos, const Quat& localRot) override
	{
		return base.AddSphereShape(sphere, localPos, localRot);
	}

	bool AddCapsuleShape(const CapsuleColliderDesc& capsule, const Vec3& localPos, const Quat& localRot) override
	{
		return base.AddCapsuleShape(capsule, localPos, localRot);
	}

	bool AddTriangleMeshShape(const TriangleMeshColliderDesc& mesh, const Vec3& localPos, const Quat& localRot) override
	{
		return base.AddTriangleMeshShape(mesh, localPos, localRot);
	}

	bool AddConvexMeshShape(const ConvexMeshColliderDesc& mesh, const Vec3& localPos, const Quat& localRot) override
	{
		return base.AddConvexMeshShape(mesh, localPos, localRot);
	}

	bool ClearShapes() override { return base.ClearShapes(); }
	uint32_t GetShapeCount() const override { return base.GetShapeCount(); }

	void* GetNativeActor() const override { return base.GetNativeActor(); }

	// ------------------------------------------------------------
	// IRigidBody
	// ------------------------------------------------------------
	void SetKinematicTarget(const Vec3& p, const Quat& q) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		// PhysX will assert if you call this on a non-kinematic.
		if (!HasRigidBodyFlag(body->getRigidBodyFlags(), PxRigidBodyFlag::eKINEMATIC))
		{
			body->setGlobalPose(ToPxTransform(p, q));
			return;
		}
		body->setKinematicTarget(ToPxTransform(p, q));
	}

	bool IsKinematic() const override
	{
		return body ? HasRigidBodyFlag(body->getRigidBodyFlags(), PxRigidBodyFlag::eKINEMATIC) : false;
	}

	void SetKinematic(bool isKinematic) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, isKinematic);
		cachedRb.isKinematic = isKinematic;
		if (!isKinematic)
			RecomputeMass();
	}

	void SetGravityEnabled(bool enabled) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !enabled);
		cachedRb.gravityEnabled = enabled;
	}

	bool IsGravityEnabled() const override
	{
		if (!body) return false;
		auto s = world.lock();
		if (!s || !s->scene) return false;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return !HasActorFlag(body->getActorFlags(), PxActorFlag::eDISABLE_GRAVITY);
	}

	void SetLinearVelocity(const Vec3& v) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setLinearVelocity(ToPx(v));
	}

	Vec3 GetLinearVelocity() const override
	{
		if (!body) return Vec3::Zero;
		auto s = world.lock();
		if (!s || !s->scene) return Vec3::Zero;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return FromPx(body->getLinearVelocity());
	}

	void SetAngularVelocity(const Vec3& v) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setAngularVelocity(ToPx(v));
	}

	Vec3 GetAngularVelocity() const override
	{
		if (!body) return Vec3::Zero;
		auto s = world.lock();
		if (!s || !s->scene) return Vec3::Zero;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return FromPx(body->getAngularVelocity());
	}

	void AddForce(const Vec3& f) override { AddForceEx(f, ForceMode::Force, true); }
	void AddImpulse(const Vec3& impulse) override { AddForceEx(impulse, ForceMode::Impulse, true); }
	void AddTorque(const Vec3& t) override { AddTorqueEx(t, ForceMode::Force, true); }

	void AddForceEx(const Vec3& f, ForceMode mode, bool autowake) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->addForce(ToPx(f), ToPxForceMode(mode), autowake);
	}

	void AddTorqueEx(const Vec3& t, ForceMode mode, bool autowake) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->addTorque(ToPx(t), ToPxForceMode(mode), autowake);
	}

	void SetDamping(float linear, float angular) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setLinearDamping(linear);
		body->setAngularDamping(angular);
		cachedRb.linearDamping = linear;
		cachedRb.angularDamping = angular;
	}

	void SetMaxVelocities(float maxLinear, float maxAngular) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		if (maxLinear > 0.0f) body->setMaxLinearVelocity(maxLinear);
		if (maxAngular > 0.0f) body->setMaxAngularVelocity(maxAngular);
		cachedRb.maxLinearVelocity = maxLinear;
		cachedRb.maxAngularVelocity = maxAngular;
	}

	void SetLockFlags(RigidBodyLockFlags flags) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setRigidDynamicLockFlags(ToPxLockFlags(flags));
		cachedRb.lockFlags = flags;
	}

	void SetCCDEnabled(bool enabled, bool speculative) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, enabled);
		body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD, speculative);
		cachedRb.enableCCD = enabled;
		cachedRb.enableSpeculativeCCD = speculative;
	}

	float GetMass() const override
	{
		if (!body) return 0.0f;
		return body->getMass();
	}

	void SetMass(float mass, bool updateInertia) override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		if (updateInertia)
			PxRigidBodyExt::setMassAndUpdateInertia(*body, mass);
		else
			body->setMass(mass);

		cachedRb.massOverride = mass;
	}

	void RecomputeMass() override
	{
		if (!body) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		ApplyMass(*body, cachedRb);
	}

	void WakeUp() override
	{
		if (!body) return;
		body->wakeUp();
	}

	void PutToSleep() override
	{
		if (!body) return;
		body->putToSleep();
	}

	bool IsAwake() const override
	{
		if (!body) return false;
		return !body->isSleeping();
	}

	bool IsSleeping() const override
	{
		if (!body) return false;
		return body->isSleeping();
	}

private:
	PhysXActor base;
	PxRigidDynamic* body = nullptr;
	std::weak_ptr<PhysXWorld::Impl> world;
	RigidBodyDesc cachedRb{};
};

// ============================================================


std::unique_ptr<IRigidBody> PhysXWorld::CreateDynamicEmpty(const Vec3& pos, const Quat& rot, const RigidBodyDesc& rb)
{
	if (!impl || !impl->physics) return {};

	PxRigidDynamic* body = impl->physics->createRigidDynamic(ToPxTransform(pos, rot));
	if (!body) return {};

	ApplyRbDesc(*body, rb);

	if (impl->enableActiveTransforms)
		body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_POSE_INTEGRATION_PREVIEW, true);

	// Set reasonable defaults to avoid invalid mass/inertia before shapes are attached.
	body->setMass(1.0f);
	body->setMassSpaceInertiaTensor(PxVec3(1.0f, 1.0f, 1.0f));

	impl->EnqueueAdd(body);
	return std::make_unique<PhysXRigidBody>(body, impl, rb);
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticEmpty(const Vec3& pos, const Quat& rot, void* userData)
{
	if (!impl || !impl->physics) return {};

	PxRigidStatic* actor = impl->physics->createRigidStatic(ToPxTransform(pos, rot));
	if (!actor) return {};
	actor->userData = userData;

	impl->EnqueueAdd(actor);
	return std::make_unique<PhysXActor>(actor, impl);
}

std::unique_ptr<IRigidBody> PhysXWorld::CreateDynamicBox(const Vec3& pos, const Quat& rot, const RigidBodyDesc& rb, const BoxColliderDesc& box)
{
	auto body = CreateDynamicEmpty(pos, rot, rb);
	if (!body) return {};
	body->AddBoxShape(box);
	body->RecomputeMass();
	return body;
}

std::unique_ptr<IRigidBody> PhysXWorld::CreateDynamicSphere(const Vec3& pos, const Quat& rot, const RigidBodyDesc& rb, const SphereColliderDesc& sphere)
{
	auto body = CreateDynamicEmpty(pos, rot, rb);
	if (!body) return {};
	body->AddSphereShape(sphere);
	body->RecomputeMass();
	return body;
}

std::unique_ptr<IRigidBody> PhysXWorld::CreateDynamicCapsule(const Vec3& pos, const Quat& rot, const RigidBodyDesc& rb, const CapsuleColliderDesc& capsule)
{
	auto body = CreateDynamicEmpty(pos, rot, rb);
	if (!body) return {};
	body->AddCapsuleShape(capsule);
	body->RecomputeMass();
	return body;
}

void PhysXWorld::CreateStaticPlane(float staticFriction, float dynamicFriction, float restitution, const FilterDesc& filter)
{
	(void)CreateStaticPlaneActor(staticFriction, dynamicFriction, restitution, filter);
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticPlaneActor(float staticFriction, float dynamicFriction, float restitution, const FilterDesc& filter)
{
	if (!impl || !impl->physics) return {};

	MaterialDesc md;
	md.staticFriction = staticFriction;
	md.dynamicFriction = dynamicFriction;
	md.restitution = restitution;
	PxMaterial* mat = impl->GetOrCreateMaterial(md);

	PxRigidStatic* plane = PxCreatePlane(*impl->physics, PxPlane(0, 1, 0, 0), *mat);
	if (!plane) return {};
	// Convenience: allow user code to tag the created actor.
	plane->userData = filter.userData;

	// Apply filter to its only shape
	PxShape* sh = nullptr;
	if (plane->getNbShapes() == 1)
	{
		plane->getShapes(&sh, 1);
		if (sh) ApplyFilterToShape(*sh, filter);
	}

	impl->EnqueueAdd(plane);
	return std::make_unique<PhysXActor>(plane, impl);
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticBox(const Vec3& pos, const Quat& rot, const BoxColliderDesc& box)
{
	auto a = CreateStaticEmpty(pos, rot, box.userData);
	if (!a) return {};
	a->AddBoxShape(box);
	return a;
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticSphere(const Vec3& pos, const Quat& rot, const SphereColliderDesc& sphere)
{
	auto a = CreateStaticEmpty(pos, rot, sphere.userData);
	if (!a) return {};
	a->AddSphereShape(sphere);
	return a;
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticCapsule(const Vec3& pos, const Quat& rot, const CapsuleColliderDesc& capsule)
{
	auto a = CreateStaticEmpty(pos, rot, capsule.userData);
	if (!a) return {};
	a->AddCapsuleShape(capsule);
	return a;
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticTriangleMesh(const Vec3& pos, const Quat& rot, const TriangleMeshColliderDesc& mesh)
{
	auto a = CreateStaticEmpty(pos, rot, mesh.userData);
	if (!a) return {};
	if (!a->AddTriangleMeshShape(mesh))
		return {};
	return a;
}

std::unique_ptr<IPhysicsActor> PhysXWorld::CreateStaticConvexMesh(const Vec3& pos, const Quat& rot, const ConvexMeshColliderDesc& mesh)
{
	auto a = CreateStaticEmpty(pos, rot, mesh.userData);
	if (!a) return {};
	if (!a->AddConvexMeshShape(mesh))
		return {};
	return a;
}

std::unique_ptr<IRigidBody> PhysXWorld::CreateDynamicConvexMesh(const Vec3& pos, const Quat& rot, const RigidBodyDesc& rb, const ConvexMeshColliderDesc& mesh)
{
	auto body = CreateDynamicEmpty(pos, rot, rb);
	if (!body) return {};
	if (!body->AddConvexMeshShape(mesh))
		return {};
	body->RecomputeMass();
	return body;
}

