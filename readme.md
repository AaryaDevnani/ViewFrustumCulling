![Culling](https://github.com/AaryaDevnani/ViewFrustumCulling/assets/62675730/cfea8182-b5a5-4847-adc3-c918bc32b2d1)

<p align="center">
  <img src="[https://github.com/AaryaDevnani/TribesNetworkingModel/assets/62675730/800544b3-1dfc-4fd3-9726-ec4ea7846d95](https://github.com/AaryaDevnani/ViewFrustumCulling/assets/62675730/cfea8182-b5a5-4847-adc3-c918bc32b2d1)" alt="View Frustum Culling Demo Gif" />
</p>
<p align="center">
View Frustum Culling Demo
</p>

# Prime Engine


Prime Engine is an engine developed by Prof. Artem Kovalovs.

I had taken a course on Game Engine Development under Professor Kovalovs in Spring ‘24.

Prime Engine is written entirely by Professor Kovalovs. 

The assignment for the course involved implementing features into this engine. This repo contains a snippet of some functionalities that I have added to the engine.
# [View Frustum Culling](https://drive.google.com/file/d/1Wp-0ClDlmAfjicfYklHXQE0AYcQb1Xl8/view?usp=sharing)

 [Demo Video Here!](https://drive.google.com/file/d/1Wp-0ClDlmAfjicfYklHXQE0AYcQb1Xl8/view?usp=sharing)

## What is View Frustum Culling?
- *Camera frustum* represents the zone of *vision* of a *camera*. The point of view frustum culling is to skip the rendering of objects outside of the camera’s field of view so as to improve performance
- This technique only renders objects visible on screen, boosting performance by skipping hidden ones.
- Imagine a pyramid extending from the camera. This "view frustum" defines what's seen. Objects outside are culled (ignored), saving precious GPU power.
- By focusing on what's truly visible, view frustum culling unlocks smoother gameplay and higher frame rates.

## Implementation Details
- Here is how I implemented it in Prime Engine:
    1. To start off, we must draw a bounding box around each object on the screen.
        - I first read the data stored in the position buffer in the `MeshCPU.cpp` file.
        - I then extracted the min and max values about each axis.
        - Using these 6 values, we can form 8 points that will be the coordinates of the bounding box.
        - Next, to draw these boxes on screen, I first created a function under the `DebugRenderer.cpp` file, that takes in two parameters: `transform` and `AABBCoords`.
        As the names suggest, the `transform` is the position of the Imrod in the real world, and the `AABBCoords` is an array of the coordinates generated above.
        - Next, I used the transform to transform all the AABB coordinates, and then draw them using the `DebugRenderer::createLineMesh()`function,
    2. Creating plane equations for the view frustum, and then performing culling
        - We start off in the `CameraSceneNode.cpp` file, where we must first make the plane equations.
        - Initially, I was manually calculating the plane equations using the following method:
            1. I set the various parameters required to calculate the points of the frustum based on the field of view.
                
                ```cpp
                float nearDist = 10.0f;
                float farDist = 20.0f;
                float wNear = 9.68f;
                float hNear = 8.0f;
                float wFar = 19.37f;
                float hFar = 18.0f;
                ```
                
            2. I calculate the coordinates of the frustum: 
                
                ```cpp
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
                ```
                
            3. I calculate the 6 planes using the above coordinates, using the following method:
                
                ```cpp
                // Coord 1, Coord 2 and Coord 3 must be 3 adjacent plane coordinates
                Vector3 v1 = coord1 - coord2;
                Vector3 v2 = coord3 - coord2;
                Vector3 normal = v2.crossProduct(v1);
                normal.normalize();
                float magnitude = -(normal.dotProduct(coord2));
                Vector4 plane(normal.m_x, normal.m_y, normal.m_z, magnitude);
                ```
                
            4. Now that we have our 6 planes, we can perform our culling test in the following manner: 
                
                ```cpp
                if ( pMeshCaller->m_performBoundingVolumeCulling)
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
                
                	if ( farDistance >0 && nearDistance <0 &&  leftDistance > 0 && rightDistance < 0 && downDistance > 0 && upDistance < 0) {
                		
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
                ```
