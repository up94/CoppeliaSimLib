/*
Source code based on the V-REP library source code from Coppelia
Robotics AG on September 2019

Copyright (C) 2006-2023 Coppelia Robotics AG
Copyright (C) 2019 Robot Nordic ApS

All rights reserved.

GNU GPL license:
================
The code in this file is free software: you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

See the GNU General Public License for more details.
<http://www.gnu.org/licenses/>
*/

#include <app.h>

#ifdef SIM_WITH_OPENGL
#include <oGL.h>

void _activateNonAmbientLights(int lightHandle,CViewableBase* viewable);
void _prepareAuxClippingPlanes();
void _enableAuxClippingPlanes(int objID);
void _prepareOrEnableAuxClippingPlanes(bool prepare,int objID);
void _disableAuxClippingPlanes();
void _drawReference(CSceneObject* object,double refSize);
void _displayBoundingBox(CSceneObject* object,int displayAttrib,bool displRef,double refSize);
void _selectLights(CSceneObject* object,CViewableBase* viewable);
void _restoreDefaultLights(CSceneObject* object,CViewableBase* viewable);
void _commonStart(CSceneObject* object,CViewableBase* viewable,int displayAttrib);
void _commonFinish(CSceneObject* object,CViewableBase* viewable);

void _drawTriangles(const float* vertices,int verticesCnt,const int* indices,int indicesCnt,const float* normals,const float* textureCoords,int* vertexBufferId,int* normalBufferId,int* texCoordBufferId);
bool _drawEdges(const float* vertices,int verticesCnt,const int* indices,int indicesCnt,const unsigned char* edges,int* edgeBufferId);
void _drawColorCodedTriangles(const float* vertices,int verticesCnt,const int* indices,int indicesCnt,const float* normals,int* vertexBufferId,int* normalBufferId);

bool _start3DTextureDisplay(CTextureProperty* tp);
void _end3DTextureDisplay(CTextureProperty* tp);
bool _start2DTextureDisplay(CTextureProperty* tp);
void _end2DTextureDisplay(CTextureProperty* tp);

void _startTextureDisplay(CTextureObject* to,bool interpolateColor,int applyMode,bool repeatX,bool repeatY);
void _endTextureDisplay();

#endif

void initializeRendering();
void deinitializeRendering();
void initGl_ifNeeded();
void deinitGl_ifNeeded();
void increaseVertexBufferRefCnt(int vertexBufferId);
void decreaseVertexBufferRefCnt(int vertexBufferId);
void increaseNormalBufferRefCnt(int normalBufferId);
void decreaseNormalBufferRefCnt(int normalBufferId);
void increaseEdgeBufferRefCnt(int edgeBufferId);
void decreaseEdgeBufferRefCnt(int edgeBufferId);
void decreaseTexCoordBufferRefCnt(int texCoordBufferId);
void destroyGlTexture(unsigned int texName);
void makeColorCurrent(const CColorObject* visParam,bool forceNonTransparent,bool useAuxiliaryComponent);
