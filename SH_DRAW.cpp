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
#include "PrimeEngine/Scene/DebugRenderer.h"

#include "CameraSceneNode.h"
#include "CameraManager.h";
#include "SH_DRAW.h"
#include <cstdlib>
#include<String>

extern int g_disableSkinRender;
extern int g_iDebugBoneSegment;

namespace PE {
namespace Components {

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
	PE::IndexBufferGPU *pibGPU = pObj->m_hIndexBufferGPU.isValid() ? pObj->m_hIndexBufferGPU.getObject<IndexBufferGPU>() : 0;
	numRanges = getNumberOfRangeCalls(pibGPU);

	// there should be numRanges Technique sequences (each sequence is a sequence of passes)
	// if effects are pushed there will multiple of numranges of sequences
	
	PrimitiveTypes::UInt32 numSequences = pObj->m_effects.m_size;

	if (numSequences % numRanges != 0)
	{
		PEINFO("Mesh: WARNING: number of technique sequences is not multiple of ranges\n");
	}

	numFullSequences = numSequences / numRanges;
}

void MeshHelpers::pushEffects(Mesh *pObj)
{
	PrimitiveTypes::UInt32 numRanges;
	PrimitiveTypes::UInt32 numFullSequences;

	MeshHelpers::analyzeTechniqueSequences(pObj, numRanges, numFullSequences);

	// need to copy top technique right underneath it
	PrimitiveTypes::UInt32 insertionIndex = numRanges;

	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; iRange++)
	{
		// for all ranges
		pObj->m_effects.insert(pObj->m_effects[iRange], insertionIndex + iRange);
	}
}

void MeshHelpers::popEffects(Mesh *pObj)
{
	PrimitiveTypes::UInt32 numRanges;
	PrimitiveTypes::UInt32 numFullSequences;

	MeshHelpers::analyzeTechniqueSequences(pObj, numRanges, numFullSequences);

	// remove the top effect sequence
	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; iRange++)
	{
		pObj->m_effects.remove(0);
	}
}

void MeshHelpers::setPixelShadersOfTopEffects(PE::GameContext &context, PE::MemoryArena arena, Mesh *pObj)
{
	PrimitiveTypes::UInt32 numRanges;
	PrimitiveTypes::UInt32 numFullSequences;

	MeshHelpers::analyzeTechniqueSequences(pObj, numRanges, numFullSequences);

	// need to copy top technique right underneath it
	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; iRange++)
	{
		// for all ranges
		for (PrimitiveTypes::UInt32 iPass = 0; iPass < pObj->m_effects[iRange].m_size; iPass++)
		{
			Handle hEffect = pObj->m_effects[iRange][iPass];
			Effect &curEffect = *hEffect.getObject<Effect>();

			// this technique has to have a pixel shader since it is last in sequence
			// create a copy of effect
			Handle hNewEffect("EFFECT", sizeof(Effect));
			Effect *pNewEffect = new(hNewEffect) Effect(context, arena, hNewEffect);
			pNewEffect->loadFromCopy(curEffect);
			
			Handle hSubstPSTech = EffectManager::Instance()->m_pixelShaderSubstitutes[curEffect.m_psInputFamily];
			if (hSubstPSTech.isValid())
			{
				Effect *pSubstPSTech = hSubstPSTech.getObject<Effect>();
				pNewEffect->setPixelShader(*pSubstPSTech);
			}

			// substitute
			pObj->m_effects[iRange][iPass] = hNewEffect;
		}
	}
}

void MeshHelpers::setEffectOfTopEffectSecuence(Mesh *pObj, Handle hNewEffect)
{
	PrimitiveTypes::UInt32 numRanges;
	PrimitiveTypes::UInt32 numFullSequences;

	MeshHelpers::analyzeTechniqueSequences(pObj, numRanges, numFullSequences);

	// need to copy top technique right underneath it
	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; iRange++)
	{
		// for all ranges
		for (PrimitiveTypes::UInt32 iPass = 0; iPass < pObj->m_effects[iRange].m_size; iPass++)
		{
			pObj->m_effects[iRange][iPass] = hNewEffect;
		}
	}
}

void MeshHelpers::setZOnlyEffectOfTopEffectSecuence(Mesh *pObj, Handle hNewEffect)
{
	PrimitiveTypes::UInt32 numRanges, numFullSequences;

	MeshHelpers::analyzeTechniqueSequences(pObj, numRanges, numFullSequences);

	// need to copy top technique right underneath it
	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; iRange++)
	{
		// for all ranges
		for (PrimitiveTypes::UInt32 iPass = 0; iPass < pObj->m_effects[iRange].m_size; iPass++)
		{
			pObj->m_zOnlyEffects[iRange][iPass] = hNewEffect;
		}
	}
}

PE_IMPLEMENT_SINGLETON_CLASS1(SingleHandler_DRAW, Component);

int TestAABBPlane(Vector3 b[2], Vector3 p, float mag) {
	// Convert AABB to center-extents representation
	Vector3 c = (b[0] + b[1] ) * 0.5f; // Compute AABB center
	Vector3 e = b[0]  - c; // Compute positive extents

	// Compute the projection interval radius of b onto L(t) = b.c + t * p.n
	float r = e.m_x * abs(p.m_x) + e.m_y * abs(p.m_y) + e.m_z * abs(p.m_z);

	// Compute distance of box center from plane
	float s = p.dotProduct(c);

	// Intersection occurs when distance s falls within [-r,+r] interval
	return abs(s) <= r;
}

void SingleHandler_DRAW::do_GATHER_DRAWCALLS(Events::Event *pEvt)
{
	// the mesh is who we are drawing for is previous distributor
	// and this object is current distributor
	Component *pCaller = pEvt->m_prevDistributor.getObject<Component>();
	Mesh *pMeshCaller = (Mesh *)pCaller;
	if (pMeshCaller->m_instances.m_size == 0)
		return; // no instances of this mesh
	Events::Event_GATHER_DRAWCALLS *pDrawEvent = NULL;
	Events::Event_GATHER_DRAWCALLS_Z_ONLY *pZOnlyDrawEvent = NULL;

	if (pEvt->isInstanceOf<Events::Event_GATHER_DRAWCALLS>())
		pDrawEvent = (Events::Event_GATHER_DRAWCALLS *)(pEvt);
		
	else
		pZOnlyDrawEvent = (Events::Event_GATHER_DRAWCALLS_Z_ONLY *)(pEvt);

    pMeshCaller->m_numVisibleInstances = pMeshCaller->m_instances.m_size; // assume all instances are visible
	for (int iInst = 0; iInst < pMeshCaller->m_instances.m_size; ++iInst)
	{
		MeshInstance* pInst = pMeshCaller->m_instances[iInst].getObject<MeshInstance>();
		if (!pInst->m_culledOut) {
		
		Matrix3x3 orientationMatrix = Matrix3x3(pInst->m_u, pInst->m_v, pInst->m_n);
		Matrix4x4 transformationMatrix = Matrix4x4(orientationMatrix, pInst->m_pos);
			DebugRenderer::Instance()->createBoundingBox(pMeshCaller->m_AABBCoords, transformationMatrix);
		}
		
	}
	
	CameraSceneNode* pcam = CameraManager::Instance()->getActiveCamera()->getCamSceneNode();
	Vector3 nearNormal = Vector3(pcam->planes[0].m_x, pcam->planes[0].m_y, pcam->planes[0].m_z);
	Vector3 farNormal = Vector3(pcam->planes[1].m_x, pcam->planes[1].m_y, pcam->planes[1].m_z);
	Vector3 leftNormal = Vector3(pcam->planes[2].m_x, pcam->planes[2].m_y, pcam->planes[2].m_z);
	Vector3 rightNormal = Vector3(pcam->planes[3].m_x, pcam->planes[3].m_y, pcam->planes[3].m_z);
	Vector3 upNormal = Vector3(pcam->planes[4].m_x, pcam->planes[4].m_y, pcam->planes[4].m_z);
	Vector3 downNormal = Vector3(pcam->planes[5].m_x, pcam->planes[5].m_y, pcam->planes[5].m_z);

    if (false && pMeshCaller->m_performBoundingVolumeCulling)
    {
        pMeshCaller->m_numVisibleInstances = 0;
        
        for (int iInst = 0; iInst < pMeshCaller->m_instances.m_size; ++iInst)
        {
            MeshInstance *pInst = pMeshCaller->m_instances[iInst].getObject<MeshInstance>();
 
			bool increase = false;
			for (int i = 0; i < 8; i++) {

				float nearDistance = nearNormal.dotProduct(pMeshCaller->m_AABBCoords[i] + pInst->m_pos) + pcam->planes[0].m_w;
				float farDistance = farNormal.dotProduct(pMeshCaller->m_AABBCoords[i] + pInst->m_pos) + pcam->planes[1].m_w;
				float leftDistance = leftNormal.dotProduct(pMeshCaller->m_AABBCoords[i] + pInst->m_pos) + pcam->planes[2].m_w;
				float rightDistance = rightNormal.dotProduct(pMeshCaller->m_AABBCoords[i] + pInst->m_pos) + pcam->planes[3].m_w;
				float upDistance = upNormal.dotProduct(pMeshCaller->m_AABBCoords[i] + pInst->m_pos) + pcam->planes[4].m_w;
				float downDistance = downNormal.dotProduct(pMeshCaller->m_AABBCoords[i] + pInst->m_pos) + pcam->planes[5].m_w;
				
				if (farDistance > 0 && nearDistance < 0 && leftDistance > 0 && rightDistance < 0 && downDistance > 0 && upDistance < 0) {
		
					pInst->m_culledOut = false;
					increase = true;
					break;
				}
				else {
					pInst->m_culledOut = true;
				}
			}
			if (increase) {
				pMeshCaller->m_numVisibleInstances++;
			}
			
        }
    }
    

	DrawList *pDrawList = pDrawEvent ? DrawList::Instance() : DrawList::ZOnlyInstance();
	
    //dbg
    //SceneNode *pRoot = RootSceneNode::Instance();

	// index buffer
	Handle hIBuf = pMeshCaller->m_hIndexBufferGPU;
	IndexBufferGPU *pibGPU = hIBuf.getObject<IndexBufferGPU>();

	// Check for vertex buffer(s)
	Handle hVertexBuffersGPU[4]; // list of bufers to pass to GPU
	Vector4 vbufWeights;
	int numVBufs = pMeshCaller->m_vertexBuffersGPUHs.m_size;
	assert(numVBufs < 4);
	for (int ivbuf = 0; ivbuf < numVBufs; ivbuf++)
	{
		hVertexBuffersGPU[ivbuf] = pMeshCaller->m_vertexBuffersGPUHs[ivbuf];
		vbufWeights.m_values[ivbuf] = hVertexBuffersGPU[ivbuf].getObject<VertexBufferGPU>()->m_weight;
	}

	if (numVBufs > 1)
	{
		for (int ivbuf = numVBufs; ivbuf < 4; ivbuf++)
		{
			hVertexBuffersGPU[ivbuf] = hVertexBuffersGPU[0];
			vbufWeights.m_values[ivbuf] = vbufWeights.m_values[0];
		}
		numVBufs = 4; // blend shape shader works with 4 shapes. so we extend whatever slots with base
	}

	// Check for material set
	if (!pMeshCaller->m_hMaterialSetGPU.isValid())
		return;

	GPUMaterialSet *pGpuMatSet = pMeshCaller->m_hMaterialSetGPU.getObject<GPUMaterialSet>();

	Matrix4x4 projectionViewWorldMatrix = pDrawEvent ? pDrawEvent->m_projectionViewTransform : pZOnlyDrawEvent->m_projectionViewTransform;

	Handle hParentSN = pCaller->getFirstParentByType<SceneNode>();
	if (!hParentSN.isValid())
	{
		// allow skeleton to be in chain
		 hParentSN = pCaller->getFirstParentByTypePtr<SkeletonInstance>()->getFirstParentByType<SceneNode>();
	}
	Matrix4x4 worldMatrix;
	worldMatrix.loadIdentity();

	if (hParentSN.isValid())
		worldMatrix = hParentSN.getObject<SceneNode>()->m_worldTransform;
	
	projectionViewWorldMatrix = projectionViewWorldMatrix * worldMatrix;

	// draw all pixel ranges with different materials
	PrimitiveTypes::UInt32 numRanges = MeshHelpers::getNumberOfRangeCalls(pibGPU);
	
	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; iRange++)
	{
		gatherDrawCallsForRange(pMeshCaller, pDrawList, &hVertexBuffersGPU[0], numVBufs, vbufWeights, iRange, pDrawEvent, pZOnlyDrawEvent);
	}
}

void SingleHandler_DRAW::gatherDrawCallsForRange(Mesh *pMeshCaller, DrawList *pDrawList,  PE::Handle *pHVBs, int vbCount, Vector4 &vbWeights, 
		int iRange,
		Events::Event_GATHER_DRAWCALLS *pDrawEvent, Events::Event_GATHER_DRAWCALLS_Z_ONLY *pZOnlyDrawEvent)
{
	// we might have several passes (several effects) so we need to check which effect list to use
	PEStaticVector<PE::Handle, 4> *pEffectsForRange = NULL;
	bool haveInstancesAndInstanceEffect = false;
	
	Handle hIBuf = pMeshCaller->m_hIndexBufferGPU;
	IndexBufferGPU *pibGPU = hIBuf.getObject<IndexBufferGPU>();
	
	IndexRange &ir = pibGPU->m_indexRanges[iRange];
	bool hasJointSegments = ir.m_boneSegments.m_size > 0; //true means a skinned mesh, otherwise static mesh
	
	if (pDrawEvent)
	{
		//if instance count is 1, then regular effects are preferred
		//if instance count is > 1 then, if instance effect is available use it, otherwise use normal effect instanceCount times

		//first of all, return if have no appropriate effects
		if (pMeshCaller->m_effects[iRange].m_size == 0 && pMeshCaller->m_instanceEffects[iRange].m_size == 0) // this effect does not render in normal passes
		{
			return;
		}

		pEffectsForRange = &pMeshCaller->m_effects[iRange];
		if (pMeshCaller->m_numVisibleInstances > 1)
		{
			// try to use effect for instances, otherwise will have to render objects one at a time
			haveInstancesAndInstanceEffect = pMeshCaller->m_instanceEffects[iRange].m_size > 0;
			for (unsigned int iPass = 0; iPass < pMeshCaller->m_instanceEffects[iRange].m_size; ++iPass)
			{
				if (!pMeshCaller->m_instanceEffects[iRange][iPass].isValid())
				{
					haveInstancesAndInstanceEffect = false;
					break;
				}
			}
			if (haveInstancesAndInstanceEffect)
			{
				pEffectsForRange = &pMeshCaller->m_instanceEffects[iRange];
			}
		}
		else
		{
			// check that have normal (non-instance) effects that are all valid
			// try to use effect for instances, otherwise will have to render objects one at a time
			bool haveEffect = pMeshCaller->m_effects[iRange].m_size > 0;
			for (unsigned int iPass = 0; iPass < pMeshCaller->m_effects[iRange].m_size; ++iPass)
			{
				if (!pMeshCaller->m_effects[iRange][iPass].isValid())
				{
					haveEffect = false;
					break;
				}
			}
			PEASSERT(haveEffect, "Can't find an effect to render asset with. Note there is only one instance of the asset " \
				"and default behavior is to use no instance version of effect. Potentially we could add code to use instanced version of effect even if there is only one instance rendered");
			if (!haveEffect)
			{
				// some are not valid or don't have effects at all
				return;
			}
		}
	}
	else
	{
		pEffectsForRange = &pMeshCaller->m_zOnlyEffects[iRange];
		if (pEffectsForRange->m_size == 0)
			return; // this mesh does not have even a single pass for depth only

		haveInstancesAndInstanceEffect = false; // right now we don't have z-only instance effects, so will have to render each object individually

		// check that all zonly passes are valid
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
		{
			// don't have a single effect to process this range
			return;
		}
	}

	Matrix4x4 evtProjectionViewWorldMatrix = pDrawEvent ? pDrawEvent->m_projectionViewTransform : pZOnlyDrawEvent->m_projectionViewTransform;
	
			
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
		{
			continue; // skip this pass if doesn't fit drawOrder
		}

		int numRenderGroups = haveInstancesAndInstanceEffect ? instancePasses : pMeshCaller->m_numVisibleInstances;

        // each render group is group of instances that can be submitted at once
        // when there is no instancing effect
        int iSrcInstance = 0; // tracks next instance index to be submitted. we might skip meshes based on culling
        // so this value might not be equal to iRender group even for non-instanced case
		for (int iRenderGroup = 0; iRenderGroup < numRenderGroups; ++iRenderGroup)
		{
			Handle hLodIB = hIBuf;
			IndexBufferGPU *pLodibGPU = pibGPU;
			Handle hLODVB[4];
			PEASSERT(vbCount < 4, "Too many vbs");
			for (int ivbuf = 0; ivbuf < vbCount; ivbuf++)
			{
				hLODVB[ivbuf] = pHVBs[ivbuf];
			}

			int numInstancesInGroup = 1;
			if (haveInstancesAndInstanceEffect)
			{
				numInstancesInGroup = (iRenderGroup < instancePasses - 1) ? (maxInstancesPerDrawCall) : (pMeshCaller->m_numVisibleInstances % maxInstancesPerDrawCall);
				if (!numInstancesInGroup) numInstancesInGroup = maxInstancesPerDrawCall; // mod equals to 0 when we have exact mutliple of instances

				// choose lod
				if (iRenderGroup > 0 && pMeshCaller->m_lods.m_size)
				{
					Mesh *pLodMesh = pMeshCaller->m_lods[0].getObject<Mesh>();
					hLodIB = pLodMesh->m_hIndexBufferGPU;
					pLodibGPU = hLodIB.getObject<IndexBufferGPU>();
					
					PEASSERT(vbCount == pLodMesh->m_vertexBuffersGPUHs.m_size, "inconsistent vb amounts");
									
					for (int ivbuf = 0; ivbuf < vbCount; ivbuf++)
					{
						hLODVB[ivbuf] = pLodMesh->m_vertexBuffersGPUHs[ivbuf];
					}
				}
			}

			PrimitiveTypes::UInt32 numJointSegments = hasJointSegments ? ir.m_boneSegments.m_size : 1;
			
			if (g_disableSkinRender && hasJointSegments)
				numJointSegments = 0;

			// joint segment represents a piece of geometry within current material patch that is affected by same bone subset
			// in case of rigid mesh, there is only one join segment (and has no joints) for convenience
			// in d3d11 we can fit whole skeleton into one bone segment, so there is usually only one bone segment in d3d11
            
            int iSrcInstanceInBoneSegment = 0;
			for (PrimitiveTypes::UInt32 _iBoneSegment = 0; _iBoneSegment < numJointSegments; _iBoneSegment++)
			{
				PrimitiveTypes::UInt32 iBoneSegment = _iBoneSegment;
				if (g_iDebugBoneSegment >= 0 && g_iDebugBoneSegment < numJointSegments)
				{
					iBoneSegment = g_iDebugBoneSegment;
					if (_iBoneSegment) break;
				}

                iSrcInstanceInBoneSegment = iSrcInstance; // reset instance id for each bone segment since we want to process same instances
                while(pMeshCaller->m_instances[iSrcInstanceInBoneSegment].getObject<MeshInstance>()->m_culledOut)
                    ++iSrcInstanceInBoneSegment;
                
				pDrawList->beginDrawCallRecord(curMat.m_dbgName);

				if (API_CHOOSE_DX11_DX9_OGL(pEffect->m_CS, NULL, NULL) == NULL)
				{
					// if not compute shader:
					if (hLodIB.isValid())
						pDrawList->setIndexBuffer(hLodIB, pLodibGPU->m_indexRanges.m_size ? iRange : -1, hasJointSegments ? iBoneSegment : -1);
					else
						pDrawList->setIndexBuffer(Handle());

					for (int ivbuf = 0; ivbuf < vbCount; ivbuf++)
					{
						pDrawList->setVertexBuffer(hLODVB[ivbuf]);
					}
				}
				else
				{
					// if compute shader
					pDrawList->setVertexBuffer(Handle());
					pDrawList->setIndexBuffer(Handle());
				}

				// if we have instance effect, we will render all instances, otherwise we render only one
				pDrawList->setInstanceCount(numInstancesInGroup, 0);
					
				curMat.createShaderActions(pDrawList);
				pDrawList->setEffect(hEffect);

					
				if (!haveInstancesAndInstanceEffect)	//rendering each object separately
                {
					addNonInstancedTechShaderActions(pMeshCaller, ir, iBoneSegment, iRenderGroup, iSrcInstanceInBoneSegment, hasJointSegments, pDrawList, pEffect, evtProjectionViewWorldMatrix, vbCount, vbWeights);
                }
                
				if (haveInstancesAndInstanceEffect) // instanced version
				{
					if (hasJointSegments)
					{
						//since we are here we know that mesh caller is actually a skin
						MeshInstance *pMeshInstance = pMeshCaller->getFirstComponent<MeshInstance>();
						#if PE_API_IS_D3D11		
						if (pEffect->m_CS)
						{
							pDrawList->setDispatchParams(Vector3(numInstancesInGroup, 1, 1));
							
							if (iEffect == 0)
							{
								addSAs_InstancedAnimationCSMap(pDrawList, pMeshInstance, pMeshCaller, numInstancesInGroup, iSrcInstanceInBoneSegment);
							}
							else
							{
								// this is compute shader for Reduce
								addSAs_InstancedAnimationCSReduce(pDrawList, pMeshInstance);
							}
						}
					
						if (!pEffect->m_CS)
						#endif
						{
							//this is final vertex+pixel shader pass
							addSAa_InstancedAnimation_CSOnly_Pass2_and_CSCPU_Pass1_and_NoCS_Pass0(pDrawList, pMeshCaller, evtProjectionViewWorldMatrix, numInstancesInGroup, iSrcInstanceInBoneSegment);
						
							if (iEffect == 1) 
							{
								PEASSERT(false, "Should not happen with current setup!");
							}
							else if (iEffect == 2)
							{
								addSAa_InstancedAnimation_CSOnly_Pass2(pDrawList);
							}
							else if (iEffect == 0)
							{ 
								addSAa_InstancedAnimation_NoCS_Pass0(pDrawList, pMeshCaller, evtProjectionViewWorldMatrix, numInstancesInGroup, iSrcInstanceInBoneSegment);
							}
						} //end if not compute shader
					} // end if has bone segments
				} // end if hasInstanceEffect
			} // for each bone segment (if no bone segment, just run the code once, for a non skinned mesh)
            
            iSrcInstance = iSrcInstanceInBoneSegment + 1;
		}

	} // loop through all effects for this iRange
}


void SingleHandler_DRAW::addSAs_InstancedAnimationCSMap(PE::Components::DrawList *pDrawList, PE::Components::MeshInstance *pMeshInstance,  PE::Components::Mesh *pMeshCaller, int numInstancesInGroup, int indexInInstanceList)
{
#if PE_API_IS_D3D11
	// this is compute shader for Map part of the computation of animation
	AnimSetBufferGPU::createShaderValueForCSMapUAV(*m_pContext, m_arena, pDrawList); // compute shader result goes here. Compute shader Map : writing individual joints

	PEASSERT(numInstancesInGroup <= PE_MAX_SKINED_INSTANCE_COUNT_IN_DRAW_CALL, "Exceeding number of max skinned instances");
	int instanceIndexInTotal = indexInInstanceList;
	for (int iinst = 0; iinst < numInstancesInGroup; ++instanceIndexInTotal) 
	{
		PEASSERT(instanceIndexInTotal < pMeshCaller->m_instances.m_size, "wrong index for instance in list");

		MeshInstance *pInst = pMeshCaller->m_instances[instanceIndexInTotal].getObject<MeshInstance>();
		if (pInst->m_culledOut)
			continue;
		SkeletonInstance *pParentSkleInstance = pInst->getFirstParentByTypePtr<SkeletonInstance>();
		DefaultAnimationSM *pAnimSM = pParentSkleInstance->getFirstComponent<DefaultAnimationSM>();

		// setting index in anim sm so that it knows which part of result buffer to lookup.
		// this index could change from draw call to draw call
		pAnimSM->setInstancedCSJobIndex(iinst);

		// todo : also add per anim sm controls to some gpu buffer so we know which anim to lookup and at what frame

		++iinst;
	}

	SkeletonInstance *pSkelInst = pMeshInstance->getFirstParentByTypePtr<SkeletonInstance>();
	Skeleton *pSkelObj = pSkelInst->getFirstParentByTypePtr<Skeleton>();
	if (pSkelInst->m_hAnimationSetGPUs[0].isValid())
	{
		// set animation data base
		pSkelInst->m_hAnimationSetGPUs[0].getObject<AnimSetBufferGPU>()->createShaderValue(pDrawList);
		SkeletonCPU *pSkelCPU = pSkelObj->m_hSkeletonCPU.getObject<SkeletonCPU>();
		// also need to create a shader value to set bind pose
		//Handle &hsvPerObjectBindPose = pDrawList->nextShaderValue();
		//hsvPerObjectBindPose = Handle("RAW_DATA", sizeof(SetPerObjectAnimationConstantsShaderAction));
		//SetPerObjectAnimationConstantsShaderAction *psvPerObjectBindPose = new(hsvPerObjectBindPose) SetPerObjectAnimationConstantsShaderAction(SetPerObjectAnimationConstantsShaderAction::AnimationBufferSlots_AnimationBufferBindPosePaletteTBuf);

		//PEASSERT(pSkelCPU->m_numJoints > 0 && pSkelCPU->m_numJoints <= PE_MAX_BONE_COUNT_IN_DRAW_CALL, "empty or too big matrix palette");
		//memcpy(&psvPerObjectBindPose->m_data.gInstancedAnimationBones[0], pSkelCPU->getBindInversesPtr(), sizeof(Matrix4x4) * pSkelCPU->m_numJoints);	
	}
#else
	PEASSERT(false, "Compute Shaders Not Supported for any api rather than d3d11");
#endif
}

void SingleHandler_DRAW::addSAs_InstancedAnimationCSReduce(PE::Components::DrawList *pDrawList, PE::Components::MeshInstance *pSkinCaller)
{
#if PE_API_IS_D3D11
	AnimSetBufferGPU::createShaderValueForVSViewOfCSMap(*m_pContext, m_arena, pDrawList); // view results of CS Map pass
	SkeletonInstance *pSkelInst = pSkinCaller->getFirstParentByTypePtr<SkeletonInstance>();
	Skeleton *pSkel = pSkelInst->getFirstParentByTypePtr<Skeleton>();
	if (pSkelInst->m_hAnimationSetGPUs[0].isValid())
	{
		pSkel->addSA_SkeletonStrucuture(pDrawList);
		pSkel->addSA_SkeletonBindInverses(pDrawList);
	}

	AnimSetBufferGPU::createShaderValueForCSReduceUAV(*m_pContext, m_arena, pDrawList); // UAV for CS Reduce pass
#else
	PEASSERT(false, "Compute Shaders Not Supported fro any api rather than d3d11");
#endif
}

/// Adds these shader actions to DrawList
/// SetInstanceControlConstantsShaderAction
/// SetInstancesTransformsConstantsShaderAction
/// 
void SingleHandler_DRAW::addSAa_InstancedAnimation_CSOnly_Pass2_and_CSCPU_Pass1_and_NoCS_Pass0(PE::Components::DrawList *pDrawList, PE::Components::Mesh *pMeshCaller,
	Matrix4x4 &evtProjectionViewWorldMatrix, int numInstancesInGroup, int indexInInstanceList)
{
#if PE_API_IS_D3D11
	//InstanceControl
	{
		Handle &hsvInstanceControl = pDrawList->nextShaderValue();
		hsvInstanceControl = Handle("RAW_DATA", sizeof(SetInstanceControlConstantsShaderAction));
		SetInstanceControlConstantsShaderAction *psvInstanceControl = new(hsvInstanceControl) SetInstanceControlConstantsShaderAction();
		psvInstanceControl->m_data.m_instanceIdOffset = indexInInstanceList;
	}

	// use this cbuffer for per-instance data
	{
		Handle &hsvPerObject = pDrawList->nextShaderValue();
		hsvPerObject = Handle("RAW_DATA", sizeof(SA_SetAndBind_ConstResource_PerInstanceData));
		SA_SetAndBind_ConstResource_PerInstanceData *psvPerObject = new(hsvPerObject) SA_SetAndBind_ConstResource_PerInstanceData();

		psvPerObject->m_numInstances = numInstancesInGroup;
		memset(&psvPerObject->m_data, 0, sizeof(SA_SetAndBind_ConstResource_PerInstanceData::PerObjectInstanceData) * numInstancesInGroup);
				
		for (int iInst = 0; iInst < numInstancesInGroup; ++indexInInstanceList)
		{
            MeshInstance *pInst = pMeshCaller->m_instances[indexInInstanceList].getObject<MeshInstance>();
            if (pInst->m_culledOut)
                continue;
            
			Handle hParentSN = pInst->getFirstParentByType<SceneNode>();

			SkeletonInstance *pParentSkelInstance = NULL;
			if (!hParentSN.isValid())
			{
				// allow skeleton to be in chain
				if (pParentSkelInstance = pInst->getFirstParentByTypePtr<SkeletonInstance>())
				{
					hParentSN = pParentSkelInstance->getFirstParentByType<SceneNode>();
				}
			}
			PEASSERT(hParentSN.isValid(), "Each instance must have a scene node parent");
				
			Matrix4x4 worldMatrix = hParentSN.getObject<SceneNode>()->m_worldTransform;
			Matrix4x4 projectionViewWorldMatrix = evtProjectionViewWorldMatrix * worldMatrix;
						
			//psvPerObject->m_data.gInstanceData[iInst].WVP = projectionViewWorldMatrix; // we dont do this to save on transferred memory
			psvPerObject->m_data.gInstanceData[iInst].W[0] = Vector3(worldMatrix.m16[0], worldMatrix.m16[1], worldMatrix.m16[2]);
			psvPerObject->m_data.gInstanceData[iInst].W[1] = Vector3(worldMatrix.m16[3], worldMatrix.m16[4], worldMatrix.m16[5]);
			psvPerObject->m_data.gInstanceData[iInst].W[2] = Vector3(worldMatrix.m16[6], worldMatrix.m16[7], worldMatrix.m16[8]);
			psvPerObject->m_data.gInstanceData[iInst].W[3] = Vector3(worldMatrix.m16[9], worldMatrix.m16[10], worldMatrix.m16[11]);
			//psvPerObject->m_data.gInstanceData[iInst].W = worldMatrix; // we dont do this, instead use approach above to store matix in 12 floats not 16
            
            ++iInst;
		}
	}
#else
	PEASSERT(false, "Compute Shaders Not Supported fro any api rather than d3d11");
#endif
}

/// Adds these actions to draw list:
/// SetPerObjectConstantsShaderAction (includes bones too for non-DX11)
/// For DX11: SetPerObjectAnimationConstantsShaderAction
void SingleHandler_DRAW::addNonInstancedTechShaderActions(Mesh *pMeshCaller, IndexRange &ir, int iBoneSegment, int iRenderGroup, int iSrcInstance, bool hasBoneSegments, DrawList *pDrawList, Effect *pEffect, const Matrix4x4 &evtProjectionViewWorldMatrix,
	int vbCount, Vector4 vbWeights)
{
	MeshInstance *pInst = pMeshCaller->m_instances[iSrcInstance].getObject<MeshInstance>();
    
    PEASSERT( !pInst->m_culledOut, "we should not be calling this method for culled out meshes");
    
	PEASSERT(API_CHOOSE_DX11_DX9_OGL(pEffect->m_CS, NULL, NULL) == NULL, "We dont support CS as part of non instanced rendering yet");
	Handle &hsvPerObject = pDrawList->nextShaderValue(); // create object referenced by Handle in DrawList, this handle will be released on end of draw call
	hsvPerObject = Handle("RAW_DATA", sizeof(SetPerObjectConstantsShaderAction));
	SetPerObjectConstantsShaderAction *psvPerObject = new(hsvPerObject) SetPerObjectConstantsShaderAction();

	memset(&psvPerObject->m_data, 0, sizeof(SetPerObjectConstantsShaderAction::Data));

    
	Handle hParentSN = pInst->getFirstParentByType<SceneNode>();
	SkeletonInstance *pParentSkelInstance = NULL;
	if (!hParentSN.isValid())
	{
		// allow skeleton to be in chain
		if (pParentSkelInstance = pInst->getFirstParentByTypePtr<SkeletonInstance>())
		{
			hParentSN = pParentSkelInstance->getFirstParentByType<SceneNode>();
		}
	}
	PEASSERT(hParentSN.isValid(), "Each instance must have a scene node parent");

	Matrix4x4 &m_worldTransform = hParentSN.getObject<SceneNode>()->m_worldTransform;

	Matrix4x4 worldMatrix = hParentSN.getObject<SceneNode>()->m_worldTransform;
					
	psvPerObject->m_data.gWVP = evtProjectionViewWorldMatrix * worldMatrix; // these values are only used by non-instance version
	psvPerObject->m_data.gWVPInverse = psvPerObject->m_data.gWVP.inverse(); // these values are only used by non-instance version

	psvPerObject->m_data.gW = worldMatrix;  // these values are only used by non-instance version
						
	// Set blend weights
	if (vbCount > 1)
	{
		psvPerObject->m_data.gVertexBufferWeights = vbWeights;
	}

	if (hasBoneSegments)
	{
        					
		#if APIABSTRACTION_D3D11
			//in d3d 11 we have only one bone segment and we set all bones
			// use the code below for storing full palette in cbuffer
			/*
			if (pMeshCaller->m_curPalette.m_size > 0)
			{
				PEASSERT(pMeshCaller->m_curPalette.m_size <= 128, "Matrix pallette is too big");
				memcpy(&psvPerObject->m_data.gJoints[0], &(pMeshCaller->m_curPalette[0]), sizeof(Matrix4x4) * pMeshCaller->m_curPalette.m_size);
			}
			*/

			psvPerObject->m_useBones = true;
		
			// this value is used by both normal and instanced version. stores either one skeleton palette or multiple
			Handle &hsvPerObject = pDrawList->nextShaderValue();
			hsvPerObject = Handle("RAW_DATA", sizeof(SA_SetAndBind_ConstResource_SingleObjectAnimationPalette));
			SA_SetAndBind_ConstResource_SingleObjectAnimationPalette *psvPerObject = new(hsvPerObject) SA_SetAndBind_ConstResource_SingleObjectAnimationPalette(*m_pContext, m_arena);

			DefaultAnimationSM *pAnimSM = pParentSkelInstance->getFirstComponent<DefaultAnimationSM>();
								
			PEASSERT(pAnimSM->m_curPalette.m_size > 0 && pAnimSM->m_curPalette.m_size <= PE_MAX_BONE_COUNT_IN_DRAW_CALL, "empty or too big matrix palette");
			memcpy(&psvPerObject->m_data.gJoints[0], &(pAnimSM->m_curPalette[0]), sizeof(Matrix4x4) * pAnimSM->m_curPalette.m_size);	

		#else
		
			DefaultAnimationSM *pAnimSM = pParentSkelInstance ? pParentSkelInstance->getFirstComponent<DefaultAnimationSM>() : NULL;
			
			if (pAnimSM)
			{
				psvPerObject->m_useBones = true;
		
				IndexRange::BoneSegment &bs = ir.m_boneSegments[iBoneSegment];
				Matrix4x4 *curPalette = pAnimSM->m_curPalette.getFirstPtr();
				for (int ibone = 0; ibone < (int)(bs.m_boneSegmentBones.m_size > PE_MAX_BONE_COUNT_IN_DRAW_CALL ? PE_MAX_BONE_COUNT_IN_DRAW_CALL : bs.m_boneSegmentBones.m_size); ibone++)
				{
					if (pAnimSM->m_curPalette.m_size > 0)
					{
						memcpy(&psvPerObject->m_data.gJoints[ibone], &(curPalette[bs.m_boneSegmentBones[ibone]]), sizeof(Matrix4x4));
					}
				}
			}
		#endif
	}	
}

/// Adds these shader actions to DrawList
/// SetInstanceControlConstantsShaderAction
void SingleHandler_DRAW::addSAa_InstancedAnimation_CSOnly_Pass2(PE::Components::DrawList *pDrawList)
{
#if PE_API_IS_D3D11
	// two CS passes have run so there is no CPU resolve, just use the result of CS resolve
	// can potentially use SA_CopyInto_SysMem_FromInstancedObjectsAnimationCSReduceResult
	// if need to copy results into system memory
	AnimSetBufferGPU::createShaderValueForVSViewOfCSReduce(*m_pContext, m_arena, pDrawList);
#endif
}

void SingleHandler_DRAW::addSAa_InstancedAnimation_NoCS_Pass0(PE::Components::DrawList *pDrawList, PE::Components::Mesh *pMeshCaller,
	Matrix4x4 &evtProjectionViewWorldMatrix, int numInstancesInGroup,  int indexInInstanceList)
{
#if PE_API_IS_D3D11
	// we dont use compute shaders, just copy anim data into buffer. assuming that cpu has calculated the animation
	Handle &hsvPerObject = pDrawList->nextShaderValue();
	hsvPerObject = Handle("RAW_DATA", sizeof(SA_SetAndBind_ConstResource_InstancedObjectsAnimationPalettes));
	SA_SetAndBind_ConstResource_InstancedObjectsAnimationPalettes *psvPerObject = new(hsvPerObject) SA_SetAndBind_ConstResource_InstancedObjectsAnimationPalettes();

	PEASSERT(numInstancesInGroup <= PE_MAX_SKINED_INSTANCE_COUNT_IN_DRAW_CALL, "Exceeding number of max skinned instances");
	int instanceIndexInTotal = indexInInstanceList;
	for (int iinst = 0; iinst < numInstancesInGroup; ++instanceIndexInTotal) 
	{
		PEASSERT(instanceIndexInTotal < pMeshCaller->m_instances.m_size, "wrong index for instance in list");

		MeshInstance *pInst = pMeshCaller->m_instances[instanceIndexInTotal].getObject<MeshInstance>();
        if (pInst->m_culledOut)
            continue;
        SkeletonInstance *pParentSkleInstance = pInst->getFirstParentByTypePtr<SkeletonInstance>();
		DefaultAnimationSM *pAnimSM = pParentSkleInstance->getFirstComponent<DefaultAnimationSM>();

		PEASSERT(pAnimSM->m_curPalette.m_size > 0 && pAnimSM->m_curPalette.m_size <= PE_MAX_BONE_COUNT_IN_DRAW_CALL, "empty or too big matrix palette");
		void *pData = &(pAnimSM->m_curPalette[0]);
		memcpy(&psvPerObject->m_data.gInstancedAnimationBones[iinst * PE_MAX_BONE_COUNT_IN_DRAW_CALL], pData, sizeof(Matrix4x4) * pAnimSM->m_curPalette.m_size);
        
        ++iinst;
	}
#endif
}

PE_IMPLEMENT_SINGLETON_CLASS1(PESSEH_CHANGE_TO_DEBUG_SHADER, Component);

void PESSEH_CHANGE_TO_DEBUG_SHADER::do_CHANGE_TO_DEBUG_SHADER(Events::Event *pEvt)
{
	// the mesh is who we are drawing for is previous distributor
	// and this object is current distributor
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
	// the mesh is who we are drawing for is previous distributor
	// and this object is current distributor
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
				// Make sure that there are enough effect sequences
				// effect sequence is a set of effects to draw all parts of the mesh
				// i.e. if mesh uses 3 materials, there are 3 parts. each part needs associated effect.
				// effect sequence would need to have 3 effects to be able to draw a mesh
				MeshHelpers::popEffects(pMeshCaller);
			}
			
		}
	}
	
}

PE_IMPLEMENT_SINGLETON_CLASS1(PESSEH_DRAW_Z_ONLY, Component);

void PESSEH_DRAW_Z_ONLY::do_GATHER_DRAWCALLS_Z_ONLY(Events::Event *pEvt)
{
    //dbg
    //Component *pCaller = pEvt->m_lastDistributor.getObject<Component>();
	//Mesh *pMeshCaller = (Mesh *)pCaller;

	assert(!"PESSEH_DRAW_Z_ONLY::operator () Not Implemented!");
}
}; // namespace Components
}; // namespace PE
