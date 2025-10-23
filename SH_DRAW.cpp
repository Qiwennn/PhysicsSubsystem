#include "SH_DRAW.h"

// API Abstraction
#include "PrimeEngine/APIAbstraction/APIAbstractionDefines.h"

// Outer-Engine includes

// Inter-Engine includes
#include "PrimeEngine/FileSystem/FileReader.h"
#include "PrimeEngine/APIAbstraction/GPUMaterial/GPUMaterialSet.h"
#include "PrimeEngine/PrimitiveTypes/PrimitiveTypes.h"
#include "PrimeEngine/APIAbstraction/Texture/Texture.h"
#include "PrimeEngine/APIAbstraction/Effect/EffectManager.h"
#include "PrimeEngine/APIAbstraction/GPUBuffers/VertexBufferGPUManager.h"
#include "PrimeEngine/Lua/LuaEnvironment.h"
#include "Light.h"
#include "PrimeEngine/Render/ShaderActions/SetPerObjectConstantsShaderAction.h"

// Sibling/Children includes
#include "Mesh.h"
#include "MeshInstance.h"
#include "SkeletonInstance.h"
#include "SceneNode.h"
#include "RootSceneNode.h"
#include "DrawList.h"
#include "PrimeEngine/Scene/DefaultAnimationSM.h"
#include "PrimeEngine/Geometry/IndexBufferCPU/IndexBufferCPU.h"

#include "PrimeEngine/APIAbstraction/GPUBuffers/AnimSetBufferGPU.h"
#include "PrimeEngine/Render/ShaderActions/SetInstanceControlConstantsShaderAction.h"
#include "PrimeEngine/Render/ShaderActions/SA_SetAndBind_ConstResource_PerInstanceData.h"
#include "PrimeEngine/Render/ShaderActions/SA_SetAndBind_ConstResource_SingleObjectAnimationPalette.h"
#include "PrimeEngine/Render/ShaderActions/SA_SetAndBind_ConstResource_InstancedObjectsAnimationPalettes.h"
#include "PrimeEngine/Scene/Skeleton.h"

#include "DebugRenderer.h"
#include "CameraManager.h"
#include "CameraSceneNode.h"

#include "SH_DRAW.h"
#include "CharacterControl/PhysicsManager.h"

extern int g_disableSkinRender;
extern int g_iDebugBoneSegment;

namespace PE {
namespace Components {

// ---------- MeshHelpers ----------

PrimitiveTypes::UInt32 MeshHelpers::getNumberOfRangeCalls(IndexBufferGPU *pibGPU)
{
	if (pibGPU && pibGPU->m_indexRanges.m_size > 0)
		return pibGPU->m_indexRanges.m_size;
	return 1;
}

void MeshHelpers::analyzeTechniqueSequences(
	Mesh *pObj,
	PrimitiveTypes::UInt32 &numRanges,
	PrimitiveTypes::UInt32 &numFullSequences)
{
	IndexBufferGPU *pIB = pObj->m_hIndexBufferGPU.isValid()
		? pObj->m_hIndexBufferGPU.getObject<IndexBufferGPU>()
		: 0;

	numRanges = getNumberOfRangeCalls(pIB);

	// There should be numRanges technique sequences (each sequence = passes for one range).
	PrimitiveTypes::UInt32 numSequences = pObj->m_effects.m_size;

	if (numSequences % numRanges != 0)
	{
		PEINFO("Mesh: WARNING: number of technique sequences is not multiple of ranges\n");
	}

	numFullSequences = numSequences / numRanges;
}

void MeshHelpers::pushEffects(Mesh *pObj)
{
	PrimitiveTypes::UInt32 numRanges, numFullSequences;
	MeshHelpers::analyzeTechniqueSequences(pObj, numRanges, numFullSequences);

	// Copy the top technique sequence right under it.
	const PrimitiveTypes::UInt32 insertionIndex = numRanges;
	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; ++iRange)
	{
		pObj->m_effects.insert(pObj->m_effects[iRange], insertionIndex + iRange);
	}
}

void MeshHelpers::popEffects(Mesh *pObj)
{
	PrimitiveTypes::UInt32 numRanges, numFullSequences;
	MeshHelpers::analyzeTechniqueSequences(pObj, numRanges, numFullSequences);

	// Remove the topmost effect sequence.
	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; ++iRange)
	{
		pObj->m_effects.remove(0);
	}
}

void MeshHelpers::setPixelShadersOfTopEffects(PE::GameContext &context, PE::MemoryArena arena, Mesh *pObj)
{
	PrimitiveTypes::UInt32 numRanges, numFullSequences;
	MeshHelpers::analyzeTechniqueSequences(pObj, numRanges, numFullSequences);

	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; ++iRange)
	{
		for (PrimitiveTypes::UInt32 iPass = 0; iPass < pObj->m_effects[iRange].m_size; ++iPass)
		{
			Handle hEffect = pObj->m_effects[iRange][iPass];
			Effect &curEffect = *hEffect.getObject<Effect>();

			// Duplicate current effect and swap PS if there is a substitute.
			Handle hNewEffect("EFFECT", sizeof(Effect));
			Effect *pNewEffect = new(hNewEffect) Effect(context, arena, hNewEffect);
			pNewEffect->loadFromCopy(curEffect);
			
			Handle hSubstPSTech = EffectManager::Instance()->m_pixelShaderSubstitutes[curEffect.m_psInputFamily];
			if (hSubstPSTech.isValid())
			{
				Effect *pSubstPSTech = hSubstPSTech.getObject<Effect>();
				pNewEffect->setPixelShader(*pSubstPSTech);
			}

			pObj->m_effects[iRange][iPass] = hNewEffect;
		}
	}
}

void MeshHelpers::setEffectOfTopEffectSecuence(Mesh *pObj, Handle hNewEffect)
{
	PrimitiveTypes::UInt32 numRanges, numFullSequences;
	MeshHelpers::analyzeTechniqueSequences(pObj, numRanges, numFullSequences);

	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; ++iRange)
	{
		for (PrimitiveTypes::UInt32 iPass = 0; iPass < pObj->m_effects[iRange].m_size; ++iPass)
		{
			pObj->m_effects[iRange][iPass] = hNewEffect;
		}
	}
}

void MeshHelpers::setZOnlyEffectOfTopEffectSecuence(Mesh *pObj, Handle hNewEffect)
{
	PrimitiveTypes::UInt32 numRanges, numFullSequences;
	MeshHelpers::analyzeTechniqueSequences(pObj, numRanges, numFullSequences);

	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; ++iRange)
	{
		for (PrimitiveTypes::UInt32 iPass = 0; iPass < pObj->m_effects[iRange].m_size; ++iPass)
		{
			pObj->m_zOnlyEffects[iRange][iPass] = hNewEffect;
		}
	}
}

// ---------- SingleHandler_DRAW (singleton) ----------

PE_IMPLEMENT_SINGLETON_CLASS1(SingleHandler_DRAW, Component);

void SingleHandler_DRAW::do_GATHER_DRAWCALLS(Events::Event *pEvt)
{
	Component *pCaller = pEvt->m_prevDistributor.getObject<Component>();
	Mesh *pMeshCaller = (Mesh *)pCaller;
	if (pMeshCaller->m_instances.m_size == 0)
		return; // nothing to draw

	Events::Event_GATHER_DRAWCALLS *pDrawEvent = NULL;
	Events::Event_GATHER_DRAWCALLS_Z_ONLY *pZOnlyDrawEvent = NULL;

	if (pEvt->isInstanceOf<Events::Event_GATHER_DRAWCALLS>())
		pDrawEvent = (Events::Event_GATHER_DRAWCALLS *)(pEvt);
	else
		pZOnlyDrawEvent = (Events::Event_GATHER_DRAWCALLS_Z_ONLY *)(pEvt);
    
	// Assume visible; may be reduced by frustum culling.
	pMeshCaller->m_numVisibleInstances = pMeshCaller->m_instances.m_size;

	// Per-instance frustum check using per-mesh AABB (PhysicsManager).
	if (pMeshCaller->m_performBoundingVolumeCulling)
	{
		pMeshCaller->m_numVisibleInstances = 0;

		for (int iInst = 0; iInst < pMeshCaller->m_instances.m_size; ++iInst)
		{
			MeshInstance *pInst = pMeshCaller->m_instances[iInst].getObject<MeshInstance>();

			// Active camera
			CameraSceneNode *pCam = CameraManager::Instance()->getActiveCamera()->getCamSceneNode();

			// Resolve SceneNode for this instance. Skinned soldier uses a deeper chain.
			SceneNode *pCurrentSN = pInst->getFirstParentByTypePtr<SceneNode>();
			SkeletonInstance *pSI = NULL;

			if (pCurrentSN == NULL)
			{
				pSI = pInst->getFirstParentByTypePtr<SkeletonInstance>();
				if (pSI)
				{
					SceneNode *pRotateSN = pSI->getFirstParentByTypePtr<SceneNode>();
					SceneNode *pSN = pRotateSN->getFirstParentByTypePtr<SceneNode>();
					pCurrentSN = pSN->getFirstParentByTypePtr<SceneNode>();
					pInst->m_culledOut = false;
					++pMeshCaller->m_numVisibleInstances;
				}
			}
			else
			{
				// Retrieve AABB info from PhysicsManager.
				PhysicsManager *pPhyManager = pInst->getFirstComponent<PhysicsManager>();
				pInst->m_culledOut = true;

				const Matrix4x4 worldMatrix = pCurrentSN->m_worldTransform;

				// Vertex-against-frustum test: if any AABB vertex lies inside all 6 planes, keep the instance.
				bool anyVertexInside = false;
				const int kNumPlanes = 6;
				for (int vIndex = 0; vIndex < 8 && !anyVertexInside; ++vIndex)
				{
					Vector3 vtxT = worldMatrix * pPhyManager->m_boundingBoxVertex[vIndex];

					int insideCount = 0;
					for (int ip = 0; ip < kNumPlanes; ++ip)
					{
						if (pCam->m_frustumPlanes[ip].isInsidePlane(vtxT))
							++insideCount;
						else
							break; // fails this plane
					}
					if (insideCount == kNumPlanes)
						anyVertexInside = true;
				}

				pInst->m_culledOut = !anyVertexInside;

				// Keep PhysicsManager's post-transform data fresh for debug draw / collisions.
				pPhyManager->buildBoundingVolumeAfterTransform(worldMatrix);

				if (pInst->m_culledOut)
					continue;

				// Visible instance.
				++pMeshCaller->m_numVisibleInstances;

				// Optional: draw AABB wireframe using 4 edge frames.
				for (int i = 0; i < 4; ++i)
				{
					DebugRenderer::Instance()->createAABBLineMesh(
						true,
						pPhyManager->m_boundingBoxAfterTransform[i],
						NULL, 0, 0);
				}
			}
		}
	}

	DrawList *pDrawList = pDrawEvent ? DrawList::Instance() : DrawList::ZOnlyInstance();

	// Index buffer
	Handle hIBuf = pMeshCaller->m_hIndexBufferGPU;
	IndexBufferGPU *pibGPU = hIBuf.getObject<IndexBufferGPU>();

	// Vertex buffers
	Handle hVertexBuffersGPU[4];
	Vector4 vbufWeights;
	int numVBufs = pMeshCaller->m_vertexBuffersGPUHs.m_size;
	assert(numVBufs < 4);
	for (int ivbuf = 0; ivbuf < numVBufs; ++ivbuf)
	{
		hVertexBuffersGPU[ivbuf] = pMeshCaller->m_vertexBuffersGPUHs[ivbuf];
		vbufWeights.m_values[ivbuf] = hVertexBuffersGPU[ivbuf].getObject<VertexBufferGPU>()->m_weight;
	}
	if (numVBufs > 1)
	{
		for (int ivbuf = numVBufs; ivbuf < 4; ++ivbuf)
		{
			hVertexBuffersGPU[ivbuf] = hVertexBuffersGPU[0];
			vbufWeights.m_values[ivbuf] = vbufWeights.m_values[0];
		}
		numVBufs = 4; // blend-shape path expects 4 slots
	}

	// Material set required
	if (!pMeshCaller->m_hMaterialSetGPU.isValid())
		return;

	GPUMaterialSet *pGpuMatSet = pMeshCaller->m_hMaterialSetGPU.getObject<GPUMaterialSet>();

	Matrix4x4 projectionViewWorldMatrix =
		pDrawEvent ? pDrawEvent->m_projectionViewTransform : pZOnlyDrawEvent->m_projectionViewTransform;

	Handle hParentSN = pCaller->getFirstParentByType<SceneNode>();
	if (!hParentSN.isValid())
	{
		// Allow skeleton in the chain
		hParentSN = pCaller->getFirstParentByTypePtr<SkeletonInstance>()->getFirstParentByType<SceneNode>();
	}
	Matrix4x4 worldMatrix;
	worldMatrix.loadIdentity();

	if (hParentSN.isValid())
		worldMatrix = hParentSN.getObject<SceneNode>()->m_worldTransform;
	
	projectionViewWorldMatrix = projectionViewWorldMatrix * worldMatrix;

	// Submit all material ranges
	const PrimitiveTypes::UInt32 numRanges = MeshHelpers::getNumberOfRangeCalls(pibGPU);
	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; ++iRange)
	{
		gatherDrawCallsForRange(
			pMeshCaller, pDrawList,
			&hVertexBuffersGPU[0], numVBufs, vbufWeights,
			iRange, pDrawEvent, pZOnlyDrawEvent);
	}
}

void SingleHandler_DRAW::gatherDrawCallsForRange(
	Mesh *pMeshCaller,
	DrawList *pDrawList,
	Handle *pHVBs,
	int vbCount,
	Vector4 &vbWeights, 
	int iRange,
	Events::Event_GATHER_DRAWCALLS *pDrawEvent,
	Events::Event_GATHER_DRAWCALLS_Z_ONLY *pZOnlyDrawEvent)
{
	// Choose effect list
	PEStaticVector<Handle, 4> *pEffectsForRange = NULL;
	bool haveInstancesAndInstanceEffect = false;
	
	Handle hIBuf = pMeshCaller->m_hIndexBufferGPU;
	IndexBufferGPU *pibGPU = hIBuf.getObject<IndexBufferGPU>();
	IndexRange &ir = pibGPU->m_indexRanges[iRange];
	const bool hasJointSegments = (ir.m_boneSegments.m_size > 0); // true => skinned

	if (pDrawEvent)
	{
		// If only 1 instance, prefer regular effects; otherwise choose instance effects if available.
		if (pMeshCaller->m_effects[iRange].m_size == 0 && pMeshCaller->m_instanceEffects[iRange].m_size == 0)
			return;

		pEffectsForRange = &pMeshCaller->m_effects[iRange];

		if (pMeshCaller->m_numVisibleInstances > 1)
		{
			haveInstancesAndInstanceEffect = (pMeshCaller->m_instanceEffects[iRange].m_size > 0);
			for (unsigned int iPass = 0; iPass < pMeshCaller->m_instanceEffects[iRange].m_size; ++iPass)
			{
				if (!pMeshCaller->m_instanceEffects[iRange][iPass].isValid())
				{
					haveInstancesAndInstanceEffect = false;
					break;
				}
			}
			if (haveInstancesAndInstanceEffect)
				pEffectsForRange = &pMeshCaller->m_instanceEffects[iRange];
		}
		else
		{
			// Validate non-instanced effects
			bool haveEffect = pMeshCaller->m_effects[iRange].m_size > 0;
			for (unsigned int iPass = 0; iPass < pMeshCaller->m_effects[iRange].m_size; ++iPass)
			{
				if (!pMeshCaller->m_effects[iRange][iPass].isValid())
				{
					haveEffect = false;
					break;
				}
			}
			PEASSERT(haveEffect, "No suitable effect for a single-instance draw");
			if (!haveEffect) return;
		}
	}
	else
	{
		pEffectsForRange = &pMeshCaller->m_zOnlyEffects[iRange];
		if (pEffectsForRange->m_size == 0)
			return;

		// Validate z-only effects
		bool haveZOnlyEffect = true;
		for (unsigned int iPass = 0; iPass < pMeshCaller->m_zOnlyEffects[iRange].m_size; ++iPass)
		{
			if (!pMeshCaller->m_zOnlyEffects[iRange][iPass].isValid())
			{
				haveZOnlyEffect = false;
				break;
			}
		}
		if (!haveZOnlyEffect)
			return;
	}

	Matrix4x4 evtProjectionViewWorldMatrix =
		pDrawEvent ? pDrawEvent->m_projectionViewTransform : pZOnlyDrawEvent->m_projectionViewTransform;
			
	GPUMaterialSet *pGpuMatSet = pMeshCaller->m_hMaterialSetGPU.getObject<GPUMaterialSet>();
	GPUMaterial &curMat = pGpuMatSet->m_materials[iRange];
		
	for (PrimitiveTypes::UInt32 iEffect = 0; iEffect < pEffectsForRange->m_size; ++iEffect)
	{
		Handle hEffect = (*pEffectsForRange)[iEffect];
		Effect *pEffect = hEffect.getObject<Effect>();
		
		int maxInstancesPerDrawCall = hasJointSegments ? PE_MAX_SKINED_INSTANCE_COUNT_IN_DRAW_CALL : PE_MAX_INSTANCE_COUNT_IN_DRAW_CALL;
		#if PE_API_IS_D3D11
			if (pEffect->m_CS && hasJointSegments)
				maxInstancesPerDrawCall = PE_MAX_SKINED_INSTANCE_COUNT_IN_COMPUTE_CALL;
		#endif

		int instancePasses = 1;
		if (haveInstancesAndInstanceEffect)
		{
			instancePasses = (pMeshCaller->m_numVisibleInstances + maxInstancesPerDrawCall - 1) / maxInstancesPerDrawCall;
		}

		if (pDrawEvent && pEffect->m_effectDrawOrder != pDrawEvent->m_drawOrder)
			continue;

		const int numRenderGroups = haveInstancesAndInstanceEffect ? instancePasses : pMeshCaller->m_numVisibleInstances;

		// Tracks which instance index we try next (can skip culled ones).
		int iSrcInstance = 0;

		for (int iRenderGroup = 0; iRenderGroup < numRenderGroups; ++iRenderGroup)
		{
			Handle hLodIB = hIBuf;
			IndexBufferGPU *pLodibGPU = pibGPU;

			Handle hLODVB[4];
			PEASSERT(vbCount < 4, "Too many vertex buffers");
			for (int ivbuf = 0; ivbuf < vbCount; ++ivbuf)
				hLODVB[ivbuf] = pHVBs[ivbuf];

			int numInstancesInGroup = 1;
			if (haveInstancesAndInstanceEffect)
			{
				numInstancesInGroup = (iRenderGroup < instancePasses - 1)
					? maxInstancesPerDrawCall
					: (pMeshCaller->m_numVisibleInstances % maxInstancesPerDrawCall);

				if (!numInstancesInGroup) numInstancesInGroup = maxInstancesPerDrawCall;

				// Choose LOD for large instance groups (optional)
				if (iRenderGroup > 0 && pMeshCaller->m_lods.m_size)
				{
					Mesh *pLodMesh = pMeshCaller->m_lods[0].getObject<Mesh>();
					hLodIB = pLodMesh->m_hIndexBufferGPU;
					pLodibGPU = hLodIB.getObject<IndexBufferGPU>();
					
					PEASSERT(vbCount == pLodMesh->m_vertexBuffersGPUHs.m_size, "VB count mismatch for LOD");
					for (int ivbuf = 0; ivbuf < vbCount; ++ivbuf)
						hLODVB[ivbuf] = pLodMesh->m_vertexBuffersGPUHs[ivbuf];
				}
			}

			PrimitiveTypes::UInt32 numJointSegments = hasJointSegments ? ir.m_boneSegments.m_size : 1;
			if (g_disableSkinRender && hasJointSegments)
				numJointSegments = 0;

			int iSrcInstanceInBoneSegment = 0;

			for (PrimitiveTypes::UInt32 _iBoneSegment = 0; _iBoneSegment < numJointSegments; ++_iBoneSegment)
			{
				PrimitiveTypes::UInt32 iBoneSegment = _iBoneSegment;
				if (g_iDebugBoneSegment >= 0 && g_iDebugBoneSegment < numJointSegments)
				{
					iBoneSegment = g_iDebugBoneSegment;
					if (_iBoneSegment) break;
				}

				// Reset instance cursor for this bone segment (we want the same instances).
				iSrcInstanceInBoneSegment = iSrcInstance;
				while (pMeshCaller->m_instances[iSrcInstanceInBoneSegment].getObject<MeshInstance>()->m_culledOut)
					++iSrcInstanceInBoneSegment;

				pDrawList->beginDrawCallRecord(curMat.m_dbgName);

				if (API_CHOOSE_DX11_DX9_OGL(pEffect->m_CS, NULL, NULL) == NULL)
				{
					// Non-CS path
					if (hLodIB.isValid())
						pDrawList->setIndexBuffer(hLodIB, pLodibGPU->m_indexRanges.m_size ? iRange : -1, hasJointSegments ? iBoneSegment : -1);
					else
						pDrawList->setIndexBuffer(Handle());

					for (int ivbuf = 0; ivbuf < vbCount; ++ivbuf)
						pDrawList->setVertexBuffer(hLODVB[ivbuf]);
				}
				else
				{
					// CS path
					pDrawList->setVertexBuffer(Handle());
					pDrawList->setIndexBuffer(Handle());
				}

				// Group size: either many (instancing) or single (non-instanced).
				pDrawList->setInstanceCount(numInstancesInGroup, 0);
				curMat.createShaderActions(pDrawList);
				pDrawList->setEffect(hEffect);

				if (!haveInstancesAndInstanceEffect) // non-instanced
				{
					addNonInstancedTechShaderActions(
						pMeshCaller, ir, iBoneSegment, iRenderGroup, iSrcInstanceInBoneSegment,
						hasJointSegments, pDrawList, pEffect, evtProjectionViewWorldMatrix,
						vbCount, vbWeights);
				}
				
				if (haveInstancesAndInstanceEffect) // instanced
				{
					if (hasJointSegments)
					{
						MeshInstance *pMeshInstance = pMeshCaller->getFirstComponent<MeshInstance>();
						#if PE_API_IS_D3D11		
						if (pEffect->m_CS)
						{
							pDrawList->setDispatchParams(Vector3(numInstancesInGroup, 1, 1));
							if (iEffect == 0)
								addSAs_InstancedAnimationCSMap(pDrawList, pMeshInstance, pMeshCaller, numInstancesInGroup, iSrcInstanceInBoneSegment);
							else
								addSAs_InstancedAnimationCSReduce(pDrawList, pMeshInstance);
						}
						if (!pEffect->m_CS)
						#endif
						{
							// Final VS/PS pass
							addSAa_InstancedAnimation_CSOnly_Pass2_and_CSCPU_Pass1_and_NoCS_Pass0(
								pDrawList, pMeshCaller, evtProjectionViewWorldMatrix,
								numInstancesInGroup, iSrcInstanceInBoneSegment);

							if (iEffect == 2)
							{
								addSAa_InstancedAnimation_CSOnly_Pass2(pDrawList);
							}
							else if (iEffect == 0)
							{
								addSAa_InstancedAnimation_NoCS_Pass0(
									pDrawList, pMeshCaller, evtProjectionViewWorldMatrix,
									numInstancesInGroup, iSrcInstanceInBoneSegment);
							}
						}
					}
				}
			}

			// Advance instance cursor (kept as in original flow).
			iSrcInstance = iSrcInstanceInBoneSegment + 1;
		}
	}
}

void SingleHandler_DRAW::addSAs_InstancedAnimationCSMap(
	DrawList *pDrawList, MeshInstance *pMeshInstance, Mesh *pMeshCaller,
	int numInstancesInGroup, int indexInInstanceList)
{
#if PE_API_IS_D3D11
	// Compute shader: Map Pass (per-joint write)
	AnimSetBufferGPU::createShaderValueForCSMapUAV(*m_pContext, m_arena, pDrawList);

	PEASSERT(numInstancesInGroup <= PE_MAX_SKINED_INSTANCE_COUNT_IN_DRAW_CALL, "Too many skinned instances");
	int instanceIndexInTotal = indexInInstanceList;
	for (int iinst = 0; iinst < numInstancesInGroup; ++instanceIndexInTotal)
	{
		PEASSERT(instanceIndexInTotal < pMeshCaller->m_instances.m_size, "Invalid instance index");

		MeshInstance *pInst = pMeshCaller->m_instances[instanceIndexInTotal].getObject<MeshInstance>();
		if (pInst->m_culledOut)
			continue;

		SkeletonInstance *pParentSkleInstance = pInst->getFirstParentByTypePtr<SkeletonInstance>();
		DefaultAnimationSM *pAnimSM = pParentSkleInstance->getFirstComponent<DefaultAnimationSM>();

		// Set CS job index for the animation SM.
		pAnimSM->setInstancedCSJobIndex(iinst);
		++iinst;
	}

	SkeletonInstance *pSkelInst = pMeshInstance->getFirstParentByTypePtr<SkeletonInstance>();
	Skeleton *pSkelObj = pSkelInst->getFirstParentByTypePtr<Skeleton>();
	if (pSkelInst->m_hAnimationSetGPUs[0].isValid())
	{
		pSkelInst->m_hAnimationSetGPUs[0].getObject<AnimSetBufferGPU>()->createShaderValue(pDrawList);
	}
#else
	PEASSERT(false, "Compute Shaders are only supported on D3D11");
#endif
}

void SingleHandler_DRAW::addSAs_InstancedAnimationCSReduce(DrawList *pDrawList, MeshInstance *pSkinCaller)
{
#if PE_API_IS_D3D11
	AnimSetBufferGPU::createShaderValueForVSViewOfCSMap(*m_pContext, m_arena, pDrawList);
	SkeletonInstance *pSkelInst = pSkinCaller->getFirstParentByTypePtr<SkeletonInstance>();
	Skeleton *pSkel = pSkelInst->getFirstParentByTypePtr<Skeleton>();
	if (pSkelInst->m_hAnimationSetGPUs[0].isValid())
	{
		pSkel->addSA_SkeletonStrucuture(pDrawList);
		pSkel->addSA_SkeletonBindInverses(pDrawList);
	}
	AnimSetBufferGPU::createShaderValueForCSReduceUAV(*m_pContext, m_arena, pDrawList);
#else
	PEASSERT(false, "Compute Shaders are only supported on D3D11");
#endif
}

void SingleHandler_DRAW::addSAa_InstancedAnimation_CSOnly_Pass2_and_CSCPU_Pass1_and_NoCS_Pass0(
	DrawList *pDrawList, Mesh *pMeshCaller,
	Matrix4x4 &evtProjectionViewWorldMatrix, int numInstancesInGroup, int indexInInstanceList)
{
#if PE_API_IS_D3D11
	// Instance controls
	{
		Handle &hsvInstanceControl = pDrawList->nextShaderValue();
		hsvInstanceControl = Handle("RAW_DATA", sizeof(SetInstanceControlConstantsShaderAction));
		SetInstanceControlConstantsShaderAction *psvInstanceControl =
			new(hsvInstanceControl) SetInstanceControlConstantsShaderAction();
		psvInstanceControl->m_data.m_instanceIdOffset = indexInInstanceList;
	}

	// Per-instance transform buffer
	{
		Handle &hsvPerObject = pDrawList->nextShaderValue();
		hsvPerObject = Handle("RAW_DATA", sizeof(SA_SetAndBind_ConstResource_PerInstanceData));
		SA_SetAndBind_ConstResource_PerInstanceData *psvPerObject =
			new(hsvPerObject) SA_SetAndBind_ConstResource_PerInstanceData();

		psvPerObject->m_numInstances = numInstancesInGroup;
		memset(&psvPerObject->m_data, 0,
			sizeof(SA_SetAndBind_ConstResource_PerInstanceData::PerObjectInstanceData) * numInstancesInGroup);
				
		for (int iInst = 0; iInst < numInstancesInGroup; ++indexInInstanceList)
		{
			MeshInstance *pInst = pMeshCaller->m_instances[indexInInstanceList].getObject<MeshInstance>();
			if (pInst->m_culledOut)
				continue;
            
			Handle hParentSN = pInst->getFirstParentByType<SceneNode>();
			SkeletonInstance *pParentSkelInstance = NULL;
			if (!hParentSN.isValid())
			{
				if (pParentSkelInstance = pInst->getFirstParentByTypePtr<SkeletonInstance>())
					hParentSN = pParentSkelInstance->getFirstParentByType<SceneNode>();
			}
			PEASSERT(hParentSN.isValid(), "Each instance must have a SceneNode parent");
				
			const Matrix4x4 &worldMatrix = hParentSN.getObject<SceneNode>()->m_worldTransform;

			// Store W in 12 floats to reduce bandwidth (shader reconstructs 4th row/col).
			psvPerObject->m_data.gInstanceData[iInst].W[0] = Vector3(worldMatrix.m16[0],  worldMatrix.m16[1],  worldMatrix.m16[2]);
			psvPerObject->m_data.gInstanceData[iInst].W[1] = Vector3(worldMatrix.m16[3],  worldMatrix.m16[4],  worldMatrix.m16[5]);
			psvPerObject->m_data.gInstanceData[iInst].W[2] = Vector3(worldMatrix.m16[6],  worldMatrix.m16[7],  worldMatrix.m16[8]);
			psvPerObject->m_data.gInstanceData[iInst].W[3] = Vector3(worldMatrix.m16[9],  worldMatrix.m16[10], worldMatrix.m16[11]);
            
			++iInst;
		}
	}
#else
	PEASSERT(false, "Compute Shaders are only supported on D3D11");
#endif
}

void SingleHandler_DRAW::addNonInstancedTechShaderActions(
	Mesh *pMeshCaller, IndexRange &ir, int iBoneSegment, int /*iRenderGroup*/, int iSrcInstance,
	bool hasBoneSegments, DrawList *pDrawList, Effect *pEffect, const Matrix4x4 &evtProjectionViewWorldMatrix,
	int vbCount, Vector4 vbWeights)
{
	MeshInstance *pInst = pMeshCaller->m_instances[iSrcInstance].getObject<MeshInstance>();
	PEASSERT(!pInst->m_culledOut, "Should not submit culled instances");

	PEASSERT(API_CHOOSE_DX11_DX9_OGL(pEffect->m_CS, NULL, NULL) == NULL,
		"Non-instanced path doesn't support CS");

	Handle &hsvPerObject = pDrawList->nextShaderValue();
	hsvPerObject = Handle("RAW_DATA", sizeof(SetPerObjectConstantsShaderAction));
	SetPerObjectConstantsShaderAction *psvPerObject =
		new(hsvPerObject) SetPerObjectConstantsShaderAction();

	memset(&psvPerObject->m_data, 0, sizeof(SetPerObjectConstantsShaderAction::Data));

	Handle hParentSN = pInst->getFirstParentByType<SceneNode>();
	SkeletonInstance *pParentSkelInstance = NULL;
	if (!hParentSN.isValid())
	{
		if (pParentSkelInstance = pInst->getFirstParentByTypePtr<SkeletonInstance>())
			hParentSN = pParentSkelInstance->getFirstParentByType<SceneNode>();
	}
	PEASSERT(hParentSN.isValid(), "Each instance must have a SceneNode parent");

	const Matrix4x4 &worldMatrix = hParentSN.getObject<SceneNode>()->m_worldTransform;

	// Per-object transforms (non-instanced)
	psvPerObject->m_data.gWVP       = evtProjectionViewWorldMatrix * worldMatrix;
	psvPerObject->m_data.gWVPInverse= psvPerObject->m_data.gWVP.inverse();
	psvPerObject->m_data.gW         = worldMatrix;

	// Blend-shape weights (when multiple VBs present)
	if (vbCount > 1)
	{
		psvPerObject->m_data.gVertexBufferWeights = vbWeights;
	}

	if (hasBoneSegments)
	{
		#if APIABSTRACTION_D3D11
			psvPerObject->m_useBones = true;

			Handle &hsvAnim = pDrawList->nextShaderValue();
			hsvAnim = Handle("RAW_DATA", sizeof(SA_SetAndBind_ConstResource_SingleObjectAnimationPalette));
			SA_SetAndBind_ConstResource_SingleObjectAnimationPalette *pAnimPal =
				new(hsvAnim) SA_SetAndBind_ConstResource_SingleObjectAnimationPalette(*m_pContext, m_arena);

			DefaultAnimationSM *pAnimSM = pParentSkelInstance->getFirstComponent<DefaultAnimationSM>();
			PEASSERT(pAnimSM->m_curPalette.m_size > 0 && pAnimSM->m_curPalette.m_size <= PE_MAX_BONE_COUNT_IN_DRAW_CALL,
				"Invalid matrix palette size");
			memcpy(&pAnimPal->m_data.gJoints[0], &(pAnimSM->m_curPalette[0]),
				sizeof(Matrix4x4) * pAnimSM->m_curPalette.m_size);
		#else
			DefaultAnimationSM *pAnimSM = pParentSkelInstance ? pParentSkelInstance->getFirstComponent<DefaultAnimationSM>() : NULL;
			if (pAnimSM)
			{
				psvPerObject->m_useBones = true;
				IndexRange::BoneSegment &bs = ir.m_boneSegments[iBoneSegment];
				Matrix4x4 *curPalette = pAnimSM->m_curPalette.getFirstPtr();

				const int count = (int)(bs.m_boneSegmentBones.m_size > PE_MAX_BONE_COUNT_IN_DRAW_CALL
					? PE_MAX_BONE_COUNT_IN_DRAW_CALL : bs.m_boneSegmentBones.m_size);
				for (int ibone = 0; ibone < count; ++ibone)
				{
					if (pAnimSM->m_curPalette.m_size > 0)
					{
						memcpy(&psvPerObject->m_data.gJoints[ibone],
							&(curPalette[bs.m_boneSegmentBones[ibone]]),
							sizeof(Matrix4x4));
					}
				}
			}
		#endif
	}
}

// ---------- Effect switching helpers ----------

PE_IMPLEMENT_SINGLETON_CLASS1(PESSEH_CHANGE_TO_DEBUG_SHADER, Component);

void PESSEH_CHANGE_TO_DEBUG_SHADER::do_CHANGE_TO_DEBUG_SHADER(Events::Event *pEvt)
{
	if (pEvt->m_prevDistributor.isValid())
	{
		Component *pCaller = pEvt->m_prevDistributor.getObject<Component>();
		if (pCaller->isInstanceOf(Mesh::GetClassId()))
		{
			Mesh *pMeshCaller = (Mesh *)pCaller;
			MeshHelpers::pushEffects(pMeshCaller);
			MeshHelpers::setPixelShadersOfTopEffects(*m_pContext, m_arena, pMeshCaller);
		}
	}
}

PE_IMPLEMENT_SINGLETON_CLASS1(PESSEH_POP_SHADERS, Component);

void PESSEH_POP_SHADERS::do_POP_SHADERS(Events::Event *pEvt)
{
	if (pEvt->m_prevDistributor.isValid())
	{
		Component *pCaller = pEvt->m_prevDistributor.getObject<Component>();
		if (pCaller->isInstanceOf(Mesh::GetClassId()))
		{
			Mesh *pMeshCaller = (Mesh *)pCaller;
			PrimitiveTypes::UInt32 numRanges, numFullSequences;
			MeshHelpers::analyzeTechniqueSequences(pMeshCaller, numRanges, numFullSequences);
			if (numFullSequences > 1)
			{
				MeshHelpers::popEffects(pMeshCaller);
			}
		}
	}
}

PE_IMPLEMENT_SINGLETON_CLASS1(PESSEH_DRAW_Z_ONLY, Component);

void PESSEH_DRAW_Z_ONLY::do_GATHER_DRAWCALLS_Z_ONLY(Events::Event * /*pEvt*/)
{
	assert(!"PESSEH_DRAW_Z_ONLY::operator () Not Implemented!");
}

}; // namespace Components
}; // namespace PE
