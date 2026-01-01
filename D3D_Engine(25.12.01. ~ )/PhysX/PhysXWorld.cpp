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

class PhysXJoint final : public IPhysicsJoint
{
public:
	PhysXJoint(PxJoint* j, std::weak_ptr<PhysXWorld::Impl> w)
		: joint(j), world(std::move(w))
	{
	}

	~PhysXJoint() override
	{
		auto s = world.lock();
		if (s && joint)
			s->EnqueueRelease(joint);
		joint = nullptr;
	}

	bool IsValid() const override { return joint != nullptr; }
	void* GetNativeJoint() const override { return joint; }

	void SetBreakForce(float force, float torque) override
	{
		if (!joint) return;
		joint->setBreakForce(force, torque);
	}

	void SetCollideConnected(bool enabled) override
	{
		if (!joint) return;
		joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, enabled);
	}

	void SetUserData(void* ptr) override
	{
		if (!joint) return;
		joint->userData = ptr;
	}

	void* GetUserData() const override
	{
		return joint ? joint->userData : nullptr;
	}

private:
	PxJoint* joint = nullptr;
	std::weak_ptr<PhysXWorld::Impl> world;
};

// ============================================================
//  PhysXWorld
// ============================================================

PhysXWorld::PhysXWorld(PhysXContext& inCtx, const Desc& desc)
	: ctx(inCtx)
{
	// NOTE: Impl uses shared_from_this internally for callback owner.
	// We create shared_ptr with a custom deleter that ensures proper init.
	impl = std::shared_ptr<Impl>(new Impl(inCtx, desc));
	// Repair callback owner now that we have the final shared_ptr.
	impl->eventCb.owner = impl;
}

PhysXWorld::~PhysXWorld() = default;

void PhysXWorld::Flush()
{
	if (!impl) return;
	impl->FlushPending(true);
}

void PhysXWorld::Step(float fixedDt)
{
	if (!impl || !impl->scene) return;

	// Apply pending adds/releases before stepping.
	impl->FlushPending(true);

	// Clear last step's active pose list.
	{
		std::scoped_lock lock(impl->activeMtx);
		impl->activeTransforms.clear();
	}

	{
		SceneWriteLock wl(impl->scene, impl->enableSceneLocks);
		impl->scene->simulate(fixedDt);
	}

	// fetch
	{
		SceneWriteLock wl(impl->scene, impl->enableSceneLocks);
		impl->scene->fetchResults(true);
	}

	// Flush any releases queued during callbacks.
	impl->FlushPending(true);
}

void PhysXWorld::SetGravity(const Vec3& g)
{
	if (!impl || !impl->scene) return;
	SceneWriteLock wl(impl->scene, impl->enableSceneLocks);
	impl->scene->setGravity(ToPx(g));
}

Vec3 PhysXWorld::GetGravity() const
{
	if (!impl || !impl->scene) return Vec3(0, -9.81f, 0);
	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	return FromPx(impl->scene->getGravity());
}

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

bool PhysXWorld::SupportsMeshCooking() const
{
#if PHYSXWRAP_ENABLE_COOKING && PHYSXWRAP_HAS_COOKING_HEADERS
	return ctx.IsCookingAvailable();
#else
	return false;
#endif
}

void PhysXWorld::ClearMeshCaches()
{
	if (!impl) return;
	impl->ClearMeshCachesInternal();
}

// ============================================================
//  Queries
// ============================================================

static inline void FillRaycastHit(const PxRaycastHit& h, RaycastHit& out)
{
	out.position = FromPx(h.position);
	out.normal = FromPx(h.normal);
	out.distance = h.distance;
	out.faceIndex = HasHitFlag(h.flags, PxHitFlag::eFACE_INDEX) ? h.faceIndex : 0xFFFFFFFFu;
	if (HasHitFlag(h.flags, PxHitFlag::eUV))
	{
		out.baryUV = Vec2(h.u, h.v);
		const float w = 1.0f - h.u - h.v;
		out.barycentric = Vec3(w, h.u, h.v);
	}
	else
	{
		out.baryUV = Vec2::Zero;
		out.barycentric = Vec3::Zero;
	}
	out.actorHandle = h.actor ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h.actor)) : 0ull;
	out.shapeHandle = h.shape ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h.shape)) : 0ull;
	out.userData = h.actor ? h.actor->userData : nullptr;
	out.nativeActor = h.actor;
	out.nativeShape = h.shape;
}

bool PhysXWorld::Raycast(const Vec3& origin, const Vec3& dir, float maxDist, RaycastHit& outHit, uint32_t layerMask, bool hitTriggers) const
{
	return RaycastEx(origin, dir, maxDist, outHit, layerMask, 0xFFFFFFFFu, hitTriggers);
}

bool PhysXWorld::RaycastEx(const Vec3& origin, const Vec3& dir, float maxDist, RaycastHit& outHit, uint32_t layerMask, uint32_t queryMask, bool hitTriggers) const
{
	if (!impl || !impl->scene) return false;

	const Vec3 nd = dir; // assume normalized by caller; PhysX will normalize internally for rays.

	PxRaycastBuffer buf;
	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const PxHitFlags hitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eFACE_INDEX | PxHitFlag::eUV;
	const bool hit = impl->scene->raycast(ToPx(origin), ToPx(nd), maxDist, buf, hitFlags, qfd, &cb);
	if (!hit || !buf.hasBlock) return false;

	FillRaycastHit(buf.block, outHit);
	return true;
}

uint32_t PhysXWorld::RaycastAll(const Vec3& origin, const Vec3& dir, float maxDist, std::vector<RaycastHit>& outHits, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, uint32_t maxHits) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxRaycastHit> hits(maxHits);
	PxRaycastBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Touch);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const PxHitFlags hitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eFACE_INDEX | PxHitFlag::eUV;
	const bool ok = impl->scene->raycast(ToPx(origin), ToPx(dir), maxDist, buf,
		hitFlags,
		qfd, &cb);

	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		RaycastHit rh;
		FillRaycastHit(buf.getTouch(i), rh);
		outHits.push_back(rh);
	}

	return static_cast<uint32_t>(outHits.size());
}

static inline void FillOverlapHit(const PxOverlapHit& h, OverlapHit& out)
{
	out.userData = h.actor ? h.actor->userData : nullptr;
	out.nativeActor = h.actor;
	out.nativeShape = h.shape;
}

uint32_t PhysXWorld::OverlapBox(const Vec3& center, const Quat& rot, const Vec3& halfExtents, std::vector<OverlapHit>& outHits, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, uint32_t maxHits) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxOverlapHit> hits(maxHits);
	PxOverlapBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Touch);

	const PxBoxGeometry geom(ToPx(halfExtents));
	const PxTransform pose = ToPxTransform(center, rot);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->overlap(geom, pose, buf, qfd, &cb);
	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		OverlapHit oh;
		FillOverlapHit(buf.getTouch(i), oh);
		outHits.push_back(oh);
	}

	return static_cast<uint32_t>(outHits.size());
}

uint32_t PhysXWorld::OverlapSphere(const Vec3& center, float radius, std::vector<OverlapHit>& outHits, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, uint32_t maxHits) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxOverlapHit> hits(maxHits);
	PxOverlapBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Touch);

	const PxSphereGeometry geom(radius);
	const PxTransform pose(ToPx(center));

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->overlap(geom, pose, buf, qfd, &cb);
	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		OverlapHit oh;
		FillOverlapHit(buf.getTouch(i), oh);
		outHits.push_back(oh);
	}

	return static_cast<uint32_t>(outHits.size());
}

uint32_t PhysXWorld::OverlapCapsule(const Vec3& center, const Quat& rot, float radius, float halfHeight, std::vector<OverlapHit>& outHits, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, uint32_t maxHits, bool alignYAxis) const
{
	outHits.clear();
	if (!impl || !impl->scene || maxHits == 0) return 0;

	std::vector<PxOverlapHit> hits(maxHits);
	PxOverlapBuffer buf(hits.data(), static_cast<PxU32>(hits.size()));

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Touch);

	const PxCapsuleGeometry geom(radius, halfHeight);

	Quat q = rot;
	if (alignYAxis)
	{
		Quat align = FromPx(CapsuleAlignQuatPx());
		q = q * align; // world rot * align (shape local)
	}

	const PxTransform pose = ToPxTransform(center, q);

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->overlap(geom, pose, buf, qfd, &cb);
	if (!ok) return 0;

	const PxU32 n = buf.getNbTouches();
	outHits.reserve(n);
	for (PxU32 i = 0; i < n; ++i)
	{
		OverlapHit oh;
		FillOverlapHit(buf.getTouch(i), oh);
		outHits.push_back(oh);
	}

	return static_cast<uint32_t>(outHits.size());
}

static inline void FillSweepHit(const PxSweepHit& h, SweepHit& out)
{
	out.position = FromPx(h.position);
	out.normal = FromPx(h.normal);
	out.distance = h.distance;
	out.userData = h.actor ? h.actor->userData : nullptr;
	out.nativeActor = h.actor;
	out.nativeShape = h.shape;
}

bool PhysXWorld::SweepBox(const Vec3& origin, const Quat& rot, const Vec3& halfExtents, const Vec3& dir, float maxDist, SweepHit& outHit, uint32_t layerMask, uint32_t queryMask, bool hitTriggers) const
{
	if (!impl || !impl->scene) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block);

	const PxBoxGeometry geom(ToPx(halfExtents));
	const PxTransform pose = ToPxTransform(origin, rot);

	PxSweepBuffer buf;

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, ToPx(dir), maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillSweepHit(buf.block, outHit);
	return true;
}

bool PhysXWorld::SweepSphere(const Vec3& origin, float radius, const Vec3& dir, float maxDist, SweepHit& outHit, uint32_t layerMask, uint32_t queryMask, bool hitTriggers) const
{
	if (!impl || !impl->scene) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block);

	const PxSphereGeometry geom(radius);
	const PxTransform pose(ToPx(origin));

	PxSweepBuffer buf;

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, ToPx(dir), maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillSweepHit(buf.block, outHit);
	return true;
}

bool PhysXWorld::SweepCapsule(const Vec3& origin, const Quat& rot, float radius, float halfHeight, const Vec3& dir, float maxDist, SweepHit& outHit, uint32_t layerMask, uint32_t queryMask, bool hitTriggers, bool alignYAxis) const
{
	if (!impl || !impl->scene) return false;

	PxQueryFilterData qfd;
	qfd.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

	MaskQueryCallback cb(layerMask, queryMask, hitTriggers, QueryHitMode::Block);

	const PxCapsuleGeometry geom(radius, halfHeight);

	Quat q = rot;
	if (alignYAxis)
	{
		Quat align = FromPx(CapsuleAlignQuatPx());
		q = q * align;
	}

	const PxTransform pose = ToPxTransform(origin, q);

	PxSweepBuffer buf;

	SceneReadLock rl(impl->scene, impl->enableSceneLocks);
	const bool ok = impl->scene->sweep(geom, pose, ToPx(dir), maxDist, buf,
		PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
		qfd, &cb);

	if (!ok || !buf.hasBlock) return false;

	FillSweepHit(buf.block, outHit);
	return true;
}

// ============================================================
//  Events
// ============================================================

void PhysXWorld::DrainActiveTransforms(std::vector<ActiveTransform>& outTransforms)
{
	outTransforms.clear();
	if (!impl) return;

	std::scoped_lock lock(impl->activeMtx);
	outTransforms.swap(impl->activeTransforms);
}

void PhysXWorld::DrainEvents(std::vector<PhysicsEvent>& outEvents)
{
	outEvents.clear();
	if (!impl) return;

	std::scoped_lock lock(impl->eventMtx);
	outEvents.swap(impl->events);
}

void PhysXWorld::SetContactModifyCallback(ContactModifyCallback cb, void* userContext)
{
	if (!impl) return;
	// NOTE: This is only effective if the scene was created with contact modify enabled
	// (PhysXWorld::Desc.enableContactModify = true). Otherwise PhysX will never call it.
	std::scoped_lock lock(impl->contactModifyMtx);
	impl->contactModifyCb = cb;
	impl->contactModifyUser = userContext;
}

// ============================================================
//  Joints (minimal)
// ============================================================

std::unique_ptr<IPhysicsJoint> PhysXWorld::CreateFixedJoint(const IPhysicsActor& a, const IPhysicsActor& b, const FixedJointDesc& desc)
{
	if (!impl || !impl->physics) return {};

	PxRigidActor* ra = reinterpret_cast<PxRigidActor*>(a.GetNativeActor());
	PxRigidActor* rb = reinterpret_cast<PxRigidActor*>(b.GetNativeActor());
	if (!ra || !rb) return {};

	const PxTransform fa = ToPxTransform(desc.frameA.position, desc.frameA.rotation);
	const PxTransform fb = ToPxTransform(desc.frameB.position, desc.frameB.rotation);

	PxFixedJoint* j = PxFixedJointCreate(*impl->physics, ra, fa, rb, fb);
	if (!j) return {};

	j->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, desc.collideConnected);

	const float breakF = (desc.breakForce > 0.0f) ? desc.breakForce : PX_MAX_F32;
	const float breakT = (desc.breakTorque > 0.0f) ? desc.breakTorque : PX_MAX_F32;
	j->setBreakForce(breakF, breakT);

	j->userData = desc.userData;

	return std::make_unique<PhysXJoint>(j, impl);
}


// ============================================================
//  Joints (extended)
// ============================================================

static PxD6Motion::Enum ToPxD6Motion(D6Motion m)
{
	switch (m)
	{
	case D6Motion::Locked:  return PxD6Motion::eLOCKED;
	case D6Motion::Limited: return PxD6Motion::eLIMITED;
	case D6Motion::Free:    return PxD6Motion::eFREE;
	}
	return PxD6Motion::eLOCKED;
}

static void ApplyLimitParams(PxJointLimitParameters& p, float stiffness, float damping, float restitution, float bounceThreshold)
{
	p.stiffness = stiffness;
	p.damping = damping;
	if (restitution >= 0.0f) p.restitution = restitution;
	if (bounceThreshold >= 0.0f) p.bounceThreshold = bounceThreshold;
}

static PxJointLinearLimitPair MakeLinearLimitPair(const PxTolerancesScale& scale, float lower, float upper,
	float stiffness, float damping, float restitution, float bounceThreshold)
{
	if (lower > upper) {
		const float t = lower; lower = upper; upper = t;
	}
	PxJointLinearLimitPair lim(scale, lower, upper);
	ApplyLimitParams(lim, stiffness, damping, restitution, bounceThreshold);
	return lim;
}

static PxJointAngularLimitPair MakeAngularLimitPair(float lower, float upper,
	float stiffness, float damping, float restitution, float bounceThreshold)
{
	if (lower > upper) {
		const float t = lower; lower = upper; upper = t;
	}
	PxJointAngularLimitPair lim(lower, upper, PxSpring(stiffness, damping));
	if (restitution >= 0.0f) lim.restitution = restitution;
	if (bounceThreshold >= 0.0f) lim.bounceThreshold = bounceThreshold;
	return lim;
}

static PxJointLimitCone MakeLimitCone(float yAngle, float zAngle,
	float stiffness, float damping, float restitution, float bounceThreshold)
{
	PxJointLimitCone lim(yAngle, zAngle, PxSpring(stiffness, damping));
	if (restitution >= 0.0f) lim.restitution = restitution;
	if (bounceThreshold >= 0.0f) lim.bounceThreshold = bounceThreshold;
	return lim;
}

static PxD6JointDrive MakeD6Drive(const D6JointDriveDesc& d)
{
	const float f = (d.forceLimit > 0.0f) ? d.forceLimit : PX_MAX_F32;
	return PxD6JointDrive(d.stiffness, d.damping, f, d.isAcceleration);
}

static void ApplyBreakAndFlags(PxJoint& j, bool collideConnected, float breakForce, float breakTorque, void* userData)
{
	j.setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, collideConnected);

	const float bf = (breakForce > 0.0f) ? breakForce : PX_MAX_F32;
	const float bt = (breakTorque > 0.0f) ? breakTorque : PX_MAX_F32;
	j.setBreakForce(bf, bt);

	j.userData = userData;
}

std::unique_ptr<IPhysicsJoint> PhysXWorld::CreateRevoluteJoint(const IPhysicsActor& a, const IPhysicsActor& b, const RevoluteJointDesc& desc)
{
	if (!impl || !impl->physics) return {};

	PxRigidActor* ra = reinterpret_cast<PxRigidActor*>(a.GetNativeActor());
	PxRigidActor* rb = reinterpret_cast<PxRigidActor*>(b.GetNativeActor());
	if (!ra || !rb) return {};

	const PxTransform fa = ToPxTransform(desc.frameA.position, desc.frameA.rotation);
	const PxTransform fb = ToPxTransform(desc.frameB.position, desc.frameB.rotation);

	PxRevoluteJoint* j = PxRevoluteJointCreate(*impl->physics, ra, fa, rb, fb);
	if (!j) return {};

	ApplyBreakAndFlags(*j, desc.collideConnected, desc.breakForce, desc.breakTorque, desc.userData);

	// Limit
	j->setRevoluteJointFlag(PxRevoluteJointFlag::eLIMIT_ENABLED, desc.enableLimit);
	if (desc.enableLimit)
	{
		PxJointAngularLimitPair lim = MakeAngularLimitPair(
			desc.lowerLimit, desc.upperLimit,
			desc.limitStiffness, desc.limitDamping,
			desc.limitRestitution, desc.limitBounceThreshold);
		j->setLimit(lim);
	}

	// Drive (motor)
	j->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_ENABLED, desc.enableDrive);
	j->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_FREESPIN, desc.driveFreeSpin);
	j->setDriveVelocity(desc.driveVelocity, true);
	j->setDriveForceLimit((desc.driveForceLimit > 0.0f) ? desc.driveForceLimit : PX_MAX_F32);

	// NOTE: PhysX default interprets drive force limit as impulse unless this flag is set.
	j->setConstraintFlag(PxConstraintFlag::eDRIVE_LIMITS_ARE_FORCES, desc.driveLimitsAreForces);

	return std::make_unique<PhysXJoint>(j, impl);
}

std::unique_ptr<IPhysicsJoint> PhysXWorld::CreatePrismaticJoint(const IPhysicsActor& a, const IPhysicsActor& b, const PrismaticJointDesc& desc)
{
	if (!impl || !impl->physics) return {};

	PxRigidActor* ra = reinterpret_cast<PxRigidActor*>(a.GetNativeActor());
	PxRigidActor* rb = reinterpret_cast<PxRigidActor*>(b.GetNativeActor());
	if (!ra || !rb) return {};

	const PxTransform fa = ToPxTransform(desc.frameA.position, desc.frameA.rotation);
	const PxTransform fb = ToPxTransform(desc.frameB.position, desc.frameB.rotation);

	PxPrismaticJoint* j = PxPrismaticJointCreate(*impl->physics, ra, fa, rb, fb);
	if (!j) return {};

	ApplyBreakAndFlags(*j, desc.collideConnected, desc.breakForce, desc.breakTorque, desc.userData);

	j->setPrismaticJointFlag(PxPrismaticJointFlag::eLIMIT_ENABLED, desc.enableLimit);
	if (desc.enableLimit)
	{
		const PxTolerancesScale scale = impl->physics->getTolerancesScale();
		PxJointLinearLimitPair lim = MakeLinearLimitPair(
			scale,
			desc.lowerLimit, desc.upperLimit,
			desc.limitStiffness, desc.limitDamping,
			desc.limitRestitution, desc.limitBounceThreshold);
		j->setLimit(lim);
	}

	return std::make_unique<PhysXJoint>(j, impl);
}

std::unique_ptr<IPhysicsJoint> PhysXWorld::CreateDistanceJoint(const IPhysicsActor& a, const IPhysicsActor& b, const DistanceJointDesc& desc)
{
	if (!impl || !impl->physics) return {};

	PxRigidActor* ra = reinterpret_cast<PxRigidActor*>(a.GetNativeActor());
	PxRigidActor* rb = reinterpret_cast<PxRigidActor*>(b.GetNativeActor());
	if (!ra || !rb) return {};

	const PxTransform fa = ToPxTransform(desc.frameA.position, desc.frameA.rotation);
	const PxTransform fb = ToPxTransform(desc.frameB.position, desc.frameB.rotation);

	PxDistanceJoint* j = PxDistanceJointCreate(*impl->physics, ra, fa, rb, fb);
	if (!j) return {};

	ApplyBreakAndFlags(*j, desc.collideConnected, desc.breakForce, desc.breakTorque, desc.userData);

	float minD = desc.minDistance;
	float maxD = desc.maxDistance;
	if (minD > maxD) {
		const float t = minD; minD = maxD; maxD = t;
	}

	j->setMinDistance(minD);
	j->setMaxDistance(maxD);
	j->setTolerance(desc.tolerance);

	j->setDistanceJointFlag(PxDistanceJointFlag::eMIN_DISTANCE_ENABLED, desc.enableMinDistance);
	j->setDistanceJointFlag(PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, desc.enableMaxDistance);

	j->setDistanceJointFlag(PxDistanceJointFlag::eSPRING_ENABLED, desc.enableSpring);
	if (desc.enableSpring)
	{
		j->setStiffness(desc.stiffness);
		j->setDamping(desc.damping);
	}

	return std::make_unique<PhysXJoint>(j, impl);
}

std::unique_ptr<IPhysicsJoint> PhysXWorld::CreateSphericalJoint(const IPhysicsActor& a, const IPhysicsActor& b, const SphericalJointDesc& desc)
{
	if (!impl || !impl->physics) return {};

	PxRigidActor* ra = reinterpret_cast<PxRigidActor*>(a.GetNativeActor());
	PxRigidActor* rb = reinterpret_cast<PxRigidActor*>(b.GetNativeActor());
	if (!ra || !rb) return {};

	const PxTransform fa = ToPxTransform(desc.frameA.position, desc.frameA.rotation);
	const PxTransform fb = ToPxTransform(desc.frameB.position, desc.frameB.rotation);

	PxSphericalJoint* j = PxSphericalJointCreate(*impl->physics, ra, fa, rb, fb);
	if (!j) return {};

	ApplyBreakAndFlags(*j, desc.collideConnected, desc.breakForce, desc.breakTorque, desc.userData);

	j->setSphericalJointFlag(PxSphericalJointFlag::eLIMIT_ENABLED, desc.enableLimit);
	if (desc.enableLimit)
	{
		PxJointLimitCone lim = MakeLimitCone(
			desc.yLimitAngle, desc.zLimitAngle,
			desc.limitStiffness, desc.limitDamping,
			desc.limitRestitution, desc.limitBounceThreshold);
		j->setLimitCone(lim);
	}

	return std::make_unique<PhysXJoint>(j, impl);
}

std::unique_ptr<IPhysicsJoint> PhysXWorld::CreateD6Joint(const IPhysicsActor& a, const IPhysicsActor& b, const D6JointDesc& desc)
{
	if (!impl || !impl->physics) return {};

	PxRigidActor* ra = reinterpret_cast<PxRigidActor*>(a.GetNativeActor());
	PxRigidActor* rb = reinterpret_cast<PxRigidActor*>(b.GetNativeActor());
	if (!ra || !rb) return {};

	const PxTransform fa = ToPxTransform(desc.frameA.position, desc.frameA.rotation);
	const PxTransform fb = ToPxTransform(desc.frameB.position, desc.frameB.rotation);

	PxD6Joint* j = PxD6JointCreate(*impl->physics, ra, fa, rb, fb);
	if (!j) return {};

	ApplyBreakAndFlags(*j, desc.collideConnected, desc.breakForce, desc.breakTorque, desc.userData);
	j->setConstraintFlag(PxConstraintFlag::eDRIVE_LIMITS_ARE_FORCES, desc.driveLimitsAreForces);

	// Motions
	j->setMotion(PxD6Axis::eX, ToPxD6Motion(desc.motionX));
	j->setMotion(PxD6Axis::eY, ToPxD6Motion(desc.motionY));
	j->setMotion(PxD6Axis::eZ, ToPxD6Motion(desc.motionZ));
	j->setMotion(PxD6Axis::eTWIST, ToPxD6Motion(desc.motionTwist));
	j->setMotion(PxD6Axis::eSWING1, ToPxD6Motion(desc.motionSwing1));
	j->setMotion(PxD6Axis::eSWING2, ToPxD6Motion(desc.motionSwing2));

	// Linear limits (per axis)
	const PxTolerancesScale scale = impl->physics->getTolerancesScale();
	if (desc.motionX == D6Motion::Limited)
		j->setLinearLimit(PxD6Axis::eX, MakeLinearLimitPair(scale,
			desc.linearLimitX.lower, desc.linearLimitX.upper,
			desc.linearLimitX.stiffness, desc.linearLimitX.damping,
			desc.linearLimitX.restitution, desc.linearLimitX.bounceThreshold));
	if (desc.motionY == D6Motion::Limited)
		j->setLinearLimit(PxD6Axis::eY, MakeLinearLimitPair(scale,
			desc.linearLimitY.lower, desc.linearLimitY.upper,
			desc.linearLimitY.stiffness, desc.linearLimitY.damping,
			desc.linearLimitY.restitution, desc.linearLimitY.bounceThreshold));
	if (desc.motionZ == D6Motion::Limited)
		j->setLinearLimit(PxD6Axis::eZ, MakeLinearLimitPair(scale,
			desc.linearLimitZ.lower, desc.linearLimitZ.upper,
			desc.linearLimitZ.stiffness, desc.linearLimitZ.damping,
			desc.linearLimitZ.restitution, desc.linearLimitZ.bounceThreshold));

	// Distance limit for multi-axis linear limits (kept generous; per-axis limits are set above)
	// If you want a strict spherical distance limit, configure it explicitly via PxD6Joint::setDistanceLimit.

	// Angular limits
	if (desc.motionTwist == D6Motion::Limited)
	{
		PxJointAngularLimitPair lim = MakeAngularLimitPair(
			desc.twistLimit.lower, desc.twistLimit.upper,
			desc.twistLimit.stiffness, desc.twistLimit.damping,
			desc.twistLimit.restitution, desc.twistLimit.bounceThreshold);
		j->setTwistLimit(lim);
	}

	if (desc.motionSwing1 == D6Motion::Limited || desc.motionSwing2 == D6Motion::Limited)
	{
		PxJointLimitCone lim = MakeLimitCone(
			desc.swingLimit.yAngle, desc.swingLimit.zAngle,
			desc.swingLimit.stiffness, desc.swingLimit.damping,
			desc.swingLimit.restitution, desc.swingLimit.bounceThreshold);
		j->setSwingLimit(lim);
	}

	// Drives
	j->setDrive(PxD6Drive::eX, MakeD6Drive(desc.driveX));
	j->setDrive(PxD6Drive::eY, MakeD6Drive(desc.driveY));
	j->setDrive(PxD6Drive::eZ, MakeD6Drive(desc.driveZ));
	j->setDrive(PxD6Drive::eSWING, MakeD6Drive(desc.driveSwing));
	j->setDrive(PxD6Drive::eTWIST, MakeD6Drive(desc.driveTwist));
	j->setDrive(PxD6Drive::eSLERP, MakeD6Drive(desc.driveSlerp));

	// Drive targets
	j->setDrivePosition(ToPxTransform(desc.drivePose.position, desc.drivePose.rotation), true);
	j->setDriveVelocity(ToPx(desc.driveLinearVelocity), ToPx(desc.driveAngularVelocity), true);

	return std::make_unique<PhysXJoint>(j, impl);
}
