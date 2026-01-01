#include "PhysXWorld_Internal.h"

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
