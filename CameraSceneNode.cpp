#include "CameraSceneNode.h"
#include "../Lua/LuaEnvironment.h"
#include "PrimeEngine/Events/StandardEvents.h"
#include <cmath>
#include "DebugRenderer.h"
#define Z_ONLY_CAM_BIAS 0.0f
namespace PE {
namespace Components {

PE_IMPLEMENT_CLASS1(CameraSceneNode, SceneNode);

CameraSceneNode::CameraSceneNode(PE::GameContext &context, PE::MemoryArena arena, Handle hMyself) : SceneNode(context, arena, hMyself)
{
	m_near = 0.05f;
	m_far = 2000.0f;
}
void CameraSceneNode::addDefaultComponents()
{
	Component::addDefaultComponents();
	PE_REGISTER_EVENT_HANDLER(Events::Event_CALCULATE_TRANSFORMATIONS, CameraSceneNode::do_CALCULATE_TRANSFORMATIONS);
}

void CameraSceneNode::do_CALCULATE_TRANSFORMATIONS(Events::Event *pEvt)
{
	Handle hParentSN = getFirstParentByType<SceneNode>();
	if (hParentSN.isValid())
	{
		Matrix4x4 parentTransform = hParentSN.getObject<PE::Components::SceneNode>()->m_worldTransform;
		m_worldTransform = parentTransform * m_base;
	}
	
	Matrix4x4 &mref_worldTransform = m_worldTransform;

	Vector3 pos = Vector3(mref_worldTransform.m[0][3], mref_worldTransform.m[1][3], mref_worldTransform.m[2][3]);
	Vector3 n = Vector3(mref_worldTransform.m[0][2], mref_worldTransform.m[1][2], mref_worldTransform.m[2][2]);
	Vector3 target = pos + n;
	Vector3 up = Vector3(mref_worldTransform.m[0][1], mref_worldTransform.m[1][1], mref_worldTransform.m[2][1]);

	n.normalize();
	up.normalize();
	
	Vector3 right = n.crossProduct(up);
	right.normalize();
	float nearDist = 10.0f;
	float farDist = 20.0f;
	float wNear = 23.84f;
	float hNear = 13.41f;
	float wFar = 47.67f;
	float hFar = 26.81f;


	Vector3 farCenter = pos + farDist * n;
	Vector3 nearCenter = pos + nearDist * n;

	nearCoords[0] = nearCenter + (up * (hNear / 2)) - (right * (wNear / 2)); //TL
	nearCoords[1] = nearCenter + (up * (hNear / 2)) + (right * (wNear / 2)); //TR
	nearCoords[2] = nearCenter - (up * (hNear / 2)) - (right * (wNear / 2)); //BL
	nearCoords[3] = nearCenter - (up * (hNear / 2)) + (right * (wNear / 2)); //BR

	farCoords[0] = farCenter + (up * (hFar / 2)) - (right * (wFar / 2)); //TL
	farCoords[1] = farCenter + (up * (hFar / 2)) + (right * (wFar / 2)); //TR
	farCoords[2] = farCenter - (up * (hFar / 2)) - (right * (wFar / 2)); //BL
	farCoords[3] = farCenter - (up * (hFar / 2)) + (right * (wFar / 2)); //BR

	Vector3 v1 = nearCoords[1] - nearCoords[2];
	Vector3 v2 = nearCoords[3] - nearCoords[2];
	Vector3 nearNormal = v2.crossProduct(v1);
	nearNormal.normalize();
	float magNear = -(nearNormal.dotProduct(nearCoords[2]));
	Vector4 nearPlane(nearNormal.m_x, nearNormal.m_y, nearNormal.m_z, magNear);

	Vector3 v3 = farCoords[1] - farCoords[2];
	Vector3 v4 = farCoords[3] - farCoords[2];
	Vector3 farNormal = v4.crossProduct(v3);
	farNormal.normalize();
	float magFar = -(farNormal.dotProduct(farCoords[2]));
	Vector4 farPlane(farNormal.m_x, farNormal.m_y, farNormal.m_z, magFar);



	Matrix4x4 newTransform = m_worldTransform;
	newTransform.turnLeft(0.7);
	Vector3 leftNormal = newTransform.getU();
	float magLeft = -(leftNormal.dotProduct(newTransform.getPos()));
	Vector4 leftPlane(leftNormal.m_x, leftNormal.m_y, leftNormal.m_z, magLeft); 
	/*
	Vector3 v5 = farCoords[2] - farCoords[0];
	Vector3 v6 = nearCoords[0] - farCoords[0];
	Vector3 leftNormal = v6.crossProduct(v5);
	leftNormal.normalize();
	float magLeft = -(leftNormal.dotProduct(farCoords[0]));
	Vector4 leftPlane(leftNormal.m_x, leftNormal.m_y, leftNormal.m_z, magLeft);*/

	Matrix4x4 rightTransform = m_worldTransform;
	rightTransform.turnRight(0.7);
	Vector3 rightNormal = rightTransform.getU();
	float magRight = -(rightNormal.dotProduct(rightTransform.getPos()));
	Vector4 rightPlane(rightNormal.m_x, rightNormal.m_y, rightNormal.m_z, magRight);
	
	/*Vector3 v7 = farCoords[3] - farCoords[1];
	Vector3 v8 = nearCoords[1] - farCoords[1];
	Vector3 rightNormal = v8.crossProduct(v7);
	rightNormal.normalize();
	float magRight = -(rightNormal.dotProduct(farCoords[1]));
	Vector4 rightPlane(leftNormal.m_x, leftNormal.m_y, leftNormal.m_z, magRight);*/


	Matrix4x4 upTransform = m_worldTransform;
	upTransform.turnUp(0.33f * PrimitiveTypes::Constants::c_Pi_F32 / 2);
	Vector3 upNormal = -upTransform.getN();
	float magUp = -(upNormal.dotProduct(upTransform.getPos()));
	Vector4 upPlane(upNormal.m_x, upNormal.m_y, upNormal.m_z, magUp);


	/*Vector3 v9 = farCoords[1] - farCoords[0];
	Vector3 v10 = nearCoords[0] - farCoords[0];
	Vector3 upNormal = v10.crossProduct(v9);
	upNormal.normalize();
	float magUp = -(upNormal.dotProduct(farCoords[0]));
	Vector4 upPlane(upNormal.m_x, upNormal.m_y, upNormal.m_z, magUp);*/

	Matrix4x4 downTransform = m_worldTransform;
	downTransform.turnDown(0.33f * PrimitiveTypes::Constants::c_Pi_F32 / 2);
	Vector3 downNormal = downTransform.getN();
	float magDown = -(downNormal.dotProduct(downTransform.getPos()));
	Vector4 downPlane(downNormal.m_x, downNormal.m_y, downNormal.m_z, magDown);

	//Vector3 v11 = farCoords[3] - farCoords[2];
	//Vector3 v12 = nearCoords[2] - farCoords[2];
	//Vector3 downNormal = v12.crossProduct(v11);
	//downNormal.normalize();
	//float magDown = -(downNormal.dotProduct(farCoords[2]));
	//Vector4 downPlane(downNormal.m_x, downNormal.m_y, downNormal.m_z, magDown);


	planes[0] = nearPlane;
	planes[1] = farPlane;
	planes[2] = leftPlane;
	planes[3] = rightPlane;
	planes[4] = upPlane;
	planes[5] = downPlane;



	m_worldToViewTransform = CameraOps::CreateViewMatrix(pos, target, up);

	m_worldTransform2 = mref_worldTransform;

	m_worldTransform2.moveForward(Z_ONLY_CAM_BIAS);

	Vector3 pos2 = Vector3(m_worldTransform2.m[0][3], m_worldTransform2.m[1][3], m_worldTransform2.m[2][3]);
	Vector3 n2 = Vector3(m_worldTransform2.m[0][2], m_worldTransform2.m[1][2], m_worldTransform2.m[2][2]);
	Vector3 target2 = pos2 + n2;
	Vector3 up2 = Vector3(m_worldTransform2.m[0][1], m_worldTransform2.m[1][1], m_worldTransform2.m[2][1]);

	m_worldToViewTransform2 = CameraOps::CreateViewMatrix(pos2, target2, up2);
    
    PrimitiveTypes::Float32 aspect = (PrimitiveTypes::Float32)(m_pContext->getGPUScreen()->getWidth()) / (PrimitiveTypes::Float32)(m_pContext->getGPUScreen()->getHeight());
    
    PrimitiveTypes::Float32 verticalFov = 0.33f * PrimitiveTypes::Constants::c_Pi_F32;
    if (aspect < 1.0f)
    {
        //ios portrait view
        static PrimitiveTypes::Float32 factor = 0.5f;
        verticalFov *= factor;
    }

	m_viewToProjectedTransform = CameraOps::CreateProjectionMatrix(verticalFov, 
		aspect,
		m_near, m_far);
	
	SceneNode::do_CALCULATE_TRANSFORMATIONS(pEvt);

}

}; // namespace Components
}; // namespace PE
