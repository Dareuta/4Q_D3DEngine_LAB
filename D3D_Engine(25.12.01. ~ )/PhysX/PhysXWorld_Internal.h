#pragma once
// PhysXWorld.cpp
#include "PhysXWorld.h"
#include "PhysicsMath.h"

#include <PxPhysicsAPI.h>

// ------------------------------------------------------------
// PhysX Character Controller (CCT) header detection
// ------------------------------------------------------------
#ifndef PHYSXWRAP_ENABLE_CCT
#define PHYSXWRAP_ENABLE_CCT 1
#endif

#ifndef PHYSXWRAP_HAS_CCT_HEADERS
#if PHYSXWRAP_ENABLE_CCT && defined(__has_include)
#  if __has_include(<characterkinematic/PxController.h>) && __has_include(<characterkinematic/PxControllerManager.h>)
#    define PHYSXWRAP_HAS_CCT_HEADERS 1
#    define PHYSXWRAP_CCT_INCLUDE_STYLE 1
#  elif __has_include(<physx/characterkinematic/PxController.h>) && __has_include(<physx/characterkinematic/PxControllerManager.h>)
#    define PHYSXWRAP_HAS_CCT_HEADERS 1
#    define PHYSXWRAP_CCT_INCLUDE_STYLE 2
#  else
#    define PHYSXWRAP_HAS_CCT_HEADERS 0
#    define PHYSXWRAP_CCT_INCLUDE_STYLE 0
#  endif
#elif PHYSXWRAP_ENABLE_CCT
#  define PHYSXWRAP_HAS_CCT_HEADERS 0
#  define PHYSXWRAP_CCT_INCLUDE_STYLE 0
#else
#  define PHYSXWRAP_HAS_CCT_HEADERS 0
#  define PHYSXWRAP_CCT_INCLUDE_STYLE 0
#endif
#endif

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
#  if PHYSXWRAP_CCT_INCLUDE_STYLE == 1
#    include <characterkinematic/PxController.h>
#    include <characterkinematic/PxControllerManager.h>
#    include <characterkinematic/PxCapsuleController.h>
#    include <characterkinematic/PxBoxController.h>
#  elif PHYSXWRAP_CCT_INCLUDE_STYLE == 2
#    include <physx/characterkinematic/PxController.h>
#    include <physx/characterkinematic/PxControllerManager.h>
#    include <physx/characterkinematic/PxCapsuleController.h>
#    include <physx/characterkinematic/PxBoxController.h>
#  endif
#endif

// Mesh cooking (PhysX 5.2+): immediate cooking entry points.
#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
#  if PHYSXWRAP_COOKING_INCLUDE_STYLE == 1
#    include <cooking/PxCooking.h>
#    include <cooking/PxTriangleMeshDesc.h>
#    include <cooking/PxConvexMeshDesc.h>
#  elif PHYSXWRAP_COOKING_INCLUDE_STYLE == 2
#    include <physx/cooking/PxCooking.h>
#    include <physx/cooking/PxTriangleMeshDesc.h>
#    include <physx/cooking/PxConvexMeshDesc.h>
#  endif
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace physx;

// ============================================================
//  Helpers
// ============================================================

static inline PxVec3 ToPx(const Vec3& v) { return PxVec3(v.x, v.y, v.z); }
static inline Vec3   FromPx(const PxVec3& v) { return Vec3(v.x, v.y, v.z); }

static inline PxExtendedVec3 ToPxExt(const Vec3& v)
{
	return PxExtendedVec3(static_cast<PxExtended>(v.x), static_cast<PxExtended>(v.y), static_cast<PxExtended>(v.z));
}

static inline Vec3 FromPxExt(const PxExtendedVec3& v)
{
	return Vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

static inline PxQuat ToPx(const Quat& q)
{
	Quat nq = q;
	nq.Normalize();
	return PxQuat(nq.x, nq.y, nq.z, nq.w);
}

static inline Quat FromPx(const PxQuat& q) { return Quat(q.x, q.y, q.z, q.w); }

static inline PxTransform ToPxTransform(const Vec3& p, const Quat& q)
{
	return PxTransform(ToPx(p), ToPx(q));
}

// Capsule axis alignment: PhysX capsule axis is +X. Rotate +X -> +Y.
static inline PxQuat CapsuleAlignQuatPx()
{
	return PxQuat(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f)); // 90deg about Z
}

// Bit-flag helpers (avoid ambiguous conversions from PxFlags)
static inline bool HasPairFlag(PxPairFlags flags, PxPairFlag::Enum bit)
{
	return (static_cast<PxU32>(flags) & static_cast<PxU32>(bit)) != 0u;
}

static inline bool HasShapeFlag(PxShapeFlags flags, PxShapeFlag::Enum bit)
{
	return (static_cast<PxU32>(flags) & static_cast<PxU32>(bit)) != 0u;
}

static inline bool HasHitFlag(PxHitFlags flags, PxHitFlag::Enum bit)
{
	return (static_cast<PxU32>(flags) & static_cast<PxU32>(bit)) != 0u;
}

static inline bool HasRigidBodyFlag(PxRigidBodyFlags flags, PxRigidBodyFlag::Enum bit)
{
	return (static_cast<PxU32>(flags) & static_cast<PxU32>(bit)) != 0u;
}

static inline bool HasActorFlag(PxActorFlags flags, PxActorFlag::Enum bit)
{
	return (static_cast<PxU32>(flags) & static_cast<PxU32>(bit)) != 0u;
}

static inline bool HasTriggerPairFlag(PxTriggerPairFlags flags, PxTriggerPairFlag::Enum bit)
{
	return (static_cast<PxU32>(flags) & static_cast<PxU32>(bit)) != 0u;
}


// Pointer pair key (commutative)
static inline uint64_t PtrPairKey(const void* a, const void* b)
{
	uintptr_t pa = reinterpret_cast<uintptr_t>(a);
	uintptr_t pb = reinterpret_cast<uintptr_t>(b);
	if (pa > pb) std::swap(pa, pb);
	// mix (splitmix-ish)
	uint64_t x = (uint64_t)pa;
	uint64_t y = (uint64_t)pb;
	x ^= y + 0x9e3779b97f4a7c15ull + (x << 6) + (x >> 2);
	return x;
}

// ============================================================
//  Mesh hashing (cache keys)
// ============================================================

static inline uint32_t FloatBits(float v)
{
	uint32_t u = 0;
	std::memcpy(&u, &v, sizeof(uint32_t));
	return u;
}

static inline uint64_t HashFNV1a64(uint64_t h, const void* data, size_t len)
{
	constexpr uint64_t prime = 1099511628211ull;
	const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
	for (size_t i = 0; i < len; ++i)
	{
		h ^= uint64_t(p[i]);
		h *= prime;
	}
	return h;
}

static inline uint64_t HashU32(uint64_t h, uint32_t v) { return HashFNV1a64(h, &v, sizeof(v)); }

static inline uint64_t HashVec3(uint64_t h, const Vec3& v)
{
	h = HashU32(h, FloatBits(v.x));
	h = HashU32(h, FloatBits(v.y));
	h = HashU32(h, FloatBits(v.z));
	return h;
}

static inline uint64_t HashVertices(uint64_t h, const Vec3* v, uint32_t count)
{
	for (uint32_t i = 0; i < count; ++i) h = HashVec3(h, v[i]);
	return h;
}

// ============================================================
//  Filter shader data (per-scene)
// ============================================================

struct FilterShaderData
{
	PxU32 enableContactEvents = 1;
	PxU32 enableContactPoints = 0;
	PxU32 enableContactModify = 0;
};

static PxFilterFlags LayerFilterShader(
	PxFilterObjectAttributes attributes0, PxFilterData filterData0,
	PxFilterObjectAttributes attributes1, PxFilterData filterData1,
	PxPairFlags& pairFlags,
	const void* constantBlock, PxU32 constantBlockSize)
{
	// filterData.word0 : layerBits
	// filterData.word1 : collideMask

	const bool allow01 = (filterData0.word1 & filterData1.word0) != 0;
	const bool allow10 = (filterData1.word1 & filterData0.word0) != 0;
	if (!allow01 || !allow10)
		return PxFilterFlag::eSUPPRESS;

	const FilterShaderData* fsd = nullptr;
	if (constantBlock && constantBlockSize >= sizeof(FilterShaderData))
		fsd = reinterpret_cast<const FilterShaderData*>(constantBlock);

	// Triggers vs contacts
	if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
	{
		pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
		return PxFilterFlag::eDEFAULT;
	}

	pairFlags = PxPairFlag::eCONTACT_DEFAULT;

	if (fsd && fsd->enableContactEvents)
	{
		pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
		pairFlags |= PxPairFlag::eNOTIFY_TOUCH_LOST;

		if (fsd->enableContactPoints) {
			pairFlags |= PxPairFlag::eNOTIFY_CONTACT_POINTS;
		}
	}

	if (fsd && fsd->enableContactModify)
	{
		pairFlags |= PxPairFlag::eMODIFY_CONTACTS;
	}

	return PxFilterFlag::eDEFAULT;
}

// ============================================================
//  Scene locks (optional)
// ============================================================

struct SceneReadLock
{
	PxScene* scene = nullptr;
	bool enabled = false;
	SceneReadLock(PxScene* s, bool e) : scene(s), enabled(e) { if (enabled && scene) scene->lockRead(); }
	~SceneReadLock() { if (enabled && scene) scene->unlockRead(); }
};

struct SceneWriteLock
{
	PxScene* scene = nullptr;
	bool enabled = false;
	SceneWriteLock(PxScene* s, bool e) : scene(s), enabled(e) { if (enabled && scene) scene->lockWrite(); }
	~SceneWriteLock() { if (enabled && scene) scene->unlockWrite(); }
};

// ============================================================
//  Query callback (layerMask + queryMask + trigger filtering)
// ============================================================

enum class QueryHitMode : uint8_t { Block, Touch };

class MaskQueryCallback final : public PxQueryFilterCallback
{
public:
	MaskQueryCallback(uint32_t layerMaskBits, uint32_t queryMaskBits, bool hitTriggers, QueryHitMode mode)
		: layerMask(layerMaskBits), queryMask(queryMaskBits), includeTriggers(hitTriggers), hitMode(mode)
	{
	}

	PxQueryHitType::Enum preFilter(
		const PxFilterData& /*filterData*/, const PxShape* shape,
		const PxRigidActor* /*actor*/, PxHitFlags& /*queryFlags*/) override
	{
		if (!shape) return PxQueryHitType::eNONE;

		const PxShapeFlags sf = shape->getFlags();

		// Must be query shape
		if (!HasShapeFlag(sf, PxShapeFlag::eSCENE_QUERY_SHAPE))
			return PxQueryHitType::eNONE;

		// Trigger handling
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
};

// ============================================================
//  PhysXWorld::Impl
// ============================================================

struct PhysXWorld::Impl : public std::enable_shared_from_this<PhysXWorld::Impl>
{
	explicit Impl(PhysXContext& inCtx, const PhysXWorld::Desc& desc)
		: ctx(&inCtx)
	{
		physics = ctx->GetPhysics();
		if (!physics) throw std::runtime_error("PhysXContext has no PxPhysics");

		enableSceneLocks = desc.enableSceneLocks;
		enableActiveTransforms = desc.enableActiveTransforms;

		// Default material (used for planes and as a fallback)
		defaultMaterial = physics->createMaterial(0.5f, 0.5f, 0.0f);
		if (!defaultMaterial) throw std::runtime_error("createMaterial failed");

		// Scene
		PxSceneDesc sdesc(physics->getTolerancesScale());
		sdesc.gravity = ToPx(desc.gravity);
		sdesc.cpuDispatcher = ctx->GetDispatcher();
		sdesc.filterShader = LayerFilterShader;

		shaderData.enableContactEvents = desc.enableContactEvents ? 1u : 0u;
		shaderData.enableContactPoints = desc.enableContactPoints ? 1u : 0u;
		shaderData.enableContactModify = desc.enableContactModify ? 1u : 0u;
		sdesc.filterShaderData = &shaderData;
		sdesc.filterShaderDataSize = sizeof(FilterShaderData);

		sdesc.simulationEventCallback = &eventCb;
		sdesc.contactModifyCallback = desc.enableContactModify ? &eventCb : nullptr;

		// CCD must be enabled on the scene for swept CCD on rigid bodies.
		if (desc.enableCCD)
			sdesc.flags |= PxSceneFlag::eENABLE_CCD;

		// Use PCM by default (better contact generation in most cases)
		sdesc.flags |= PxSceneFlag::eENABLE_PCM;

		scene = physics->createScene(sdesc);
		if (!scene) throw std::runtime_error("createScene failed");

		// NOTE: enable_shared_from_this isn't active inside the raw constructor.
		// PhysXWorld repairs eventCb.owner after Impl is owned by a shared_ptr.
		eventCb.owner.reset();

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
		// Character Controller Manager (CCT)
		controllerMgr = PxCreateControllerManager(*scene);
#endif
	}

	~Impl()
	{
		// Ensure callback doesn't access freed Impl
		eventCb.owner.reset();

		FlushPending(true);

		if (defaultMaterial) defaultMaterial->release();
		defaultMaterial = nullptr;

		// Release controllers/manager before scene teardown.
#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
		if (controllerMgr)
		{
			// Best-effort: controller manager should have no live controllers at this point.
			controllerMgr->release();
			controllerMgr = nullptr;
		}
#endif

		if (scene) scene->release();
		scene = nullptr;

		// Release cached materials/meshes
		ClearMeshCachesInternal();
	}

	// ------------------------------------------------------------
	// Simulation callback
	// ------------------------------------------------------------
	struct EventCallback final : public PxSimulationEventCallback, physx::PxContactModifyCallback
	{
		std::weak_ptr<Impl> owner;

		void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
		void onWake(PxActor**, PxU32) override {}
		void onSleep(PxActor**, PxU32) override {}
		void onAdvance(const PxRigidBody* const* bodyBuffer, const PxTransform* poseBuffer, const PxU32 count) override
		{
			auto s = owner.lock();
			if (!s || !s->enableActiveTransforms) return;
			if (!bodyBuffer || !poseBuffer || count == 0) return;

			std::scoped_lock lock(s->activeMtx);
			s->activeTransforms.reserve(s->activeTransforms.size() + count);

			for (PxU32 i = 0; i < count; ++i)
			{
				const PxRigidBody* rb = bodyBuffer[i];
				if (!rb) continue;

				ActiveTransform at{};
				at.nativeActor = const_cast<PxRigidBody*>(rb);
				at.userData = rb->userData;
				at.position = FromPx(poseBuffer[i].p);
				at.rotation = FromPx(poseBuffer[i].q);

				s->activeTransforms.emplace_back(at);
			}
		}

		void onTrigger(PxTriggerPair* pairs, PxU32 count) override
		{
			auto s = owner.lock();
			if (!s || !pairs) return;

			std::scoped_lock lock(s->eventMtx, s->contactStateMtx);

			for (PxU32 i = 0; i < count; ++i)
			{
				const PxTriggerPair& tp = pairs[i];

				if (HasTriggerPairFlag(tp.flags, PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER) ||
					HasTriggerPairFlag(tp.flags, PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
					continue;

				const PxShape* shA = tp.triggerShape;
				const PxShape* shB = tp.otherShape;
				const PxActor* acA = tp.triggerActor;
				const PxActor* acB = tp.otherActor;

				if (!shA || !shB || !acA || !acB)
					continue;

				const uint64_t shapeKey = PtrPairKey(shA, shB);
				const uint64_t actorKey = PtrPairKey(acA, acB);

				const bool begin = HasPairFlag(tp.status, PxPairFlag::eNOTIFY_TOUCH_FOUND);
				const bool end = HasPairFlag(tp.status, PxPairFlag::eNOTIFY_TOUCH_LOST);

				if (begin)
				{
					if (s->activeTriggerShapePairs.insert(shapeKey).second)
					{
						uint32_t& cnt = s->activeTriggerActorCounts[actorKey];
						++cnt;
						if (cnt == 1)
						{
							PhysicsEvent e;
							e.type = PhysicsEventType::TriggerEnter;
							e.nativeActorA = const_cast<PxActor*>(acA);
							e.nativeActorB = const_cast<PxActor*>(acB);
							e.nativeShapeA = const_cast<PxShape*>(shA);
							e.nativeShapeB = const_cast<PxShape*>(shB);
							e.userDataA = const_cast<void*>(acA->userData);
							e.userDataB = const_cast<void*>(acB->userData);
							s->events.push_back(e);
						}
					}
				}

				if (end)
				{
					if (s->activeTriggerShapePairs.erase(shapeKey) > 0)
					{
						auto it = s->activeTriggerActorCounts.find(actorKey);
						if (it != s->activeTriggerActorCounts.end())
						{
							if (it->second > 0) --it->second;
							if (it->second == 0)
							{
								s->activeTriggerActorCounts.erase(it);

								PhysicsEvent e;
								e.type = PhysicsEventType::TriggerExit;
								e.nativeActorA = const_cast<PxActor*>(acA);
								e.nativeActorB = const_cast<PxActor*>(acB);
								e.nativeShapeA = const_cast<PxShape*>(shA);
								e.nativeShapeB = const_cast<PxShape*>(shB);
								e.userDataA = const_cast<void*>(acA->userData);
								e.userDataB = const_cast<void*>(acB->userData);
								s->events.push_back(e);
							}
						}
					}
				}
			}
		}

		void onContact(const PxContactPairHeader& header, const PxContactPair* pairs, PxU32 count) override
		{
			auto s = owner.lock();
			if (!s || !pairs || count == 0) return;

			const PxActor* ac0 = header.actors[0];
			const PxActor* ac1 = header.actors[1];
			if (!ac0 || !ac1) return;

			std::scoped_lock lock(s->eventMtx, s->contactStateMtx);

			const uint64_t actorKey = PtrPairKey(ac0, ac1);

			for (PxU32 i = 0; i < count; ++i)
			{
				const PxContactPair& cp = pairs[i];

				// if shape removed, ignore
				if ((static_cast<PxU32>(cp.flags) &
					(static_cast<PxU32>(PxContactPairFlag::eREMOVED_SHAPE_0) |
						static_cast<PxU32>(PxContactPairFlag::eREMOVED_SHAPE_1))) != 0u)
					continue;

				const PxShape* sh0 = cp.shapes[0];
				const PxShape* sh1 = cp.shapes[1];
				if (!sh0 || !sh1) continue;

				const uint64_t shapeKey = PtrPairKey(sh0, sh1);

				const bool begin = HasPairFlag(cp.events, PxPairFlag::eNOTIFY_TOUCH_FOUND);
				const bool end = HasPairFlag(cp.events, PxPairFlag::eNOTIFY_TOUCH_LOST);

				if (begin)
				{
					if (s->activeContactShapePairs.insert(shapeKey).second)
					{
						uint32_t& cnt = s->activeContactActorCounts[actorKey];
						++cnt;

						if (cnt == 1)
						{
							PhysicsEvent e;
							e.type = PhysicsEventType::ContactBegin;
							e.nativeActorA = const_cast<PxActor*>(ac0);
							e.nativeActorB = const_cast<PxActor*>(ac1);
							e.nativeShapeA = const_cast<PxShape*>(sh0);
							e.nativeShapeB = const_cast<PxShape*>(sh1);
							e.userDataA = const_cast<void*>(ac0->userData);
							e.userDataB = const_cast<void*>(ac1->userData);

							if (s->shaderData.enableContactPoints && HasPairFlag(cp.events, PxPairFlag::eNOTIFY_CONTACT_POINTS))
							{
								PxContactPairPoint pts[1];
								PxU32 n = cp.extractContacts(pts, 1);
								if (n > 0)
								{
									e.position = FromPx(pts[0].position);
									e.normal = FromPx(pts[0].normal);
								}
							}

							s->events.push_back(e);
						}
					}
				}

				if (end)
				{
					if (s->activeContactShapePairs.erase(shapeKey) > 0)
					{
						auto it = s->activeContactActorCounts.find(actorKey);
						if (it != s->activeContactActorCounts.end())
						{
							if (it->second > 0) --it->second;
							if (it->second == 0)
							{
								s->activeContactActorCounts.erase(it);

								PhysicsEvent e;
								e.type = PhysicsEventType::ContactEnd;
								e.nativeActorA = const_cast<PxActor*>(ac0);
								e.nativeActorB = const_cast<PxActor*>(ac1);
								e.nativeShapeA = const_cast<PxShape*>(sh0);
								e.nativeShapeB = const_cast<PxShape*>(sh1);
								e.userDataA = const_cast<void*>(ac0->userData);
								e.userDataB = const_cast<void*>(ac1->userData);
								s->events.push_back(e);
							}
						}
					}
				}
			}
		}

		void onContactModify(PxContactModifyPair* const pairs, PxU32 count) override
		{
			auto s = owner.lock();
			if (!s || !pairs || count == 0) return;

			// Fast path: no user callback registered.
			ContactModifyCallback cb = nullptr;
			void* user = nullptr;
			{
				std::scoped_lock lock(s->contactModifyMtx);
				cb = s->contactModifyCb;
				user = s->contactModifyUser;
			}
			if (!cb) return;

			// IMPORTANT: PhysX calls this on the simulation thread.
			// Keep work minimal and avoid locking other engine systems.
			for (PxU32 i = 0; i < count; ++i)
			{
				PxContactModifyPair& mp = pairs[i];
				// In PhysX 5.x these are exposed as const pointers.
				const PxRigidActor* a = mp.actor[0];
				const PxRigidActor* b = mp.actor[1];
				const PxShape* shA = mp.shape[0];
				const PxShape* shB = mp.shape[1];
				if (!a || !b || !shA || !shB) continue;

				ContactModifyPair pair;
				pair.userDataA = a->userData;
				pair.userDataB = b->userData;
				// Store as opaque handles. We intentionally erase const here because the public
				// interface exposes native pointers as void*.
				pair.nativeActorA = const_cast<PxRigidActor*>(a);
				pair.nativeActorB = const_cast<PxRigidActor*>(b);
				pair.nativeShapeA = const_cast<PxShape*>(shA);
				pair.nativeShapeB = const_cast<PxShape*>(shB);

				PxContactSet& cs = mp.contacts;
				const PxU32 n = cs.size();
				pair.contacts.resize(n);
				for (PxU32 c = 0; c < n; ++c)
				{
					ContactModifyPoint& dst = pair.contacts[c];
					dst.position = FromPx(cs.getPoint(c));
					dst.normal = FromPx(cs.getNormal(c));
					dst.separation = cs.getSeparation(c);
					dst.targetVelocity = FromPx(cs.getTargetVelocity(c));
					dst.maxImpulse = cs.getMaxImpulse(c);
				}

				cb(pair, user);

				if (pair.ignorePair)
				{
					for (PxU32 c = 0; c < n; ++c)
						cs.ignore(c);
					continue;
				}

				// Apply modifications back to PhysX contact set.
				const PxU32 m = static_cast<PxU32>(std::min<size_t>(n, pair.contacts.size()));
				for (PxU32 c = 0; c < m; ++c)
				{
					const ContactModifyPoint& src = pair.contacts[c];
					if (src.ignore)
					{
						cs.ignore(c);
						continue;
					}
					cs.setPoint(c, ToPx(src.position));
					cs.setNormal(c, ToPx(src.normal));
					cs.setSeparation(c, src.separation);
					cs.setTargetVelocity(c, ToPx(src.targetVelocity));
					if (src.maxImpulse >= 0.0f)
						cs.setMaxImpulse(c, src.maxImpulse);
				}
			}
		}
	};

	// ------------------------------------------------------------
	// Public-ish state
	// ------------------------------------------------------------

	PhysXContext* ctx = nullptr;
	PxPhysics* physics = nullptr;
	PxScene* scene = nullptr;
	PxMaterial* defaultMaterial = nullptr;

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
	PxControllerManager* controllerMgr = nullptr;
#endif

	bool enableSceneLocks = true;


	bool enableActiveTransforms = true;

	std::mutex activeMtx;
	std::vector<ActiveTransform> activeTransforms;

	FilterShaderData shaderData{};
	EventCallback eventCb{};

	// Events
	std::mutex eventMtx;
	std::vector<PhysicsEvent> events;

	// Optional contact modify callback (registered from game thread)
	std::mutex contactModifyMtx;
	ContactModifyCallback contactModifyCb = nullptr;
	void* contactModifyUser = nullptr;

	// Contact/trigger dedup state
	std::mutex contactStateMtx;
	std::unordered_set<uint64_t> activeContactShapePairs;
	std::unordered_map<uint64_t, uint32_t> activeContactActorCounts;
	std::unordered_set<uint64_t> activeTriggerShapePairs;
	std::unordered_map<uint64_t, uint32_t> activeTriggerActorCounts;

	// Pending adds/releases (for safety around simulate/fetch)
	std::mutex pendingMtx;
	enum class ActorOpType : uint8_t { Add, Remove };
	struct ActorOp { PxActor* actor = nullptr; ActorOpType type = ActorOpType::Add; };
	std::vector<ActorOp> pendingActorOps;
	std::vector<PxBase*> pendingRelease; // actors, joints, meshes, etc.

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
	std::vector<PxController*> pendingControllerRelease;
#endif

	// Caches
	std::mutex materialCacheMtx;
	std::unordered_map<uint64_t, PxMaterial*> materialCache;

#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	std::mutex meshCacheMtx;
	std::unordered_map<uint64_t, PxTriangleMesh*> triMeshCache;
	std::unordered_map<uint64_t, PxConvexMesh*> convexMeshCache;
#endif

	// ------------------------------------------------------------
	// Pending maintenance
	// ------------------------------------------------------------

	void EnqueueAdd(PxActor* a)
	{
		if (!a) return;
		std::scoped_lock lock(pendingMtx);
		pendingActorOps.push_back({ a, ActorOpType::Add });
	}

	void EnqueueRemove(PxActor* a)
	{
		if (!a) return;
		std::scoped_lock lock(pendingMtx);
		pendingActorOps.push_back({ a, ActorOpType::Remove });
	}

	void EnqueueRelease(PxBase* b)
	{
		if (!b) return;
		std::scoped_lock lock(pendingMtx);
		pendingRelease.push_back(b);
	}

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
	void EnqueueControllerRelease(PxController* c)
	{
		if (!c) return;
		std::scoped_lock lock(pendingMtx);
		pendingControllerRelease.push_back(c);
	}
#endif

	void FlushPending(bool allowImmediateRelease)
	{
		std::vector<ActorOp> actorOps;
		std::vector<PxBase*> rels;

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
		std::vector<PxController*> ctrls;
#endif
		{
			std::scoped_lock lock(pendingMtx);
			actorOps.swap(pendingActorOps);
			rels.swap(pendingRelease);

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
			ctrls.swap(pendingControllerRelease);
#endif
		}

		if (!allowImmediateRelease)
		{
			// Can't touch scene now (e.g. during simulate). Put everything back.
			std::scoped_lock lock(pendingMtx);
			pendingActorOps.insert(pendingActorOps.end(), actorOps.begin(), actorOps.end());
			pendingRelease.insert(pendingRelease.end(), rels.begin(), rels.end());

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
			pendingControllerRelease.insert(pendingControllerRelease.end(), ctrls.begin(), ctrls.end());
#endif
			return;
		}

		if (scene)
		{
			SceneWriteLock wl(scene, enableSceneLocks);
			for (const ActorOp& op : actorOps)
			{
				PxActor* a = op.actor;
				if (!a) continue;
				switch (op.type)
				{
				case ActorOpType::Add:
					if (!a->getScene())
						scene->addActor(*a);
					break;
				case ActorOpType::Remove:
					if (a->getScene() == scene)
						scene->removeActor(*a);
					break;
				}
			}
		}

		// Release after applying scene ops.
		for (PxBase* b : rels)
		{
			if (!b) continue;
			// If this is an actor still in the scene, remove it first.
			if (scene)
			{
				if (PxActor* a = b->is<PxActor>())
				{
					if (a->getScene() == scene)
					{
						SceneWriteLock wl(scene, enableSceneLocks);
						scene->removeActor(*a);
					}
				}
			}
			b->release();
		}

#if PHYSXWRAP_ENABLE_CCT && PHYSXWRAP_HAS_CCT_HEADERS
		// Release controllers last (they own a kinematic actor inside the scene).
		if (!ctrls.empty())
		{
			if (scene)
			{
				SceneWriteLock wl(scene, enableSceneLocks);
				for (PxController* c : ctrls)
					if (c) c->release();
			}
			else
			{
				for (PxController* c : ctrls)
					if (c) c->release();
			}
		}
#endif
	}

	// ------------------------------------------------------------
	// Materials
	// ------------------------------------------------------------

	PxMaterial* GetOrCreateMaterial(const MaterialDesc& m)
	{
		const uint64_t seed = 14695981039346656037ull;
		uint64_t h = seed;
		h = HashU32(h, FloatBits(m.staticFriction));
		h = HashU32(h, FloatBits(m.dynamicFriction));
		h = HashU32(h, FloatBits(m.restitution));

		{
			std::scoped_lock lock(materialCacheMtx);
			auto it = materialCache.find(h);
			if (it != materialCache.end()) return it->second;
		}

		PxMaterial* mat = physics->createMaterial(m.staticFriction, m.dynamicFriction, m.restitution);
		if (!mat) return defaultMaterial;

		{
			std::scoped_lock lock(materialCacheMtx);
			materialCache.emplace(h, mat);
		}

		return mat;
	}

#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	PxTriangleMesh* GetOrCreateTriangleMesh(const TriangleMeshColliderDesc& mesh)
	{
		const PxCookingParams* cook = ctx ? ctx->GetCookingParams() : nullptr;
		if (!cook) return nullptr;

		if (!mesh.vertices || mesh.vertexCount == 0) return nullptr;
		if ((!mesh.indices16 && !mesh.indices32) || mesh.indexCount < 3 || (mesh.indexCount % 3) != 0) return nullptr;

		const uint64_t seed = 14695981039346656037ull;
		uint64_t h = seed;
		h = HashU32(h, mesh.vertexCount);
		h = HashVertices(h, mesh.vertices, mesh.vertexCount);

		h = HashU32(h, mesh.indexCount);
		if (mesh.indices32)
			h = HashFNV1a64(h, mesh.indices32, sizeof(uint32_t) * mesh.indexCount);
		else
			h = HashFNV1a64(h, mesh.indices16, sizeof(uint16_t) * mesh.indexCount);

		h = HashU32(h, mesh.flipNormals ? 1u : 0u);
		h = HashU32(h, mesh.validate ? 1u : 0u);

		{
			std::scoped_lock lock(meshCacheMtx);
			auto it = triMeshCache.find(h);
			if (it != triMeshCache.end()) return it->second;
		}

		PxTriangleMeshDesc desc{};
		desc.points.count = mesh.vertexCount;
		desc.points.stride = sizeof(PxVec3);
		desc.points.data = mesh.vertices;

		if (mesh.indices32)
		{
			desc.triangles.count = mesh.indexCount / 3;
			desc.triangles.stride = 3 * sizeof(uint32_t);
			desc.triangles.data = mesh.indices32;
			// no flag means 32-bit
		}
		else
		{
			desc.flags |= PxMeshFlag::e16_BIT_INDICES;
			desc.triangles.count = mesh.indexCount / 3;
			desc.triangles.stride = 3 * sizeof(uint16_t);
			desc.triangles.data = mesh.indices16;
		}

		if (mesh.flipNormals)
			desc.flags |= PxMeshFlag::eFLIPNORMALS;

		if (mesh.validate)
		{
			const bool ok = PxValidateTriangleMesh(*cook, desc);
			if (!ok) return nullptr;
		}

		PxTriangleMesh* tm = PxCreateTriangleMesh(*cook, desc, physics->getPhysicsInsertionCallback());
		if (!tm) return nullptr;

		{
			std::scoped_lock lock(meshCacheMtx);
			triMeshCache.emplace(h, tm);
		}

		return tm;
	}

	PxConvexMesh* GetOrCreateConvexMesh(const ConvexMeshColliderDesc& mesh)
	{
		const PxCookingParams* cook = ctx ? ctx->GetCookingParams() : nullptr;
		if (!cook) return nullptr;

		if (!mesh.vertices || mesh.vertexCount == 0) return nullptr;

		const uint64_t seed = 14695981039346656037ull;
		uint64_t h = seed;
		h = HashU32(h, mesh.vertexCount);
		h = HashVertices(h, mesh.vertices, mesh.vertexCount);
		h = HashU32(h, mesh.shiftVertices ? 1u : 0u);
		h = HashU32(h, mesh.vertexLimit);
		h = HashU32(h, mesh.validate ? 1u : 0u);

		{
			std::scoped_lock lock(meshCacheMtx);
			auto it = convexMeshCache.find(h);
			if (it != convexMeshCache.end()) return it->second;
		}

		PxConvexMeshDesc desc{};
		desc.points.count = mesh.vertexCount;
		desc.points.stride = sizeof(PxVec3);
		desc.points.data = mesh.vertices;
		desc.flags |= PxConvexFlag::eCOMPUTE_CONVEX;
		if (mesh.shiftVertices)
			desc.flags |= PxConvexFlag::eSHIFT_VERTICES;

		desc.vertexLimit = static_cast<PxU16>(std::min<uint32_t>(mesh.vertexLimit, 255u));

		if (mesh.validate)
		{
			const bool ok = PxValidateConvexMesh(*cook, desc);
			if (!ok) return nullptr;
		}

		PxConvexMesh* cm = PxCreateConvexMesh(*cook, desc, physics->getPhysicsInsertionCallback());
		if (!cm) return nullptr;

		{
			std::scoped_lock lock(meshCacheMtx);
			convexMeshCache.emplace(h, cm);
		}

		return cm;
	}
#endif

	void ClearMeshCachesInternal()
	{
		{
			std::scoped_lock lock(materialCacheMtx);
			for (auto& kv : materialCache)
				if (kv.second) kv.second->release();
			materialCache.clear();
		}

#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
		{
			std::scoped_lock lock(meshCacheMtx);
			for (auto& kv : triMeshCache)
				if (kv.second) kv.second->release();
			triMeshCache.clear();

			for (auto& kv : convexMeshCache)
				if (kv.second) kv.second->release();
			convexMeshCache.clear();
		}
#endif
	}
};

// ============================================================
//  Local helpers for actor/shape setup
// ============================================================

static inline PxFilterData MakeSimFilter(const FilterDesc& f)
{
	PxFilterData d{};
	d.word0 = f.layerBits;
	d.word1 = f.collideMask;
	return d;
}

static inline PxFilterData MakeQueryFilter(const FilterDesc& f)
{
	PxFilterData d{};
	d.word0 = f.layerBits;
	d.word2 = f.queryMask;
	return d;
}

static inline void ApplyFilterToShape(PxShape& shape, const FilterDesc& f)
{
	shape.setSimulationFilterData(MakeSimFilter(f));
	shape.setQueryFilterData(MakeQueryFilter(f));

	// In PhysX 5.x, PxFlags may not expose mutating helpers like set(flag,bool).
	// Prefer the explicit PxShape::setFlag API.
	shape.setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);

	if (f.isTrigger)
	{
		shape.setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
		shape.setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
	}
	else
	{
		shape.setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
		shape.setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
	}
}

static inline PxRigidDynamicLockFlags ToPxLockFlags(RigidBodyLockFlags f)
{
	PxRigidDynamicLockFlags out;
	if ((uint16_t(f) & uint16_t(RigidBodyLockFlags::LockLinearX)) != 0) out |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
	if ((uint16_t(f) & uint16_t(RigidBodyLockFlags::LockLinearY)) != 0) out |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
	if ((uint16_t(f) & uint16_t(RigidBodyLockFlags::LockLinearZ)) != 0) out |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
	if ((uint16_t(f) & uint16_t(RigidBodyLockFlags::LockAngularX)) != 0) out |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
	if ((uint16_t(f) & uint16_t(RigidBodyLockFlags::LockAngularY)) != 0) out |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
	if ((uint16_t(f) & uint16_t(RigidBodyLockFlags::LockAngularZ)) != 0) out |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
	return out;
}

static inline PxForceMode::Enum ToPxForceMode(ForceMode m)
{
	switch (m)
	{
	case ForceMode::Force:          return PxForceMode::eFORCE;
	case ForceMode::Impulse:        return PxForceMode::eIMPULSE;
	case ForceMode::VelocityChange: return PxForceMode::eVELOCITY_CHANGE;
	case ForceMode::Acceleration:   return PxForceMode::eACCELERATION;
	default:                        return PxForceMode::eFORCE;
	}
}

static void ApplyRbDesc(PxRigidDynamic& body, const RigidBodyDesc& rb)
{
	body.userData = rb.userData;

	body.setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !rb.gravityEnabled);

	body.setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, rb.isKinematic);
	body.setLinearDamping(rb.linearDamping);
	body.setAngularDamping(rb.angularDamping);

	if (rb.maxLinearVelocity > 0.0f)  body.setMaxLinearVelocity(rb.maxLinearVelocity);
	if (rb.maxAngularVelocity > 0.0f) body.setMaxAngularVelocity(rb.maxAngularVelocity);

	body.setSolverIterationCounts(
		static_cast<PxU32>(max(1u, rb.solverPositionIterations)),
		static_cast<PxU32>(max(1u, rb.solverVelocityIterations)));

	if (rb.sleepThreshold >= 0.0f) body.setSleepThreshold(rb.sleepThreshold);
	if (rb.stabilizationThreshold >= 0.0f) body.setStabilizationThreshold(rb.stabilizationThreshold);

	body.setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, rb.enableCCD);
	body.setRigidBodyFlag(PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD, rb.enableSpeculativeCCD);

	body.setRigidDynamicLockFlags(ToPxLockFlags(rb.lockFlags));

	if (!rb.startAwake)
		body.putToSleep();
}

static void ApplyMass(PxRigidDynamic& body, const RigidBodyDesc& rb)
{
	if (rb.isKinematic)
		return;

	if (rb.massOverride > 0.0f)
	{
		// This updates inertia based on attached shapes.
		PxRigidBodyExt::setMassAndUpdateInertia(body, rb.massOverride);
	}
	else
	{
		const float density = (rb.density > 0.0f) ? rb.density : 1.0f;
		PxRigidBodyExt::updateMassAndInertia(body, density);
	}
}

// ============================================================
