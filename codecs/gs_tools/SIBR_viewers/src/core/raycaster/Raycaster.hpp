/*
 * Copyright (C) 2020, Inria
 * GRAPHDECO research group, https://team.inria.fr/graphdeco
 * All rights reserved.
 *
 * This software is free for non-commercial, research and evaluation use 
 * under the terms of the LICENSE.md file.
 *
 * For inquiries contact sibr@inria.fr and/or George.Drettakis@inria.fr
 */


#pragma once

# pragma warning(push, 0)
/** Embree 4 renamed the intersect context and moved it behind an arguments
    struct. Distributions from Ubuntu 24.04 onwards ship only Embree 4, while
    older ones ship only Embree 3, so both are supported here. */
#  if defined(__has_include) && __has_include(<embree4/rtcore.h>)
#    include <embree4/rtcore.h>
#    include <embree4/rtcore_ray.h>
#    define SIBR_EMBREE_MAJOR 4
#  else
#    include <embree3/rtcore.h>
#    include <embree3/rtcore_ray.h>
#    define SIBR_EMBREE_MAJOR 3
#  endif
#  include <xmmintrin.h>	// functions for setting the control register
#  include <pmmintrin.h>	// functions for setting the control register
# pragma warning(pop)

#if SIBR_EMBREE_MAJOR >= 4
using RTCIntersectContext = RTCRayQueryContext;
static inline void rtcInitIntersectContext(RTCIntersectContext * ctx) { rtcInitRayQueryContext(ctx); }
static inline void sibr_rtcOccluded1(RTCScene s, RTCIntersectContext * ctx, RTCRay * r) {
	RTCOccludedArguments a; rtcInitOccludedArguments(&a); a.context = ctx; rtcOccluded1(s, r, &a);
}
static inline void sibr_rtcOccluded8(const int * v, RTCScene s, RTCIntersectContext * ctx, RTCRay8 * r) {
	RTCOccludedArguments a; rtcInitOccludedArguments(&a); a.context = ctx; rtcOccluded8(v, s, r, &a);
}
static inline void sibr_rtcIntersect1(RTCScene s, RTCIntersectContext * ctx, RTCRayHit * rh) {
	RTCIntersectArguments a; rtcInitIntersectArguments(&a); a.context = ctx; rtcIntersect1(s, rh, &a);
}
static inline void sibr_rtcIntersect8(const int * v, RTCScene s, RTCIntersectContext * ctx, RTCRayHit8 * rh) {
	RTCIntersectArguments a; rtcInitIntersectArguments(&a); a.context = ctx; rtcIntersect8(v, s, rh, &a);
}
#else
static inline void sibr_rtcOccluded1(RTCScene s, RTCIntersectContext * ctx, RTCRay * r) { rtcOccluded1(s, ctx, r); }
static inline void sibr_rtcOccluded8(const int * v, RTCScene s, RTCIntersectContext * ctx, RTCRay8 * r) { rtcOccluded8(v, s, ctx, r); }
static inline void sibr_rtcIntersect1(RTCScene s, RTCIntersectContext * ctx, RTCRayHit * rh) { rtcIntersect1(s, ctx, rh); }
static inline void sibr_rtcIntersect8(const int * v, RTCScene s, RTCIntersectContext * ctx, RTCRayHit8 * rh) { rtcIntersect8(v, s, ctx, rh); }
#endif

# include <core/graphics/Mesh.hpp>
# include <core/system/Matrix.hpp>
# include "core/raycaster/Config.hpp"
# include "core/raycaster/Ray.hpp"

namespace sibr
{
	///
	/// This class can be used to cast rays against a scene containing triangular
	/// meshes. You can check for intersections with the geometry and get
	/// information about the hit (such as coordinates, distance, triangle id).
	///
	/// You should have one or few instance of this class (for performance
	/// purposes). Each instance can run in parallel.
	///
	/// \note This abstraction is built on top of Embree.
	/// \warning There is no backface culling applied.
	/// \ingroup sibr_raycaster
	///
	class SIBR_RAYCASTER_EXPORT Raycaster
	{
	public:
		typedef std::shared_ptr<RTCDevice>	RTCDevicePtr;
		typedef std::shared_ptr<RTCScene>		RTCScenePtr;
		typedef std::shared_ptr<Raycaster>		Ptr;

		typedef	uint	geomId;
		/// Stores a number representing an invalid geom id.
		static const geomId InvalidGeomId; 

		/// Destructor.
		~Raycaster( void );


		/// Init the raycaster.
		/// Called automatically whenever you call a member that need this
		/// instance to be init. However, you can call it manually to check
		/// error on init.
		/// \param sceneType the type of scene, see Embree doc.
		/// \return a success flag
		bool	init(RTCSceneFlags sceneType = RTC_SCENE_FLAG_NONE );

		/// Add a triangle mesh to the raycast scene, taht you won't modify frequently
		/// Return the id  of the geometry added so you can track your mesh (and compare
		/// its id to the one stored in RayHits).
		/// \param mesh the mesh to add
		/// \return the mesh ID or Raycaster::InvalidGeomId if it fails.
		geomId	addMesh( const sibr::Mesh& mesh );

		/// Add a triangle mesh to the raycast scene, that you will frequently update.
		/// \param mesh the mesh to add
		/// \return the mesh ID or Raycaster::InvalidGeomId if it fails.
		geomId	addDynamicMesh( const sibr::Mesh& mesh );

		/// Add a triangle mesh to the raycast scene.
		/// \param mesh the mesh to add
		/// \param type the type of mesh
		/// \return the mesh ID or Raycaster::InvalidGeomId if it fails.
		geomId	addGenericMesh( const sibr::Mesh& mesh, RTCBuildQuality type );

		/// Transform the vertices of a mesh by applying a sibr::Matrix4f mat.
		/// \note The original positions are always stored *unchanged* in mesh.vertices -- we only xform the vertices in the embree buffer
		/// \param mesh the mesh to transform
		/// \param mesh_id the corresponding raycaster mesh id
		/// \param mat the transformation to apply
		/// \param centerPt will contain the new centroid
		/// \param maxlen will contain the maximum distance from a vertex to the centroid
		/// \bug maxlen is computed incrementally and may be incorrect
		void xformRtcMeshOnly(sibr::Mesh& mesh, geomId mesh_id, sibr::Matrix4f& mat, sibr::Vector3f& centerPt, float& maxlen);

		/// Launch a ray into the raycaster scene. Return information about
		/// this cast in RayHit. To simply know if something has been hit, use RayHit::hitSomething().
		/// \sa hitSomething
		/// \param ray the ray to cast
		/// \param minDist Any intersection closer than minDist from the ray origin will be ignored. Useful to avoid self intersections. 
		/// \return the (potential) intersection information
		RayHit	intersect( const Ray& ray, float minDist=0.f  );

		/// Launch 8 rays into the raycaster scene in an optimized fashion, reporting intersections infos.
		/// \param inray the rays to cast
		/// \param valid8 an indication of which of the rays should be cast
		/// \param minDist Any intersection closer than minDist from the ray origin will be ignored. Useful to avoid self intersections. 
		/// \return the list of (potential) intersection informations
		std::array<RayHit, 8>	intersect8(const std::array<Ray, 8>& inray,const std::vector<int> & valid8=std::vector<int>(8,-1), float minDist = 0.f );

		/// Optimized ray-cast that only tells you if an intersection occured.
		/// \sa intersect
		/// \param ray the ray to cast
		/// \param minDist Any intersection closer than minDist from the ray origin will be ignored. Useful to avoid self intersections. 
		/// \return true if an intersection took place
		bool	hitSomething( const Ray& ray, float minDist=0.f );

		/// Launch 8 rays into the raycaster scene in an optimized fashion, reporting if intersections occured.
		/// \param inray the rays to cast
		/// \param minDist Any intersection closer than minDist from the ray origin will be ignored. Useful to avoid self intersections. 
		/// \return a list of boolean denoting if intersections happened
		std::array<bool, 8>	hitSomething8(const std::array<Ray, 8>& inray, float minDist = 0.f);

		/// Disable geometry to avoid raycasting against it (eg background when only intersecting a foreground object).
		/// \param id the mesh to disable
		/// \todo Untested.
		void	disableGeom(geomId id) { rtcDisableGeometry(rtcGetGeometry((*_scene.get()),id)); rtcCommitGeometry(rtcGetGeometry(*_scene.get(),id)); rtcCommitScene(*_scene.get()); }

		/// Enable geometry to start raycasting it again.
		/// \param id the geometry to enable 
		/// \todo Untested.
		void	enableGeom(geomId id) { rtcEnableGeometry(rtcGetGeometry((*_scene.get()),id)); rtcCommitGeometry(rtcGetGeometry(*_scene.get(),id)); rtcCommitScene(*_scene.get());}

		/// Delete geometry
		/// \param id the geometry to delete
		void	deleteGeom(geomId id) { rtcReleaseGeometry(rtcGetGeometry((*_scene.get()),id)); rtcCommitGeometry(rtcGetGeometry(*_scene.get(),id)); rtcCommitScene(*_scene.get());} 

		/// Clears internal scene..
		void clearGeometry();

		/// Returns the normalized smooth normal (shading normal) from a hit, assuming the mesh has normals
		/// \param mesh sibr::Mesh used by raycaster
		/// \param hit intersection basic information
		/// \return the interpolated normalized normal
		static sibr::Vector3f smoothNormal(const sibr::Mesh & mesh, const RayHit & hit);

		/// Interpolate color at a hit (barycentric interpolation), assuming the mesh has colors.
		/// \param mesh sibr::Mesh used by raycaster
		/// \param hit intersection basic information
		/// \return the interpolated color
		static sibr::Vector3f smoothColor(const sibr::Mesh & mesh, const RayHit & hit);

		/// Interpolate texcoords from a hit (barycentric interpolation), assuming the mesh has UVs.
		/// \param mesh sibr::Mesh used by raycaster
		/// \param hit intersection basic information
		/// \‚eturn the interpolated texture coordinates
		static sibr::Vector2f smoothUV(const sibr::Mesh & mesh, const RayHit & hit);

		/// \return true if the raycaster is initialized. 
		bool isInit() { return g_device && _scene; }

	private: 

		/// Will be called by embree whenever an error occurs
		/// \param userPtr the user data pointer
		/// \param code the error code
		/// \param msg additional info message.
		static void rtcErrorCallback(void* userPtr, RTCError code, const char* msg);

		
		static bool g_initRegisterFlag; ///< Used to initialize flag of registers used by SSE
		static RTCDevicePtr	g_device;	///< embree device (context for a raycaster)

		/// \return the internal scene pointer
		RTCScenePtr	scene() 	{ return _scene; }

		RTCScenePtr		_scene;		///< scene storing raycastable meshes
		RTCDevicePtr	_devicePtr;	///< embree device (context for a raycaster)
	};

	///// DEFINITION /////

} // namespace sibr
