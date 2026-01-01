#include "PhysXWorld_Internal.h"

// ============================================================
//  Character Controller (CCT) wrapper
// ============================================================

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS

static inline PxControllerNonWalkableMode::Enum ToPx(CCTNonWalkableMode m)
{
	switch (m)
	{
	case CCTNonWalkableMode::PreventClimbing: return PxControllerNonWalkableMode::ePREVENT_CLIMBING;
	case CCTNonWalkableMode::PreventClimbingAndForceSliding: return PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING;
	default: return PxControllerNonWalkableMode::ePREVENT_CLIMBING;
	}
}

static inline PxCapsuleClimbingMode::Enum ToPx(CCTCapsuleClimbingMode m)
{
	switch (m)
	{
	case CCTCapsuleClimbingMode::Easy: return PxCapsuleClimbingMode::eEASY;
	case CCTCapsuleClimbingMode::Constrained: return PxCapsuleClimbingMode::eCONSTRAINED;
	default: return PxCapsuleClimbingMode::eCONSTRAINED;
	}
}

static inline CCTCollisionFlags FromPx(physx::PxControllerCollisionFlags f)
{
	CCTCollisionFlags out = CCTCollisionFlags::None;
	if (f.isSet(physx::PxControllerCollisionFlag::eCOLLISION_SIDES)) out |= CCTCollisionFlags::Sides;
	if (f.isSet(physx::PxControllerCollisionFlag::eCOLLISION_UP))    out |= CCTCollisionFlags::Up;
	if (f.isSet(physx::PxControllerCollisionFlag::eCOLLISION_DOWN))  out |= CCTCollisionFlags::Down;
	return out;
}

class MaskQueryCallbackIgnoreActor final : public PxQueryFilterCallback
{
public:
	MaskQueryCallbackIgnoreActor(uint32_t layerMaskIn, uint32_t queryMaskIn, bool includeTriggersIn, QueryHitMode hitModeIn, const PxRigidActor* ignoreIn)
		: layerMask(layerMaskIn), queryMask(queryMaskIn), includeTriggers(includeTriggersIn), hitMode(hitModeIn), ignore(ignoreIn)
	{
	}

	PxQueryHitType::Enum preFilter(
		const PxFilterData& /*filterData*/, const PxShape* shape, const PxRigidActor* actor, PxHitFlags& /*queryFlags*/) override
	{
		if (!shape) return PxQueryHitType::eNONE;
		if (ignore && actor == ignore) return PxQueryHitType::eNONE;

		const PxShapeFlags sf = shape->getFlags();
		if (!HasShapeFlag(sf, PxShapeFlag::eSCENE_QUERY_SHAPE))
			return PxQueryHitType::eNONE;
		if (!includeTriggers && HasShapeFlag(sf, PxShapeFlag::eTRIGGER_SHAPE))
			return PxQueryHitType::eNONE;

		const PxFilterData fd = shape->getQueryFilterData();
		const uint32_t shapeLayerBits = fd.word0;
		const uint32_t shapeQueryMask = fd.word2;

		if ((shapeLayerBits & layerMask) == 0)
			return PxQueryHitType::eNONE;
		if ((shapeQueryMask & queryMask) == 0)
			return PxQueryHitType::eNONE;

		return (hitMode == QueryHitMode::Block) ? PxQueryHitType::eBLOCK : PxQueryHitType::eTOUCH;
	}

	PxQueryHitType::Enum postFilter(
		const PxFilterData& /*filterData*/, const PxQueryHit& /*hit*/,
		const PxShape* /*shape*/, const PxRigidActor* /*actor*/) override
	{
		return (hitMode == QueryHitMode::Block) ? PxQueryHitType::eBLOCK : PxQueryHitType::eTOUCH;
	}

private:
	uint32_t layerMask = 0xFFFFFFFFu;
	uint32_t queryMask = 0xFFFFFFFFu;
	bool includeTriggers = false;
	QueryHitMode hitMode = QueryHitMode::Block;
	const PxRigidActor* ignore = nullptr;
};

class PhysXCharacterController final : public ICharacterController
{
public:
	PhysXCharacterController(PxController* inController, PxRigidDynamic* inActor, const CharacterControllerDesc& desc, std::weak_ptr<PhysXWorld::Impl> w)
		: controller(inController), actor(inActor), world(std::move(w)), type(desc.type)
	{
		radius = (desc.type == CCTType::Capsule) ? desc.radius : max(desc.halfExtents.x, max(desc.halfExtents.y, desc.halfExtents.z));
		halfHeight = (desc.type == CCTType::Capsule) ? desc.halfHeight : desc.halfExtents.y;
		up = desc.upDirection;
		if (!physwrap::NormalizeSafe(up)) up = Vec3::UnitY;
		stepOffset = desc.stepOffset;
		contactOffset = desc.contactOffset;
		slopeLimitRadians = desc.slopeLimitRadians;
		filter.layerBits = desc.layerBits;
		filter.collideMask = desc.collideMask;
		filter.queryMask = desc.queryMask;
		filter.isTrigger = false;

		if (actor)
		{
			actor->userData = desc.userData;
			// Ensure kinematic (should already be)
			actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
		}
	}

	~PhysXCharacterController() override
	{
		auto s = world.lock();
		if (s && controller)
			s->EnqueueControllerRelease(controller);
		controller = nullptr;
		actor = nullptr;
	}

	bool IsValid() const override { return controller != nullptr; }

	void Destroy() override
	{
		if (!controller) return;
		auto s = world.lock();
		if (s)
			s->EnqueueControllerRelease(controller);
		controller = nullptr;
		actor = nullptr;
	}

	void SetUserData(void* ptr) override
	{
		if (actor) actor->userData = ptr;
		else userDataFallback = ptr;
	}

	void* GetUserData() const override
	{
		return actor ? actor->userData : userDataFallback;
	}

	void SetLayerMasks(uint32_t layerBits, uint32_t collideMask, uint32_t queryMask) override
	{
		filter.layerBits = layerBits;
		filter.collideMask = collideMask;
		filter.queryMask = queryMask;
		if (!actor) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);

		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
			if (sh) ApplyFilterToShape(*sh, filter);
	}

	void SetPosition(const Vec3& centerPos) override
	{
		if (!controller) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		controller->setPosition(ToPxExt(centerPos));
	}

	Vec3 GetPosition() const override
	{
		if (!controller) return Vec3::Zero;
		auto s = world.lock();
		if (!s || !s->scene) return Vec3::Zero;
		SceneReadLock rl(s->scene, s->enableSceneLocks);
		return FromPxExt(controller->getPosition());
	}

	void SetFootPosition(const Vec3& footPos) override
	{
		const float footToCenter = (type == CCTType::Capsule) ? (halfHeight + radius) : halfHeight;
		SetPosition(footPos + up * footToCenter);
	}

	Vec3 GetFootPosition() const override
	{
		const float footToCenter = (type == CCTType::Capsule) ? (halfHeight + radius) : halfHeight;
		return GetPosition() - up * footToCenter;
	}

	CCTCollisionFlags Move(
		const Vec3& displacement,
		float dt,
		uint32_t layerMask,
		uint32_t queryMask,
		bool hitTriggers,
		float minDistance) override
	{
		if (!controller) return CCTCollisionFlags::None;
		auto s = world.lock();
		if (!s || !s->scene) return CCTCollisionFlags::None;

		MaskQueryCallbackIgnoreActor cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block, actor);
		PxFilterData fd{}; // unused by our callback but required by PhysX API
		PxControllerFilters filters;
		filters.mFilterData = &fd;
		filters.mFilterCallback = &cb;
		filters.mCCTFilterCallback = nullptr;

		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		PxControllerCollisionFlags cf = controller->move(ToPx(displacement), minDistance, dt, filters);
		lastCollisionFlags = FromPx(cf);
		return lastCollisionFlags;
	}

	CharacterControllerState GetState(
		uint32_t groundLayerMask,
		uint32_t groundQueryMask,
		float groundProbeDistance,
		bool hitTriggers) const override
	{
		CharacterControllerState out{};
		out.collisionFlags = lastCollisionFlags;
		out.onGround = (lastCollisionFlags & CCTCollisionFlags::Down) != CCTCollisionFlags::None;

		if (!controller) return out;
		auto s = world.lock();
		if (!s || !s->scene) return out;

		SceneReadLock rl(s->scene, s->enableSceneLocks);
		const Vec3 center = FromPxExt(controller->getPosition());
		const float footToCenter = (type == CCTType::Capsule) ? (halfHeight + radius) : halfHeight;
		const Vec3 foot = center - up * footToCenter;
		const Vec3 origin = foot + up * max(0.0f, groundProbeDistance);
		const Vec3 dir = -up;
		const float maxDist = max(0.0f, groundProbeDistance) + max(0.01f, contactOffset) + 0.01f;

		PxRaycastBuffer buf;
		PxQueryFilterData qfd;
		qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
		MaskQueryCallbackIgnoreActor cb(groundLayerMask, groundQueryMask, hitTriggers, QueryHitMode::Block, actor);

		const PxHitFlags hitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL;
		const bool hit = s->scene->raycast(ToPx(origin), ToPx(dir), maxDist, buf, hitFlags, qfd, &cb);
		if (hit && buf.hasBlock)
		{
			out.groundNormal = FromPx(buf.block.normal);
			// Distance from foot to hit point along -up.
			out.groundDistance = max(0.0f, buf.block.distance - max(0.0f, groundProbeDistance));
			out.onGround = out.onGround || (out.groundDistance <= max(0.05f, contactOffset + 0.02f));
		}
		return out;
	}

	void SetStepOffset(float v) override
	{
		stepOffset = v;
		if (!controller) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		controller->setStepOffset(v);
	}
	float GetStepOffset() const override { return stepOffset; }

	void SetSlopeLimit(float inSlopeLimitRadians) override
	{
		slopeLimitRadians = inSlopeLimitRadians;
		if (!controller) return;
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		const float c = std::cos(max(0.0f, min(inSlopeLimitRadians, 1.56079633f))); // clamp to < 89.4deg
		controller->setSlopeLimit(c);
	}
	float GetSlopeLimit() const override { return slopeLimitRadians; }

	void Resize(float inHalfHeight) override
	{
		if (!controller) return;
		if (type != CCTType::Capsule) return;
		PxCapsuleController* cap = static_cast<PxCapsuleController*>(controller);
		halfHeight = max(0.01f, inHalfHeight);
		auto s = world.lock();
		if (!s || !s->scene) return;
		SceneWriteLock wl(s->scene, s->enableSceneLocks);
		cap->setHeight(halfHeight * 2.0f);
	}

	void* GetNativeController() const override { return controller; }
	void* GetNativeActor() const override { return actor; }

private:
	PxController* controller = nullptr;
	PxRigidDynamic* actor = nullptr;
	std::weak_ptr<PhysXWorld::Impl> world;
	CCTType type = CCTType::Capsule;

	FilterDesc filter{};
	Vec3 up = Vec3::UnitY;
	float radius = 0.5f;
	float halfHeight = 0.5f;
	float stepOffset = 0.3f;
	float contactOffset = 0.1f;
	float slopeLimitRadians = 0.785398163f;
	mutable void* userDataFallback = nullptr;

	mutable CCTCollisionFlags lastCollisionFlags = CCTCollisionFlags::None;
};

#endif // PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS


bool PhysXWorld::SupportsCharacterControllers() const
{
#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
	return impl && impl->controllerMgr != nullptr;
#else
	return false;
#endif
}

std::unique_ptr<ICharacterController> PhysXWorld::CreateCharacterController(const CharacterControllerDesc& desc)
{
#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
	if (!impl || !impl->scene || !impl->controllerMgr) return {};

	// Minimal sanity checks (PhysX will also validate)
	if (desc.type == CCTType::Capsule)
	{
		if (desc.radius <= 0.0f || desc.halfHeight <= 0.0f) return {};
	}
	else
	{
		// PhysX box controller uses 3 half-extents
		if (desc.halfExtents.x <= 0.0f || desc.halfExtents.y <= 0.0f || desc.halfExtents.z <= 0.0f) return {};
	}

	Vec3 up = desc.upDirection;
	if (!physwrap::NormalizeSafe(up)) up = Vec3::UnitY;

	PxMaterial* mat = impl->GetOrCreateMaterial(desc);
	if (!mat) return {};

	// Convert slope angle -> cosine (PhysX expects cosine).
	const float clampedSlope = max(0.0f, min(desc.slopeLimitRadians, 1.56079633f));
	const float slopeCos = std::cos(clampedSlope);

	PxController* controller = nullptr;
	{
		SceneWriteLock wl(impl->scene, impl->enableSceneLocks);
		if (desc.type == CCTType::Capsule)
		{
			PxCapsuleControllerDesc cd;
			cd.radius = desc.radius;
			cd.height = desc.halfHeight * 2.0f; // cylinder height
			cd.climbingMode = ToPx(desc.climbingMode);

			cd.material = mat;
			cd.upDirection = ToPx(up);
			cd.stepOffset = max(0.0f, desc.stepOffset);
			cd.contactOffset = max(0.001f, desc.contactOffset);
			cd.slopeLimit = slopeCos;
			cd.nonWalkableMode = ToPx(desc.nonWalkableMode);
			cd.density = max(0.0f, desc.density);

			const float footToCenter = desc.halfHeight + desc.radius;
			const Vec3 center = desc.footPosition + up * footToCenter;
			cd.position = ToPxExt(center);

			if (!cd.isValid()) return {};
			controller = impl->controllerMgr->createController(cd);
		}
		else
		{
			PxBoxControllerDesc cd;
			cd.halfHeight = desc.halfExtents.y;
			cd.halfSideExtent = desc.halfExtents.x;
			cd.halfForwardExtent = desc.halfExtents.z;

			cd.material = mat;
			cd.upDirection = ToPx(up);
			cd.stepOffset = max(0.0f, desc.stepOffset);
			cd.contactOffset = max(0.001f, desc.contactOffset);
			cd.slopeLimit = slopeCos;
			cd.nonWalkableMode = ToPx(desc.nonWalkableMode);
			cd.density = max(0.0f, desc.density);

			const float footToCenter = desc.halfExtents.y;
			const Vec3 center = desc.footPosition + up * footToCenter;
			cd.position = ToPxExt(center);

			if (!cd.isValid()) return {};
			controller = impl->controllerMgr->createController(cd);
		}
	}

	if (!controller) return {};

	PxRigidDynamic* actor = controller->getActor();
	if (actor)
	{
		SceneWriteLock wl(impl->scene, impl->enableSceneLocks);
		actor->userData = desc.userData;
		actor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
		actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);

		// Apply filters to the underlying kinematic actor shapes.
		FilterDesc f = desc;
		f.isTrigger = false;
		const PxU32 n = actor->getNbShapes();
		std::vector<PxShape*> shapes(n);
		actor->getShapes(shapes.data(), n);
		for (PxShape* sh : shapes)
		{
			if (!sh) continue;
			ApplyFilterToShape(*sh, f);
			sh->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, desc.enableQueries);
		}
	}

	return std::make_unique<PhysXCharacterController>(controller, actor, desc, impl);
#else
	(void)desc;
	return {};
#endif
}

