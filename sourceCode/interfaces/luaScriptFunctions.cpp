#include <luaScriptFunctions.h>
#include <simInternal.h>
#include <tt.h>
#include <threadPool_old.h>
#include <linMotionRoutines.h>
#include <pluginContainer.h>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <vVarious.h>
#include <vDateTime.h>
#include <app.h>
#include <apiErrors.h>
#include <interfaceStack.h>
#include <fileOperations.h>
#include <ttUtil.h>
#include <imgLoaderSaver.h>
#include <sceneObjectOperations.h>
#include <collisionRoutines.h>
#include <distanceRoutines.h>
#include <cbor.h>
#include <meshRoutines.h>

#define LUA_START(funcName) \
    CApiErrors::clearThreadBasedFirstCapiErrorAndWarning_old(); \
    std::string functionName(funcName); \
    std::string errorString; \
    std::string warningString; \
    bool cSideErrorOrWarningReporting=true;

#define LUA_START_NO_CSIDE_ERROR(funcName) \
    CApiErrors::clearThreadBasedFirstCapiErrorAndWarning_old(); \
    std::string functionName(funcName); \
    std::string errorString; \
    std::string warningString; \
    bool cSideErrorOrWarningReporting=false;

#define LUA_END(p) \
    do { \
        _reportWarningsIfNeeded(L,functionName.c_str(),warningString.c_str(),cSideErrorOrWarningReporting); \
        return(p); \
    } while(0)

void _reportWarningsIfNeeded(luaWrap_lua_State* L,const char* functionName,const char* warningString,bool cSideErrorOrWarningReporting)
{
    std::string warnStr(warningString);
    if ( (warnStr.size()==0)&&cSideErrorOrWarningReporting )
        warnStr=CApiErrors::getAndClearThreadBasedFirstCapiWarning_old(); // without old threads, use CApiErrors::getAndClearLastWarningOrError
    if (warnStr.size()>0) // without old threads, check and remove "warning@" in warnStr
    {
        CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
        if (it!=nullptr)
        {
            int verb=sim_verbosity_scriptwarnings;
            int lineNumber=-1;
            lineNumber=luaWrap_getCurrentCodeLine(L);
            std::string msg;
            msg+=std::to_string(lineNumber);
            msg+=": in ";
            msg+=functionName;
            msg+="' ";
            msg+=warnStr;
            App::logScriptMsg(it->getShortDescriptiveName().c_str(),verb,msg.c_str());
        }
    }
}

void _raiseErrorIfNeeded(luaWrap_lua_State* L,const char* functionName,const char* errorString,bool cSideErrorOrWarningReporting)
{
    std::string errStr(errorString);
    if ( (errStr.size()==0)&&cSideErrorOrWarningReporting )
        errStr=CApiErrors::getAndClearThreadBasedFirstCapiError_old(); // without old threads, use CApiErrors::getAndClearLastWarningOrError
    if (errStr.size()==0)
        return;
    // without old threads, filter out "warning@" in errStr
    CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
    if (it==nullptr)
        return;
    it->setLastError_old(errStr.c_str());
    if (!it->getRaiseErrors_backCompatibility())
        return;
    int lineNumber=-1;
    lineNumber=luaWrap_getCurrentCodeLine(L);
    std::string msg;
    msg+=std::to_string(lineNumber);
    msg+=": in ";
    msg+=functionName;
    msg+=": ";
    msg+=errStr;
    luaWrap_lua_pushstring(L,msg.c_str());

    luaWrap_lua_error(L); // does a long jump and never returns
}

#define LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED() _raiseErrorIfNeeded(L,functionName.c_str(),errorString.c_str(),cSideErrorOrWarningReporting)


const SLuaCommands simLuaCommands[]=
{
    {"sim.handleDynamics",_simHandleDynamics,                    "int result=sim.handleDynamics(float deltaTime)",true},
    {"sim.handleProximitySensor",_simHandleProximitySensor,      "int result,float distance,float[3] detectedPoint,int detectedObjectHandle,float[3] normalVector=sim.handleProximitySensor(int sensorHandle)",true},
    {"sim.readProximitySensor",_simReadProximitySensor,          "int result,float distance,float[3] detectedPoint,int detectedObjectHandle,float[3] normalVector=sim.readProximitySensor(int sensorHandle)",true},
    {"sim.resetProximitySensor",_simResetProximitySensor,        "sim.resetProximitySensor(int objectHandle)",true},
    {"sim.checkProximitySensor",_simCheckProximitySensor,        "int result,float distance,float[3] detectedPoint,int detectedObjectHandle,float[3] normalVector=sim.checkProximitySensor(int sensorHandle,int entityHandle)",true},
    {"sim.checkProximitySensorEx",_simCheckProximitySensorEx,    "int result,float distance,float[3] detectedPoint,int detectedObjectHandle,float[3] normalVector=sim.checkProximitySensorEx(int sensorHandle,int entityHandle,int mode,float threshold,float maxAngle)",true},
    {"sim.checkProximitySensorEx2",_simCheckProximitySensorEx2,  "int result,float distance,float[3] detectedPoint,float[3] normalVector=sim.checkProximitySensorEx2(int sensorHandle,float[3..*] vertices,int itemType,int itemCount,int mode,float threshold,float maxAngle)",true},
    {"sim._getObject",_sim_getObject,                            "",false}, // handled via sim.getObject from sim.lua
    {"sim.getObjectUid",_simGetObjectUid,                        "int uid=sim.getObjectUid(int objectHandle)",true},
    {"sim._getObjectFromUid",_sim_getObjectFromUid,              "",false}, // handled via sim.getObjectFromUid from sim.lua
    {"sim.getScript",_simGetScript,                              "int scriptHandle=sim.getScript(int scriptType,int objectHandle=-1,string scriptName='')",true},
    {"sim.addScript",_simAddScript,                              "int scriptHandle=sim.addScript(int scriptType)",true},
    {"sim.associateScriptWithObject",_simAssociateScriptWithObject,"sim.associateScriptWithObject(int scriptHandle,int objectHandle)",true},
    {"sim.getObjectPosition",_simGetObjectPosition,              "float[3] position=sim.getObjectPosition(int objectHandle,int relativeToObjectHandle)",true},
    {"sim.getObjectOrientation",_simGetObjectOrientation,        "float[3] eulerAngles=sim.getObjectOrientation(int objectHandle,int relativeToObjectHandle)",true},
    {"sim.setObjectPosition",_simSetObjectPosition,              "sim.setObjectPosition(int objectHandle,int relativeToObjectHandle,float[3] position)",true},
    {"sim.setObjectOrientation",_simSetObjectOrientation,        "sim.setObjectOrientation(int objectHandle,int relativeToObjectHandle,float[3] eulerAngles)",true},
    {"sim.getJointPosition",_simGetJointPosition,                "float position=sim.getJointPosition(int objectHandle)",true},
    {"sim.setJointPosition",_simSetJointPosition,                "sim.setJointPosition(int objectHandle,float position)",true},
    {"sim.setJointTargetPosition",_simSetJointTargetPosition,    "sim.setJointTargetPosition(int objectHandle,float targetPosition,float[] motionParams={})",true},
    {"sim.getJointTargetPosition",_simGetJointTargetPosition,    "float targetPosition=sim.getJointTargetPosition(int objectHandle)",true},
    {"sim.setJointTargetVelocity",_simSetJointTargetVelocity,    "sim.setJointTargetVelocity(int objectHandle,float targetVelocity,float[] motionParams={})",true},
    {"sim.getJointTargetVelocity",_simGetJointTargetVelocity,    "float targetVelocity=sim.getJointTargetVelocity(int objectHandle)",true},
    {"sim.removeObjects",_simRemoveObjects,                      "sim.removeObjects(int[1..*] objectHandles)",true},
    {"sim.removeModel",_simRemoveModel,                          "int objectCount=sim.removeModel(int objectHandle)",true},
    {"sim.getSimulationTime",_simGetSimulationTime,              "float simulationTime=sim.getSimulationTime()",true},
    {"sim.getSimulationState",_simGetSimulationState,            "int simulationState=sim.getSimulationState()",true},
    {"sim.getSystemTime",_simGetSystemTime,                      "float time=sim.getSystemTime()",true},
    {"sim.checkCollision",_simCheckCollision,                    "int result,int[2] collidingObjects=sim.checkCollision(int entity1Handle,int entity2Handle)",true},
    {"sim.checkCollisionEx",_simCheckCollisionEx,                "int segmentCount,float[6..*] segmentData=sim.checkCollisionEx(int entity1Handle,int entity2Handle)",true},
    {"sim.checkDistance",_simCheckDistance,                      "int result,float[7] distanceData,int[2] objectHandlePair=sim.checkDistance(int entity1Handle,int entity2Handle,float threshold=0.0)",true},
    {"sim.getSimulationTimeStep",_simGetSimulationTimeStep,      "float timeStep=sim.getSimulationTimeStep()",true},
    {"sim.getSimulatorMessage",_simGetSimulatorMessage,          "int messageID,int[4] auxiliaryData,int[1..*] auxiliaryData2=sim.getSimulatorMessage()",true},
    {"sim.resetGraph",_simResetGraph,                            "sim.resetGraph(int objectHandle)",true},
    {"sim.handleGraph",_simHandleGraph,                          "sim.handleGraph(int objectHandle,float simulationTime)",true},
    {"sim.getGraphCurve",_simGetGraphCurve,                      "string label,int attributes,float[3] curveColor,float[] xData,float[] yData,float[6] minMax,int curveId,int curveWidth=sim.getGraphCurve(int graphHandle,int graphType,int curveIndex)",true},
    {"sim.getGraphInfo",_simGetGraphInfo,                        "int bitCoded,float[3] bgColor,float[3] fgColor,int bufferSize=sim.getGraphInfo(int graphHandle)",true},
    {"sim.addGraphStream",_simAddGraphStream,                    "int streamId=sim.addGraphStream(int graphHandle,string streamName,string unit,int options=0,float[3] color={1,0,0},float cyclicRange=pi)",true},
    {"sim.destroyGraphCurve",_simDestroyGraphCurve,              "sim.destroyGraphCurve(int graphHandle,int curveId)",true},
    {"sim.setGraphStreamTransformation",_simSetGraphStreamTransformation, "sim.setGraphStreamTransformation(int graphHandle,int streamId,int trType,float mult=1.0,float off=0.0,int movAvgPeriod=1)",true},
    {"sim.duplicateGraphCurveToStatic",_simDuplicateGraphCurveToStatic, "int curveId=sim.duplicateGraphCurveToStatic(int graphHandle,int curveId,string curveName='')",true},
    {"sim.addGraphCurve",_simAddGraphCurve,                      "int curveId=sim.addGraphCurve(int graphHandle,string curveName,int dim,int[2..3] streamIds,float[2..3] defaultValues,string unitStr,int options=0,float[3] color={1,1,0},int curveWidth=2)",true},
    {"sim.setGraphStreamValue",_simSetGraphStreamValue,          "sim.setGraphStreamValue(int graphHandle,int streamId,float value)",true},
    {"sim.refreshDialogs",_simRefreshDialogs,                    "int result=sim.refreshDialogs(int refreshDegree)",true},
    {"sim.getModuleName",_simGetModuleName,                      "string moduleName,int version=sim.getModuleName(int index)",true},
    {"sim.removeScript",_simRemoveScript,                        "sim.removeScript(int scriptHandle)",true},
    {"sim.stopSimulation",_simStopSimulation,                    "int result=sim.stopSimulation()",true},
    {"sim.pauseSimulation",_simPauseSimulation,                  "int result=sim.pauseSimulation()",true},
    {"sim.startSimulation",_simStartSimulation,                  "int result=sim.startSimulation()",true},
    {"sim.getObjectMatrix",_simGetObjectMatrix,                  "float[12] matrix=sim.getObjectMatrix(int objectHandle,int relativeToObjectHandle)",true},
    {"sim.setObjectMatrix",_simSetObjectMatrix,                  "sim.setObjectMatrix(int objectHandle,int relativeToObjectHandle,float[12] matrix)",true},
    {"sim.getObjectPose",_simGetObjectPose,                      "float[7] pose=sim.getObjectPose(int objectHandle,int relativeToObjectHandle)",true},
    {"sim.setObjectPose",_simSetObjectPose,                      "sim.setObjectPose(int objectHandle,int relativeToObjectHandle,float[7] pose)",true},
    {"sim.getObjectChildPose",_simGetObjectChildPose,            "float[7] pose=sim.getObjectChildPose(int objectHandle)",true},
    {"sim.setObjectChildPose",_simSetObjectChildPose,            "sim.setObjectChildPose(int objectHandle,float[7] pose)",true},
    {"sim.buildIdentityMatrix",_simBuildIdentityMatrix,          "float[12] matrix=sim.buildIdentityMatrix()",true},
    {"sim.buildMatrix",_simBuildMatrix,                          "float[12] matrix=sim.buildMatrix(float[3] position,float[3] eulerAngles)",true},
    {"sim.buildPose",_simBuildPose,                              "float[7] pose=sim.buildPose(float[3] position,float[3] eulerAnglesOrAxis,int mode=0,float[3] axis2=nil)",true},
    {"sim.getEulerAnglesFromMatrix",_simGetEulerAnglesFromMatrix,"float[3] eulerAngles=sim.getEulerAnglesFromMatrix(float[12] matrix)",true},
    {"sim.invertMatrix",_simInvertMatrix,                        "sim.invertMatrix(float[12] matrix)",true},
    {"sim.invertPose",_simInvertPose,                            "sim.invertPose(float[7] pose)",true},
    {"sim.multiplyMatrices",_simMultiplyMatrices,                "float[12] resultMatrix=sim.multiplyMatrices(float[12] matrixIn1,float[12] matrixIn2)",true},
    {"sim.multiplyPoses",_simMultiplyPoses,                      "float[7] resultPose=sim.multiplyPoses(float[7] poseIn1,float[7] poseIn2)",true},
    {"sim.interpolateMatrices",_simInterpolateMatrices,          "float[12] resultMatrix=sim.interpolateMatrices(float[12] matrixIn1,float[12] matrixIn2,float interpolFactor)",true},
    {"sim.interpolatePoses",_simInterpolatePoses,                "float[7] resultPose=sim.interpolatePoses(float[7] poseIn1,float[7] poseIn2,float interpolFactor)",true},
    {"sim.poseToMatrix",_simPoseToMatrix,                        "float[12] matrix=sim.poseToMatrix(float[7] pose)",true},
    {"sim.matrixToPose",_simMatrixToPose,                        "float[7] pose=sim.matrixToPose(float[12] matrix)",true},
    {"sim.multiplyVector",_simMultiplyVector,                    "float[] resultVectors=sim.multiplyVector(float[12] matrix,float[] inVectors)\nfloat[] resultVectors=sim.multiplyVector(float[7] pose,float[] inVectors)",true},
    {"sim.getObjectChild",_simGetObjectChild,                    "int childObjectHandle=sim.getObjectChild(int objectHandle,int index)",true},
    {"sim.getObjectParent",_simGetObjectParent,                  "int parentObjectHandle=sim.getObjectParent(int objectHandle)",true},
    {"sim.setObjectParent",_simSetObjectParent,                  "sim.setObjectParent(int objectHandle,int parentObjectHandle,bool keepInPlace=true)",true},
    {"sim.getObjectType",_simGetObjectType,                      "int objectType=sim.getObjectType(int objectHandle)",true},
    {"sim.getJointType",_simGetJointType,                        "int jointType=sim.getJointType(int objectHandle)",true},
    {"sim.setBoolParam",_simSetBoolParam,                        "sim.setBoolParam(int parameter,bool boolState)",true},
    {"sim.getBoolParam",_simGetBoolParam,                        "bool boolState=sim.getBoolParam(int parameter)",true},
    {"sim.setInt32Param",_simSetInt32Param,                      "sim.setInt32Param(int parameter,int intState)",true},
    {"sim.getInt32Param",_simGetInt32Param,                      "int intState=sim.getInt32Param(int parameter)",true},
    {"sim.setFloatParam",_simSetFloatParam,                      "sim.setFloatParam(int parameter,float floatState)",true},
    {"sim.getFloatParam",_simGetFloatParam,                      "float floatState=sim.getFloatParam(int parameter)",true},
    {"sim.setStringParam",_simSetStringParam,                    "sim.setStringParam(int parameter,string stringState)",true},
    {"sim.getStringParam",_simGetStringParam,                    "string stringState=sim.getStringParam(int parameter)",true},
    {"sim.setArrayParam",_simSetArrayParam,                      "sim.setArrayParam(int parameter,float[3] arrayOfValues)",true},
    {"sim.getArrayParam",_simGetArrayParam,                      "float[3] arrayOfValues=sim.getArrayParam(int parameter)",true},
    {"sim.setNamedStringParam",_simSetNamedStringParam,          "sim.setNamedStringParam(string paramName,buffer stringParam)",true},
    {"sim.getNamedStringParam",_simGetNamedStringParam,          "buffer stringParam=sim.getNamedStringParam(string paramName)",true},
    {"sim.getObjectAlias",_simGetObjectAlias,                    "string objectAlias=sim.getObjectAlias(int objectHandle,int options=-1)",true},
    {"sim.setObjectAlias",_simSetObjectAlias,                    "sim.setObjectAlias(int objectHandle,string objectAlias)",true},
    {"sim.getJointInterval",_simGetJointInterval,                "bool cyclic,float[2] interval=sim.getJointInterval(int objectHandle)",true},
    {"sim.setJointInterval",_simSetJointInterval,                "sim.setJointInterval(int objectHandle,bool cyclic,float[2] interval)",true},
    {"sim.loadScene",_simLoadScene,                              "sim.loadScene(string filename)\nsim.loadScene(buffer serializedScene)",true},
    {"sim.closeScene",_simCloseScene,                            "int result=sim.closeScene()",true},
    {"sim.saveScene",_simSaveScene,                              "sim.saveScene(string filename)\nbuffer serializedScene=sim.saveScene()",true},
    {"sim.loadModel",_simLoadModel,                              "int objectHandle=sim.loadModel(string filename)\nint objectHandle=sim.loadModel(buffer serializedModel)",true},
    {"sim.saveModel",_simSaveModel,                              "sim.saveModel(int modelBaseHandle,string filename)\nbuffer serializedModel=sim.saveModel(int modelBaseHandle)",true},
    {"sim.getObjectSel",_simGetObjectSel,                        "int[] objectHandles=sim.getObjectSel()",true},
    {"sim.setObjectSel",_simSetObjectSel,                        "sim.setObjectSel(int[] objectHandles)",true},
    {"sim.getIsRealTimeSimulation",_simGetRealTimeSimulation,      "int result=sim.getIsRealTimeSimulation()",true},
    {"sim.setNavigationMode",_simSetNavigationMode,              "sim.setNavigationMode(int navigationMode)",true},
    {"sim.getNavigationMode",_simGetNavigationMode,              "int navigationMode=sim.getNavigationMode()",true},
    {"sim.setPage",_simSetPage,                                  "sim.setPage(int pageIndex)",true},
    {"sim.getPage",_simGetPage,                                  "int pageIndex=sim.getPage()",true},
    {"sim.copyPasteObjects",_simCopyPasteObjects,                "int[1..*] copiedObjectHandles=sim.copyPasteObjects(int[1..*] objectHandles,int options=0)",true},
    {"sim.scaleObjects",_simScaleObjects,                        "sim.scaleObjects(int[1..*] objectHandles,float scalingFactor,bool scalePositionsToo)",true},
    {"sim.setThreadAutomaticSwitch",_simSetThreadAutomaticSwitch,"int autoSwitchForbidLevel=sim.setThreadAutomaticSwitch(bool automaticSwitch)\nint autoSwitchForbidLevel=sim.setThreadAutomaticSwitch(int forbidLevel)",true},
    {"sim.getThreadAutomaticSwitch",_simGetThreadAutomaticSwitch,"bool result=sim.getThreadAutomaticSwitch()",true},
    {"sim.getThreadSwitchAllowed",_simGetThreadSwitchAllowed,    "bool allowed=sim.getThreadSwitchAllowed()",true},
    {"sim.setThreadSwitchAllowed",_simSetThreadSwitchAllowed,    "int forbidLevel=sim.setThreadSwitchAllowed(bool allowed)\nint forbidLevel=sim.setThreadSwitchAllowed(int forbidLevel)",true},
    {"sim.setThreadSwitchTiming",_simSetThreadSwitchTiming,      "sim.setThreadSwitchTiming(int dtInMs)",true},
    {"sim.getThreadSwitchTiming",_simGetThreadSwitchTiming,      "int dtInMs=sim.getThreadSwitchTiming()",true},
    {"sim.saveImage",_simSaveImage,                              "buffer serializedImage=sim.saveImage(buffer image,int[2] resolution,int options,string filename,int quality)",true},
    {"sim.loadImage",_simLoadImage,                              "buffer image,int[2] resolution=sim.loadImage(int options,string filename)\nbuffer image,int[2] resolution=sim.loadImage(int options,buffer serializedImage)",true},
    {"sim.getScaledImage",_simGetScaledImage,                    "buffer imageOut,int[2] effectiveResolutionOut=sim.getScaledImage(buffer imageIn,int[2] resolutionIn,int[2] desiredResolutionOut,int options)",true},
    {"sim.transformImage",_simTransformImage,                    "sim.transformImage(buffer image,int[2] resolution,int options)",true},
    {"sim.getQHull",_simGetQHull,                                "float[] verticesOut,int[] indicesOut=sim.getQHull(float[] verticesIn)",true},
    {"sim.getDecimatedMesh",_simGetDecimatedMesh,                "float[] verticesOut,int[] indicesOut=sim.getDecimatedMesh(float[] verticesIn,int[] indicesIn,float decimationPercentage)",true},
    {"sim.packInt32Table",_simPackInt32Table,                    "buffer data=sim.packInt32Table(int[] int32Numbers,int startInt32Index=0,int int32Count=0)",true},
    {"sim.packUInt32Table",_simPackUInt32Table,                  "buffer data=sim.packUInt32Table(int[] uint32Numbers,int startUInt32Index=0,int uint32Count=0)",true},
    {"sim.packFloatTable",_simPackFloatTable,                    "buffer data=sim.packFloatTable(float[] floatNumbers,int startFloatIndex=0,int floatCount=0)",true},
    {"sim.packDoubleTable",_simPackDoubleTable,                  "buffer data=sim.packDoubleTable(float[] doubleNumbers,int startDoubleIndex=0,int doubleCount=0)",true},
    {"sim.packUInt8Table",_simPackUInt8Table,                    "buffer data=sim.packUInt8Table(int[] uint8Numbers,int startUint8Index=0,int uint8count=0)",true},
    {"sim.packUInt16Table",_simPackUInt16Table,                  "buffer data=sim.packUInt16Table(int[] uint16Numbers,int startUint16Index=0,int uint16Count=0)",true},
    {"sim.unpackInt32Table",_simUnpackInt32Table,                "int[] int32Numbers=sim.unpackInt32Table(buffer data,int startInt32Index=0,int int32Count=0,int additionalByteOffset=0)",true},
    {"sim.unpackUInt32Table",_simUnpackUInt32Table,              "int[] uint32Numbers=sim.unpackUInt32Table(buffer data,int startUint32Index=0,int uint32Count=0,int additionalByteOffset=0)",true},
    {"sim.unpackFloatTable",_simUnpackFloatTable,                "float[] floatNumbers=sim.unpackFloatTable(buffer data,int startFloatIndex=0,int floatCount=0,int additionalByteOffset=0)",true},
    {"sim.unpackDoubleTable",_simUnpackDoubleTable,              "float[] doubleNumbers=sim.unpackDoubleTable(buffer data,int startDoubleIndex=0,int doubleCount=0,int additionalByteOffset=0)",true},
    {"sim.unpackUInt8Table",_simUnpackUInt8Table,                "int[] uint8Numbers=sim.unpackUInt8Table(buffer data,int startUint8Index=0,int uint8count=0)",true},
    {"sim.unpackUInt16Table",_simUnpackUInt16Table,              "int[] uint16Numbers=sim.unpackUInt16Table(buffer data,int startUint16Index=0,int uint16Count=0,int additionalByteOffset=0)",true},
    {"sim.packTable",_simPackTable,                              "buffer buffer=sim.packTable(any[] aTable,int scheme=0)\nbuffer buffer=sim.packTable(map aTable,int scheme=0)",true},
    {"sim.unpackTable",_simUnpackTable,                          "any aTable=sim.unpackTable(buffer buffer)",true},
    {"sim.transformBuffer",_simTransformBuffer,                  "buffer outBuffer=sim.transformBuffer(buffer inBuffer,int inFormat,float multiplier,float offset,int outFormat)",true},
    {"sim.combineRgbImages",_simCombineRgbImages,                "buffer outImg=sim.combineRgbImages(buffer img1,int[2] img1Res,buffer img2,int[2] img2Res,int operation)",true},
    {"sim.getVelocity",_simGetVelocity,                          "float[3] linearVelocity,float[3] angularVelocity=sim.getVelocity(int shapeHandle)",true},
    {"sim.getObjectVelocity",_simGetObjectVelocity,              "float[3] linearVelocity,float[3] angularVelocity=sim.getObjectVelocity(int objectHandle)",true},
    {"sim.getJointVelocity",_simGetJointVelocity,                "float velocity=sim.getJointVelocity(int jointHandle)",true},
    {"sim.addForceAndTorque",_simAddForceAndTorque,              "sim.addForceAndTorque(int shapeHandle,float[3] force=nil,float[3] torque=nil)",true},
    {"sim.addForce",_simAddForce,                                "sim.addForce(int shapeHandle,float[3] position,float[3] force)",true},
    {"sim.setExplicitHandling",_simSetExplicitHandling,          "sim.setExplicitHandling(int objectHandle,int explicitHandlingFlags)",true},
    {"sim.getExplicitHandling",_simGetExplicitHandling,          "int explicitHandlingFlags=sim.getExplicitHandling(int objectHandle)",true},
    {"sim.addDrawingObject",_simAddDrawingObject,                "int drawingObjectHandle=sim.addDrawingObject(int objectType,float size,float duplicateTolerance,int parentObjectHandle,int maxItemCount,float[3] color=nil)",true},
    {"sim.removeDrawingObject",_simRemoveDrawingObject,          "sim.removeDrawingObject(int drawingObjectHandle)",true},
    {"sim.addDrawingObjectItem",_simAddDrawingObjectItem,        "int result=sim.addDrawingObjectItem(int drawingObjectHandle,float[] itemData)",true},
    {"sim.addParticleObject",_simAddParticleObject,              "int particleObjectHandle=sim.addParticleObject(int objectType,float size,float density,float[] params,float lifeTime,int maxItemCount,float[3] color=nil)",true},
    {"sim.removeParticleObject",_simRemoveParticleObject,        "sim.removeParticleObject(int particleObjectHandle)",true},
    {"sim.addParticleObjectItem",_simAddParticleObjectItem,      "sim.addParticleObjectItem(int objectHandle,float[] itemData)",true},
    {"sim.getObjectSizeFactor",_simGetObjectSizeFactor,          "float sizeFactor=sim.getObjectSizeFactor(int ObjectHandle)",true},
    {"sim.setInt32Signal",_simSetInt32Signal,                    "sim.setInt32Signal(string signalName,int signalValue)",true},
    {"sim.getInt32Signal",_simGetInt32Signal,                    "int signalValue=sim.getInt32Signal(string signalName)",true},
    {"sim.clearInt32Signal",_simClearInt32Signal,                "sim.clearInt32Signal(string signalName)",true},
    {"sim.setFloatSignal",_simSetFloatSignal,                    "sim.setFloatSignal(string signalName,float signalValue)",true},
    {"sim.getFloatSignal",_simGetFloatSignal,                    "float signalValue=sim.getFloatSignal(string signalName)",true},
    {"sim.clearFloatSignal",_simClearFloatSignal,                "sim.clearFloatSignal(string signalName)",true},
    {"sim.setStringSignal",_simSetStringSignal,                  "sim.setStringSignal(string signalName,buffer signalValue)",true},
    {"sim.getStringSignal",_simGetStringSignal,                  "buffer signalValue=sim.getStringSignal(string signalName)",true},
    {"sim.clearStringSignal",_simClearStringSignal,              "clearStringSignal(string signalName)",true},
    {"sim.getSignalName",_simGetSignalName,                      "string signalName=sim.getSignalName(int signalIndex,int signalType)",true},
    {"sim.persistentDataWrite",_simPersistentDataWrite,          "sim.persistentDataWrite(string dataTag,buffer dataValue,int options=0)",true},
    {"sim.persistentDataRead",_simPersistentDataRead,            "buffer dataValue=sim.persistentDataRead(string dataTag)",true},
    {"sim.setObjectProperty",_simSetObjectProperty,              "sim.setObjectProperty(int objectHandle,int property)",true},
    {"sim.getObjectProperty",_simGetObjectProperty,              "int property=sim.getObjectProperty(int objectHandle)",true},
    {"sim.setObjectSpecialProperty",_simSetObjectSpecialProperty,"sim.setObjectSpecialProperty(int objectHandle,int property)",true},
    {"sim.getObjectSpecialProperty",_simGetObjectSpecialProperty,"int property=sim.getObjectSpecialProperty(int objectHandle)",true},
    {"sim.setModelProperty",_simSetModelProperty,                "sim.setModelProperty(int objectHandle,int property)",true},
    {"sim.getModelProperty",_simGetModelProperty,                "int property=sim.getModelProperty(int objectHandle)",true},
    {"sim.readForceSensor",_simReadForceSensor,                  "int result,float[3] forceVector,float[3] torqueVector=sim.readForceSensor(int objectHandle)",true},
    {"sim.getLightParameters",_simGetLightParameters,            "int state,float[3] zero,float[3] diffusePart,float[3] specular=sim.getLightParameters(int lightHandle)",true},
    {"sim.setLightParameters",_simSetLightParameters,            "sim.setLightParameters(int lightHandle,int state,float[3] reserved,float[3] diffusePart,float[3] specularPart)",true},
    {"sim.getLinkDummy",_simGetLinkDummy,                        "int linkDummyHandle=sim.getLinkDummy(int dummyHandle)",true},
    {"sim.setLinkDummy",_simSetLinkDummy,                        "sim.setLinkDummy(int dummyHandle,int linkDummyHandle)",true},
    {"sim.setObjectColor",_simSetObjectColor,                    "bool result=sim.setObjectColor(int objectHandle,int index,int colorComponent,float[3] rgbData)",true},
    {"sim.getObjectColor",_simGetObjectColor,                    "float[3] rgbData=sim.getObjectColor(int objectHandle,int index,int colorComponent)",true},
    {"sim.setShapeColor",_simSetShapeColor,                      "sim.setShapeColor(int shapeHandle,string colorName,int colorComponent,float[3] rgbData)",true},
    {"sim.getShapeColor",_simGetShapeColor,                      "int result,float[] rgbData=sim.getShapeColor(int shapeHandle,string colorName,int colorComponent)",true},
    {"sim.resetDynamicObject",_simResetDynamicObject,            "sim.resetDynamicObject(int objectHandle)",true},
    {"sim.setJointMode",_simSetJointMode,                        "sim.setJointMode(int jointHandle,int jointMode,int options)",true},
    {"sim.getJointMode",_simGetJointMode,                        "int jointMode,int options=sim.getJointMode(int jointHandle)",true},
    {"sim._serialOpen",_simSerialOpen,                           "",false}, // partially implemented in sim.lua
    {"sim._serialClose",_simSerialClose,                         "",false}, // partially implemented in sim.lua
    {"sim.serialSend",_simSerialSend,                            "int charsSent=sim.serialSend(int portHandle,buffer data)",true},
    {"sim._serialRead",_simSerialRead,                           "",false}, // partially implemented in sim.lua
    {"sim.serialCheck",_simSerialCheck,                          "int byteCount=sim.serialCheck(int portHandle)",true},
    {"sim.getContactInfo",_simGetContactInfo,                    "int[2] collidingObjects,float[3] collisionPoint,float[3] reactionForce,float[3] normalVector=sim.getContactInfo(int dynamicPass,int objectHandle,int index)",true},
    {"sim.auxiliaryConsoleOpen",_simAuxiliaryConsoleOpen,        "int consoleHandle=sim.auxiliaryConsoleOpen(string title,int maxLines,int mode,int[2] position=nil,int[2] size=nil,float[3] textColor=nil,float[3] backgroundColor=nil)",true},
    {"sim.auxiliaryConsoleClose",_simAuxiliaryConsoleClose,      "int result=sim.auxiliaryConsoleClose(int consoleHandle)",true},
    {"sim.auxiliaryConsolePrint",_simAuxiliaryConsolePrint,      "int result=sim.auxiliaryConsolePrint(int consoleHandle,string text)",true},
    {"sim.auxiliaryConsoleShow",_simAuxiliaryConsoleShow,        "int result=sim.auxiliaryConsoleShow(int consoleHandle,bool showState)",true},
    {"sim.importShape",_simImportShape,                          "int shapeHandle=sim.importShape(int fileformat,string pathAndFilename,int options,float identicalVerticeTolerance,float scalingFactor)",true},
    {"sim.importMesh",_simImportMesh,                            "float[1..*] vertices,int[1..*] indices=sim.importMesh(int fileformat,string pathAndFilename,int options,float identicalVerticeTolerance,float scalingFactor)",true},
    {"sim.exportMesh",_simExportMesh,                            "sim.exportMesh(int fileformat,string pathAndFilename,int options,float scalingFactor,float[1..*] vertices,int[1..*] indices)",true},
    {"sim.createMeshShape",_simCreateMeshShape,                  "int shapeHandle=sim.createMeshShape(int options,float shadingAngle,float[] vertices,int[] indices)",true},
    {"sim.getShapeMesh",_simGetShapeMesh,                        "float[] vertices,int[] indices,float[] normals=sim.getShapeMesh(int shapeHandle)",true},
    {"sim.createPrimitiveShape",_simCreatePrimitiveShape,        "int shapeHandle=sim.createPrimitiveShape(int primitiveType,float[3] sizes,int options=0)",true},
    {"sim.createHeightfieldShape",_simCreateHeightfieldShape,    "int shapeHandle=sim.createHeightfieldShape(int options,float shadingAngle,int xPointCount,int yPointCount,float xSize,float[] heights)",true},
    {"sim.createJoint",_simCreateJoint,                          "int jointHandle=sim.createJoint(int jointType,int jointMode,int options,float[2] sizes=nil)",true},
    {"sim.createDummy",_simCreateDummy,                          "int dummyHandle=sim.createDummy(float size)",true},
    {"sim.createProximitySensor",_simCreateProximitySensor,      "int sensorHandle=sim.createProximitySensor(int sensorType,int subType,int options,int[8] intParams,float[15] floatParams)",true},
    {"sim.createForceSensor",_simCreateForceSensor,              "int sensorHandle=sim.createForceSensor(int options,int[5] intParams,float[5] floatParams)",true},
    {"sim.createVisionSensor",_simCreateVisionSensor,            "int sensorHandle=sim.createVisionSensor(int options,int[4] intParams,float[11] floatParams)",true},
    {"sim.floatingViewAdd",_simFloatingViewAdd,                  "int floatingViewHandle=sim.floatingViewAdd(float posX,float posY,float sizeX,float sizeY,int options)",true},
    {"sim.floatingViewRemove",_simFloatingViewRemove,            "int result=sim.floatingViewRemove(int floatingViewHandle)",true},
    {"sim.adjustView",_simAdjustView,                            "int result=sim.adjustView(int viewHandleOrIndex,int associatedViewableObjectHandle,int options,string viewLabel=nil)",true},
    {"sim.cameraFitToView",_simCameraFitToView,                  "int result=sim.cameraFitToView(int viewHandleOrIndex,int[] objectHandles=nil,int options=0,float scaling=1.0)",true},
    {"sim.announceSceneContentChange",_simAnnounceSceneContentChange,"int result=sim.announceSceneContentChange()",true},
    {"sim.getObjectInt32Param",_simGetObjectInt32Param,          "int parameter=sim.getObjectInt32Param(int objectHandle,int parameterID)",true},
    {"sim.setObjectInt32Param",_simSetObjectInt32Param,          "sim.setObjectInt32Param(int objectHandle,int parameterID,int parameter)",true},
    {"sim.getObjectFloatParam",_simGetObjectFloatParam,          "float parameter=sim.getObjectFloatParam(int objectHandle,int parameterID)",true},
    {"sim.setObjectFloatParam",_simSetObjectFloatParam,          "sim.setObjectFloatParam(int objectHandle,int parameterID,float parameter)",true},
    {"sim.getObjectFloatArrayParam",_simGetObjectFloatArrayParam,"float[] params=sim.getObjectFloatArrayParam(int objectHandle,int parameterID)",true},
    {"sim.setObjectFloatArrayParam",_simSetObjectFloatArrayParam,"sim.setObjectFloatArrayParam(int objectHandle,int parameterID,float[] params)",true},
    {"sim.getObjectStringParam",_simGetObjectStringParam,        "buffer parameter=sim.getObjectStringParam(int objectHandle,int parameterID)",true},
    {"sim.setObjectStringParam",_simSetObjectStringParam,        "sim.setObjectStringParam(int objectHandle,int parameterID,buffer parameter)",true},
    {"sim.getScriptInt32Param",_simGetScriptInt32Param,          "int parameter=sim.getScriptInt32Param(int scriptHandle,int parameterID)",true},
    {"sim.setScriptInt32Param",_simSetScriptInt32Param,          "sim.setScriptInt32Param(int scriptHandle,int parameterID,int parameter)",true},
    {"sim.getScriptStringParam",_simGetScriptStringParam,        "buffer parameter=sim.getScriptStringParam(int scriptHandle,int parameterID)",true},
    {"sim.setScriptStringParam",_simSetScriptStringParam,        "sim.setScriptStringParam(int scriptHandle,int parameterID,buffer parameter)",true},
    {"sim.getRotationAxis",_simGetRotationAxis,                  "float[3] axis,float angle=sim.getRotationAxis(float[12] matrixStart,float[12] matrixGoal)\nfloat[3] axis,float angle=sim.getRotationAxis(float[7] poseStart,float[7] poseGoal)",true},
    {"sim.rotateAroundAxis",_simRotateAroundAxis,                "float[12] matrixOut=sim.rotateAroundAxis(float[12] matrixIn,float[3] axis,float[3] axisPos,float angle)\nfloat[7] poseOut=sim.rotateAroundAxis(float[7] poseIn,float[3] axis,float[3] axisPos,float angle)",true},
    {"sim.launchExecutable",_simLaunchExecutable,                "sim.launchExecutable(string filename,string parameters='',int showStatus=1)",true},
    {"sim.getJointForce",_simGetJointForce,                      "float forceOrTorque=sim.getJointForce(int jointHandle)",true},
    {"sim.getJointTargetForce",_simGetJointTargetForce,          "float forceOrTorque=sim.getJointTargetForce(int jointHandle)",true},
    {"sim.setJointTargetForce",_simSetJointTargetForce,          "sim.setJointTargetForce(int objectHandle,float forceOrTorque,bool signedValue=true)",true},
    {"sim.isHandle",_simIsHandle,                                "bool result=sim.isHandle(int objectHandle)",true},
    {"sim.getObjectQuaternion",_simGetObjectQuaternion,          "float[4] quaternion=sim.getObjectQuaternion(int objectHandle,int relativeToObjectHandle)",true},
    {"sim.setObjectQuaternion",_simSetObjectQuaternion,          "sim.setObjectQuaternion(int objectHandle,int relativeToObjectHandle,float[4] quaternion)",true},
    {"sim.groupShapes",_simGroupShapes,                          "int shapeHandle=sim.groupShapes(int[] shapeHandles,bool merge=false)",true},
    {"sim.ungroupShape",_simUngroupShape,                        "int[] simpleShapeHandles=sim.ungroupShape(int shapeHandle)",true},
    {"sim.convexDecompose",_simConvexDecompose,                  "int shapeHandle=sim.convexDecompose(int shapeHandle,int options,int[4] intParams,float[3] floatParams)",true},
    {"sim.quitSimulator",_simQuitSimulator,                      "sim.quitSimulator()",true},
    {"sim.getThreadId",_simGetThreadId,                          "int threadId=sim.getThreadId()",true},
    {"sim.setShapeMaterial",_simSetShapeMaterial,                "sim.setShapeMaterial(int shapeHandle,int materialIdOrShapeHandle)",true},
    {"sim.getTextureId",_simGetTextureId,                        "int textureId,int[2] resolution=sim.getTextureId(string textureName)",true},
    {"sim.readTexture",_simReadTexture,                          "buffer textureData=sim.readTexture(int textureId,int options,int posX=0,int posY=0,int sizeX=0,int sizeY=0)",true},
    {"sim.writeTexture",_simWriteTexture,                        "sim.writeTexture(int textureId,int options,buffer textureData,int posX=0,int posY=0,int sizeX=0,int sizeY=0,float interpol=0.0)",true},
    {"sim.createTexture",_simCreateTexture,                      "int shapeHandle,int textureId,int[2] resolution=sim.createTexture(string fileName,int options,float[2] planeSizes=nil,float[2] scalingUV=nil,float[2] xy_g=nil,int fixedResolution=0,int[2] resolution=nil)",true},
    {"sim.writeCustomDataBlock",_simWriteCustomDataBlock,        "sim.writeCustomDataBlock(int objectHandle,string tagName,buffer data)",true},
    {"sim.readCustomDataBlock",_simReadCustomDataBlock,          "buffer data=sim.readCustomDataBlock(int objectHandle,string tagName)",true},
    {"sim.readCustomDataBlockTags",_simReadCustomDataBlockTags,  "string[] tags=sim.readCustomDataBlockTags(int objectHandle)",true},
    {"sim.getShapeGeomInfo",_simGetShapeGeomInfo,                "int result,int pureType,float[4] dimensions=sim.getShapeGeomInfo(int shapeHandle)",true},
    {"sim.getObjectsInTree",_simGetObjectsInTree,                "int[] objects=sim.getObjectsInTree(int treeBaseHandle,int objectType=sim.handle_all,int options=0)",true},
    {"sim.getObjects",_simGetObjects,                            "int objectHandle=sim.getObjects(int index,int objectType)",true},
    {"sim.scaleObject",_simScaleObject,                          "sim.scaleObject(int objectHandle,float xScale,float yScale,float zScale,int options=0)",true},
    {"sim.setShapeTexture",_simSetShapeTexture,                  "sim.setShapeTexture(int shapeHandle,int textureId,int mappingMode,int options,float[2] uvScaling,float[3] position=nil,float[3] orientation=nil)",true},
    {"sim.getShapeTextureId",_simGetShapeTextureId,              "int textureId=sim.getShapeTextureId(int shapeHandle)",true},
    {"sim.destroyCollection",_simDestroyCollection,              "sim.destroyCollection(int collectionHandle)",true},
    {"sim.addItemToCollection",_simAddItemToCollection,          "sim.addItemToCollection(int collectionHandle,int what,int objectHandle,int options)",true},
    {"sim.getCollectionObjects",_simGetCollectionObjects,        "int[] objectHandles=sim.getCollectionObjects(int collectionHandle)",true},
    {"sim.handleAddOnScripts",_simHandleAddOnScripts,            "int count=sim.handleAddOnScripts(int callType)",true},
    {"sim.handleSandboxScript",_simHandleSandboxScript,          "sim.handleSandboxScript(int callType)",true},
    {"sim.handleChildScripts",_simHandleChildScripts,            "int calledScripts=sim.handleChildScripts(int callType)",true},
    {"sim.handleEmbeddedScripts",_simHandleEmbeddedScripts,      "int calledScripts=sim.handleEmbeddedScripts(int callType)",true},
    {"sim.reorientShapeBoundingBox",_simReorientShapeBoundingBox,"int result=sim.reorientShapeBoundingBox(int shapeHandle,int relativeToHandle)",true},
    {"sim.handleVisionSensor",_simHandleVisionSensor,            "int detectionCount,float[] auxPacket1,float[] auxPacket2=sim.handleVisionSensor(int sensorHandle)",true},
    {"sim.readVisionSensor",_simReadVisionSensor,                "int result,float[] auxPacket1,float[] auxPacket2=sim.readVisionSensor(int sensorHandle)",true},
    {"sim.resetVisionSensor",_simResetVisionSensor,              "sim.resetVisionSensor(int sensorHandle)",true},
    {"sim.getVisionSensorImg",_simGetVisionSensorImg,            "buffer image,int[2] resolution=sim.getVisionSensorImg(int sensorHandle,int options=0,float rgbaCutOff=0.0,int[2] pos={0,0},int[2] size={0,0})",true},
    {"sim.setVisionSensorImg",_simSetVisionSensorImg,            "sim.setVisionSensorImg(int sensorHandle,buffer image,int options=0,int[2] pos={0,0},int[2] size={0,0})",true},
    {"sim.getVisionSensorDepth",_simGetVisionSensorDepth,        "buffer depth,int[2] resolution=sim.getVisionSensorDepth(int sensorHandle,int options=0,int[2] pos={0,0},int[2] size={0,0})",true},
    {"sim.checkVisionSensor",_simCheckVisionSensor,              "int result,float[] auxPacket1,float[] auxPacket2=sim.checkVisionSensor(int sensorHandle,int entityHandle)",true},
    {"sim.checkVisionSensorEx",_simCheckVisionSensorEx,          "float[] theBuffer=sim.checkVisionSensorEx(int sensorHandle,int entityHandle,bool returnImage)\nbuffer theBuffer=sim.checkVisionSensorEx(int sensorHandle,int entityHandle,bool returnImage)",true},
    {"sim.ruckigPos",_simRuckigPos,                              "int handle=sim.ruckigPos(int dofs,float baseCycleTime,int flags,float[] currentPosVelAccel,float[] maxVelAccelJerk,int[] selection,float[] targetPosVel)",true},
    {"sim.ruckigVel",_simRuckigVel,                              "int handle=sim.ruckigVel(int dofs,float baseCycleTime,int flags,float[] currentPosVelAccel,float[] maxAccelJerk,int[] selection,float[] targetVel)",true},
    {"sim.ruckigStep",_simRuckigStep,                            "int result,float[] newPosVelAccel,float synchronizationTime=sim.ruckigStep(int handle,float cycleTime)",true},
    {"sim.ruckigRemove",_simRuckigRemove,                        "sim.ruckigRemove(int handle)",true},
    {"sim.buildMatrixQ",_simBuildMatrixQ,                        "float[12] matrix=sim.buildMatrixQ(float[3] position,float[4] quaternion)",true},
    {"sim.getQuaternionFromMatrix",_simGetQuaternionFromMatrix,  "float[4] quaternion=sim.getQuaternionFromMatrix(float[12] matrix)",true},
    {"sim.loadModule",_simLoadModule,                            "int pluginHandle=sim.loadModule(string filenameAndPath,string pluginName)",true},
    {"sim.unloadModule",_simUnloadModule,                        "int result=sim.unloadModule(int pluginHandle)",true},
    {"sim.callScriptFunction",_simCallScriptFunction,            "any outArg=sim.callScriptFunction(string functionName,int scriptHandle,any inArg=nil)",true},
    {"sim.getExtensionString",_simGetExtensionString,            "string theString=sim.getExtensionString(int objectHandle,int index,string key=nil)",true},
    {"sim.computeMassAndInertia",_simComputeMassAndInertia,      "int result=sim.computeMassAndInertia(int shapeHandle,float density)",true},
    {"sim.getEngineFloatParam",_simGetEngineFloatParam,          "float floatParam=sim.getEngineFloatParam(int paramId,int objectHandle)",true},
    {"sim.getEngineInt32Param",_simGetEngineInt32Param,          "int int32Param=sim.getEngineInt32Param(int paramId,int objectHandle)",true},
    {"sim.getEngineBoolParam",_simGetEngineBoolParam,            "bool boolParam=sim.getEngineBoolParam(int paramId,int objectHandle)",true},
    {"sim.setEngineFloatParam",_simSetEngineFloatParam,          "sim.setEngineFloatParam(int paramId,int objectHandle,float floatParam)",true},
    {"sim.setEngineInt32Param",_simSetEngineInt32Param,          "sim.setEngineInt32Param(int paramId,int objectHandle,int int32Param)",true},
    {"sim.setEngineBoolParam",_simSetEngineBoolParam,            "sim.setEngineBoolParam(int paramId,int objectHandle,bool boolParam)",true},
    {"sim.createOctree",_simCreateOctree,                        "int handle=sim.createOctree(float voxelSize,int options,float pointSize)",true},
    {"sim.createPointCloud",_simCreatePointCloud,                "int handle=sim.createPointCloud(float maxVoxelSize,int maxPtCntPerVoxel,int options,float pointSize)",true},
    {"sim.setPointCloudOptions",_simSetPointCloudOptions,        "sim.setPointCloudOptions(int pointCloudHandle,float maxVoxelSize,int maxPtCntPerVoxel,int options,float pointSize)",true},
    {"sim.getPointCloudOptions",_simGetPointCloudOptions,        "float maxVoxelSize,int maxPtCntPerVoxel,int options,float pointSize=sim.getPointCloudOptions(int pointCloudHandle)",true},
    {"sim.insertVoxelsIntoOctree",_simInsertVoxelsIntoOctree,    "int totalVoxelCnt=sim.insertVoxelsIntoOctree(int octreeHandle,int options,float[] points,float[] color=nil,int[] tag=nil)",true},
    {"sim.removeVoxelsFromOctree",_simRemoveVoxelsFromOctree,    "int totalVoxelCnt=sim.removeVoxelsFromOctree(int octreeHandle,int options,float[] points)",true},
    {"sim.insertPointsIntoPointCloud",_simInsertPointsIntoPointCloud,"int totalPointCnt=sim.insertPointsIntoPointCloud(int pointCloudHandle,int options,float[] points,float[] color=nil,float duplicateTolerance=nil)",true},
    {"sim.removePointsFromPointCloud",_simRemovePointsFromPointCloud,"int totalPointCnt=sim.removePointsFromPointCloud(int pointCloudHandle,int options,float[] points,float tolerance)",true},
    {"sim.intersectPointsWithPointCloud",_simIntersectPointsWithPointCloud,"int totalPointCnt=sim.intersectPointsWithPointCloud(int pointCloudHandle,int options,float[] points,float tolerance)",true},
    {"sim.getOctreeVoxels",_simGetOctreeVoxels,                  "float[] voxels=sim.getOctreeVoxels(int octreeHandle)",true},
    {"sim.getPointCloudPoints",_simGetPointCloudPoints,          "float[] points=sim.getPointCloudPoints(int pointCloudHandle)",true},
    {"sim.insertObjectIntoOctree",_simInsertObjectIntoOctree,    "int totalVoxelCnt=sim.insertObjectIntoOctree(int octreeHandle,int objectHandle,int options,float[] color=nil,int tag=0)",true},
    {"sim.subtractObjectFromOctree",_simSubtractObjectFromOctree,    "int totalVoxelCnt=sim.subtractObjectFromOctree(int octreeHandle,int objectHandle,int options)",true},
    {"sim.insertObjectIntoPointCloud",_simInsertObjectIntoPointCloud,"int totalPointCnt=sim.insertObjectIntoPointCloud(int pointCloudHandle,int objectHandle,int options,float gridSize,float[] color=nil,float duplicateTolerance=nil)",true},
    {"sim.subtractObjectFromPointCloud",_simSubtractObjectFromPointCloud,    "int totalPointCnt=sim.subtractObjectFromPointCloud(int pointCloudHandle,int objectHandle,int options,float tolerance)",true},
    {"sim.checkOctreePointOccupancy",_simCheckOctreePointOccupancy,"int result,int tag,int locationLow,int locationHigh=sim.checkOctreePointOccupancy(int octreeHandle,int options,float[] points)",true},
    {"sim.handleSimulationStart",_simHandleSimulationStart,      "sim.handleSimulationStart()",true},
    {"sim.handleSensingStart",_simHandleSensingStart,            "sim.handleSensingStart()",true},
    {"sim.auxFunc",_simAuxFunc,                                  "reserved function - do not use",true},
    {"sim.setReferencedHandles",_simSetReferencedHandles,        "sim.setReferencedHandles(int objectHandle,int[] referencedHandles)",true},
    {"sim.getReferencedHandles",_simGetReferencedHandles,        "int[] referencedHandles=sim.getReferencedHandles(int objectHandle)",true},
    {"sim.getShapeViz",_simGetShapeViz,                          "map data=sim.getShapeViz(int shapeHandle,int itemIndex)",true},
    {"sim.executeScriptString",_simExecuteScriptString,          "int result,any value=sim.executeScriptString(string stringAtScriptName,int scriptHandleOrType)",true},
    {"sim.getApiFunc",_simGetApiFunc,                            "string[] funcsAndVars=sim.getApiFunc(int scriptHandleOrType,string apiWord)",true},
    {"sim.getApiInfo",_simGetApiInfo,                            "string info=sim.getApiInfo(int scriptHandleOrType,string apiWord)",true},
    {"sim.getModuleInfo",_simGetModuleInfo,                      "string info=sim.getModuleInfo(string moduleName,int infoType)\nnumber info=sim.getModuleInfo(string moduleName,int infoType)",true},
    {"sim.setModuleInfo",_simSetModuleInfo,                      "sim.setModuleInfo(string moduleName,int infoType,string info)\nsim.setModuleInfo(string moduleName,int infoType,number info)",true},
    {"sim.registerScriptFunction",_simRegisterScriptFunction,    "int result=sim.registerScriptFunction(string funcNameAtPluginName,string callTips)",true},
    {"sim.registerScriptVariable",_simRegisterScriptVariable,    "int result=sim.registerScriptVariable(string varNameAtPluginName)",true},
    {"sim.registerScriptFuncHook",_simRegisterScriptFuncHook,    "int result=sim.registerScriptFuncHook(string funcToHook,func userFunc,bool execBefore)",true},
    {"sim.isDeprecated",_simIsDeprecated,                        "int result=sim.isDeprecated(string funcOrConst)",true},
    {"sim.getPersistentDataTags",_simGetPersistentDataTags,      "string[] tags=sim.getPersistentDataTags()",true},
    {"sim.getRandom",_simGetRandom,                              "float randomNumber=sim.getRandom(int seed=nil)",true},
    {"sim.textEditorOpen",_simTextEditorOpen,                    "int handle=sim.textEditorOpen(string initText,string properties)",true},
    {"sim.textEditorClose",_simTextEditorClose,                  "string text,int[2] pos,int[2] size=sim.textEditorClose(int handle)",true},
    {"sim.textEditorShow",_simTextEditorShow,                    "sim.textEditorShow(int handle,bool showState)",true},
    {"sim.textEditorGetInfo",_simTextEditorGetInfo,              "string text,int[2] pos,int[2] size,bool visible=sim.textEditorGetInfo(int handle)",true},
    {"sim.setJointDependency",_simSetJointDependency,            "sim.setJointDependency(int jointHandle,int masterJointHandle,float offset,float multCoeff)",true},
    {"sim.getJointDependency",_simGetJointDependency,            "int masterJointHandle,float offset,float multCoeff=sim.getJointDependency(int jointHandle)",true},
    {"sim.getStackTraceback",_simGetStackTraceback,              "string stacktraceback=sim.getStackTraceback(int scriptHandle=sim.handle_self)",true},
    {"sim.addLog",_simAddLog,                                    "sim.addLog(int verbosityLevel,string logMessage)",true},
    {"sim.getShapeMass",_simGetShapeMass,                        "float mass=sim.getShapeMassAndInertia(int shapeHandle)",true},
    {"sim.setShapeMass",_simSetShapeMass,                        "sim.setShapeMass(int shapeHandle,float mass)",true},
    {"sim.getShapeInertia",_simGetShapeInertia,                  "float[9] inertiaMatrix,float[12] transformationMatrix=sim.getShapeInertia(int shapeHandle)",true},
    {"sim.setShapeInertia",_simSetShapeInertia,                  "sim.setShapeInertia(int shapeHandle,float[9] inertiaMatrix,float[12] transformationMatrix)",true},
    {"sim.isDynamicallyEnabled",_simIsDynamicallyEnabled,        "bool enabled=sim.isDynamicallyEnabled(int objectHandle)",true},
    {"sim.generateShapeFromPath",_simGenerateShapeFromPath,      "int shapeHandle=sim.generateShapeFromPath(float[] path,float[] section,int options=0,float[3] upVector={0.0,0.0,1.0})",true},
    {"sim.getClosestPosOnPath",_simGetClosestPosOnPath,          "float posAlongPath=sim.getClosestPosOnPath(float[] path,float[] pathLengths,float[3] absPt)",true},
    {"sim.initScript",_simInitScript,                            "bool result=sim.initScript(int scriptHandle)",true},
    {"sim.moduleEntry",_simModuleEntry,                          "int handle=sim.moduleEntry(int handle,string label=nil,int state=-1)",true},
    {"sim.pushUserEvent",_simPushUserEvent,                      "sim.pushUserEvent(string event,int handle,int uid,map eventData,int options=0)",true},
    {"sim.createCollectionEx",_simCreateCollectionEx,            "",false}, // implemented in sim.lua
    {"sim.getGenesisEvents",_simGetGenesisEvents,                "map[] events=sim.getGenesisEvents()\nbuffer events=sim.getGenesisEvents()",true},
    {"sim.broadcastMsg",_simBroadcastMsg,                        "sim.broadcastMsg(map message,int options=0)",true},
    {"sim.handleJointMotion",_simHandleJointMotion,              "sim.handleJointMotion()",true},
    {"sim.getVisionSensorRes",_simGetVisionSensorRes,            "sim.getVisionSensorRes(int sensorHandle)",true},

    {"sim.test",_simTest,                                        "test function - do not use",true},

    // deprecated
    {"sim.getObjectSelection",_simGetObjectSel,                 "Deprecated. Use sim.getObjectSel instead",false},
    {"sim.setObjectSelection",_simSetObjectSel,                 "Deprecated. Use sim.setObjectSel instead",false},
    {"sim.rmlPos",_simRuckigPos,                                "Deprecated. Use sim.ruckigPos instead",false},
    {"sim.rmlVel",_simRuckigVel,                                "Deprecated. Use sim.ruckigVel instead",false},
    {"sim.rmlStep",_simRuckigStep,                              "Deprecated. Use sim.ruckigStep instead",false},
    {"sim.rmlRemove",_simRuckigRemove,                          "Deprecated. Use sim.ruckigRemove instead",false},
    {"sim.addStatusbarMessage",_simAddStatusbarMessage,         "Deprecated. Use sim.addLog instead",false},
    {"sim.getNameSuffix",_simGetNameSuffix,                     "Deprecated",false},
    {"sim.setNameSuffix",_simSetNameSuffix,                     "Deprecated",false},
    {"sim.resetMill",_simResetMill,                             "Deprecated. Has no effect",false},
    {"sim.resetMilling",_simResetMilling,                       "Deprecated. Has no effect",false},
    {"sim.handleMill",_simHandleMill,                           "Deprecated. Has no effect",false},
    {"sim.openTextEditor",_simOpenTextEditor,                    "Deprecated. Use sim.textEditorOpen instead",false},
    {"sim.closeTextEditor",_simCloseTextEditor,                  "Deprecated. Use sim.textEditorClose instead",false},
    {"simHandlePath",_simHandlePath,                                "Deprecated",false},
    {"simHandleJoint",_simHandleJoint,                              "Deprecated",false},
    {"sim.getScriptSimulationParameter",_simGetScriptSimulationParameter,"Deprecated. Use sim.getUserParameter instead",false},
    {"sim.setScriptSimulationParameter",_simSetScriptSimulationParameter,"Deprecated. Use sim.setUserParameter instead",false},
    {"sim.setJointForce",_simSetJointMaxForce,                   "Deprecated. Use sim.setJointTargetForce instead",false},
    {"sim.handleMechanism",_simHandleMechanism,                  "Deprecated. Has no effect.",false},
    {"sim.setPathTargetNominalVelocity",_simSetPathTargetNominalVelocity,"Deprecated",false},
    {"sim.getShapeMassAndInertia",_simGetShapeMassAndInertia,    "Deprecated. Use sim.getShapeMass and/or sim.getShapeInertia instead",false},
    {"sim.setShapeMassAndInertia",_simSetShapeMassAndInertia,    "Deprecated. Use sim.setShapeMass and/or sim.setShapeInertia instead",false},
    {"sim.checkIkGroup",_simCheckIkGroup,                        "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.createIkGroup",_simCreateIkGroup,                      "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.removeIkGroup",_simRemoveIkGroup,                      "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.createIkElement",_simCreateIkElement,                  "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.exportIk",_simExportIk,                                "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.computeJacobian",_simComputeJacobian,                  "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.getConfigForTipPose",_simGetConfigForTipPose,          "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.generateIkPath",_simGenerateIkPath,                    "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.getIkGroupHandle",_simGetIkGroupHandle,                "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.getIkGroupMatrix",_simGetIkGroupMatrix,                "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.handleIkGroup",_simHandleIkGroup,                      "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.setIkGroupProperties",_simSetIkGroupProperties,        "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.setIkElementProperties",_simSetIkElementProperties,    "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"sim.setThreadIsFree",_simSetThreadIsFree,                  "Deprecated. Has no effect.",false},
    {"sim._tubeRead",_simTubeRead,                               "",false},
    {"sim.tubeOpen",_simTubeOpen,                                "Deprecated. Use signals or custom data blocks instead",false},
    {"sim.tubeClose",_simTubeClose,                              "Deprecated. Use signals or custom data blocks instead",false},
    {"sim.tubeWrite",_simTubeWrite,                              "Deprecated. Use signals or custom data blocks instead",false},
    {"sim.tubeStatus",_simTubeStatus,                            "Deprecated. Use signals or custom data blocks instead",false},
    {"sim.sendData",_simSendData,                                "Deprecated. Use signals or custom data blocks instead",false},
    {"sim.receiveData",_simReceiveData,                          "Deprecated. Use signals or custom data blocks instead",false},
    {"sim._moveToJointPos_1",_sim_moveToJointPos_1,              "",false},
    {"sim._moveToJointPos_2",_sim_moveToJointPos_2,              "",false},
    {"sim._moveToPos_1",_sim_moveToPos_1,                        "",false},
    {"sim._moveToPos_2",_sim_moveToPos_2,                        "",false},
    {"sim._moveToObj_1",_sim_moveToObj_1,                        "",false},
    {"sim._moveToObj_2",_sim_moveToObj_2,                        "",false},
    {"sim._followPath_1",_sim_followPath_1,                      "",false},
    {"sim._followPath_2",_sim_followPath_2,                      "",false},
    {"sim._del",_sim_del,                                        "",false},

    {"sim.getPathPosition",_simGetPathPosition,                  "Deprecated",false},
    {"sim.setPathPosition",_simSetPathPosition,                  "Deprecated",false},
    {"sim.getPathLength",_simGetPathLength,                      "Deprecated",false},
    {"sim.getDataOnPath",_simGetDataOnPath,                      "Deprecated",false},
    {"sim.getPositionOnPath",_simGetPositionOnPath,              "Deprecated",false},
    {"sim.getOrientationOnPath",_simGetOrientationOnPath,        "Deprecated",false},
    {"sim.getClosestPositionOnPath",_simGetClosestPositionOnPath,"Deprecated",false},
    {"sim._createPath",_sim_CreatePath,                          "Deprecated",false},
    {"sim.insertPathCtrlPoints",_simInsertPathCtrlPoints,        "Deprecated",false},
    {"sim.cutPathCtrlPoints",_simCutPathCtrlPoints,              "Deprecated",false},
    {"sim.getScriptExecutionCount",_simGetScriptExecutionCount,  "Deprecated",false},
    {"sim.isScriptExecutionThreaded",_simIsScriptExecutionThreaded,"Deprecated",false},
    {"sim.isScriptRunningInThread",_simIsScriptRunningInThread,  "Deprecated",false},
    {"sim.setThreadResumeLocation",_simSetThreadResumeLocation,  "Deprecated",false},
    {"sim.resumeThreads",_simResumeThreads,                      "Deprecated",false},
    {"sim.launchThreadedChildScripts",_simLaunchThreadedChildScripts,"Deprecated",false},
    {"sim.getUserParameter",_simGetUserParameter,                "Deprecated. Use sim.readCustomDataBlock instead",false},
    {"sim.setUserParameter",_simSetUserParameter,                "Deprecated. Use sim.writeCustomDataBlock instead",false},
    {"sim.setVisionSensorFilter",_simSetVisionSensorFilter,      "Deprecated. Use vision callback functions instead",false},
    {"sim.getVisionSensorFilter",_simGetVisionSensorFilter,      "Deprecated. Use vision callback functions instead",false},
    {"sim.getCollectionHandle",_simGetCollectionHandle,          "Deprecated. Use sim.createCollection instead",false},
    {"sim.removeCollection",_simRemoveCollection,                "Deprecated. Use sim.destroyCollection instead",false},
    {"sim.emptyCollection",_simEmptyCollection,                  "Deprecated. Use sim.destroyCollection instead",false},
    {"sim.getCollectionName",_simGetCollectionName,              "Deprecated",false},
    {"sim.setCollectionName",_simSetCollectionName,              "Deprecated",false},
    {"sim._createCollection",_sim_CreateCollection,              "",false},
    {"sim.addObjectToCollection",_simAddObjectToCollection,      "Deprecated. Use sim.addItemToCollection instead",false},
    {"sim.getCollisionHandle",_simGetCollisionHandle,            "Deprecated. Use sim.checkCollision instead",false},
    {"sim.getDistanceHandle",_simGetDistanceHandle,              "Deprecated. Use sim.checkDistance instead",false},
    {"sim.handleCollision",_simHandleCollision,                  "Deprecated. Use sim.checkCollision instead",false},
    {"sim.readCollision",_simReadCollision,                      "Deprecated. Use sim.checkCollision instead",false},
    {"sim.handleDistance",_simHandleDistance,                    "Deprecated. Use sim.checkDistance instead",false},
    {"sim.readDistance",_simReadDistance,                        "Deprecated. Use sim.checkDistance instead",false},
    {"sim.resetCollision",_simResetCollision,                    "Deprecated",false},
    {"sim.resetDistance",_simResetDistance,                      "Deprecated",false},
    {"sim.addBanner",_simAddBanner,                              "Deprecated",false},
    {"sim.removeBanner",_simRemoveBanner,                        "Deprecated",false},
    {"sim.addGhost",_simAddGhost,                                "Deprecated",false},
    {"sim.modifyGhost",_simModifyGhost,                          "Deprecated",false},
    {"sim.addPointCloud",_simAddPointCloud,                      "Deprecated. Use point cloud objects instead",false},
    {"sim.modifyPointCloud",_simModifyPointCloud,                "Deprecated. Use point cloud objects instead",false},
    {"sim.setGraphUserData",_simSetGraphUserData,                "Deprecated. Use sim.setGraphStreamValue instead",false},
    {"sim.copyMatrix",_simCopyMatrix,                            "Deprecated. Use sim.copyTable instead",false},
    {"sim.getObjectInt32Parameter",_simGetObjectInt32Parameter,  "Deprecated. Use sim.getObjectInt32Param instead",false},
    {"sim.setObjectInt32Parameter",_simSetObjectInt32Param,      "Deprecated. Use sim.setObjectInt32Param instead",false},
    {"sim.getObjectFloatParameter",_simGetObjectFloatParameter,  "Deprecated. Use sim.getObjectFloatParam instead",false},
    {"sim.setObjectFloatParameter",_simSetObjectFloatParam,      "Deprecated. Use sim.setObjectFloatParam instead",false},
    {"sim.getObjectStringParameter",_simGetObjectStringParam,    "Deprecated. Use sim.getObjectStringParam instead",false},
    {"sim.setObjectStringParameter",_simSetObjectStringParam,    "Deprecated. Use sim.setObjectStringParam instead",false},
    {"sim.setBoolParameter",_simSetBoolParam,                    "Deprecated. Use sim.setBoolParam instead",false},
    {"sim.getBoolParameter",_simGetBoolParam,                    "Deprecated. Use sim.getBoolParam instead",false},
    {"sim.setInt32Parameter",_simSetInt32Param,                  "Deprecated. Use sim.setInt32Param instead",false},
    {"sim.getInt32Parameter",_simGetInt32Param,                  "Deprecated. Use sim.getInt32Param instead",false},
    {"sim.setFloatParameter",_simSetFloatParam,                  "Deprecated. Use sim.setFloatParam instead",false},
    {"sim.getFloatParameter",_simGetFloatParam,                  "Deprecated. Use sim.getFloatParam instead",false},
    {"sim.setStringParameter",_simSetStringParam,                "Deprecated. Use sim.setStringParam instead",false},
    {"sim.getStringParameter",_simGetStringParam,                "Deprecated. Use sim.getStringParam instead",false},
    {"sim.setArrayParameter",_simSetArrayParam,                  "Deprecated. Use sim.setArrayParam instead",false},
    {"sim.getArrayParameter",_simGetArrayParam,                  "Deprecated. Use sim.getArrayParam instead",false},
    {"sim.getEngineFloatParameter",_simGetEngineFloatParam,      "Deprecated. Use sim.getEngineFloatParam instead",false},
    {"sim.getEngineInt32Parameter",_simGetEngineInt32Param,      "Deprecated. Use sim.getEngineInt32Param instead",false},
    {"sim.getEngineBoolParameter",_simGetEngineBoolParam,        "Deprecated. Use sim.getEngineBoolParam instead",false},
    {"sim.setEngineFloatParameter",_simSetEngineFloatParam,      "Deprecated. Use sim.setEngineFloatParam instead",false},
    {"sim.setEngineInt32Parameter",_simSetEngineInt32Param,      "Deprecated. Use sim.setEngineInt32Param instead",false},
    {"sim.setEngineBoolParameter",_simSetEngineBoolParam,        "Deprecated. Use sim.setEngineBoolParam instead",false},
    {"sim.isHandleValid",_simIsHandleValid,                      "Deprecated. Use sim.isHandle instead",false},
    {"sim.setIntegerSignal",_simSetInt32Signal,                  "Deprecated. Use sim.setInt32Signal instead",false},
    {"sim.getIntegerSignal",_simGetInt32Signal,                  "Deprecated. Use sim.getInt32Signal instead",false},
    {"sim.clearIntegerSignal",_simClearInt32Signal,              "Deprecated. Use sim.clearInt32Signal instead",false},
    {"sim.getObjectName",_simGetObjectName,                      "Deprecated. Use sim.getObjectAlias instead",false},
    {"sim.setObjectName",_simSetObjectName,                      "Deprecated. Use sim.setObjectAlias instead",false},
    {"sim.getScriptName",_simGetScriptName,                      "Deprecated. Use sim.getObjectAlias instead",false},
    {"sim.setScriptVariable",_simSetScriptVariable,              "Deprecated. Use sim.executeScriptString instead",false},
    {"sim.getObjectAssociatedWithScript",_simGetObjectAssociatedWithScript,"Deprecated. Use sim.getObjectHandle('.') instead",false},
    {"sim.getScriptAssociatedWithObject",_simGetScriptAssociatedWithObject,"Deprecated. Use sim.getScriptHandle instead",false},
    {"sim.getCustomizationScriptAssociatedWithObject",_simGetCustomizationScriptAssociatedWithObject,"Deprecated. Use sim.getScriptHandle instead",false},
    {"sim.getObjectConfiguration",_simGetObjectConfiguration,    "Deprecated",false},
    {"sim.setObjectConfiguration",_simSetObjectConfiguration,    "Deprecated",false},
    {"sim.getConfigurationTree",_simGetConfigurationTree,        "Deprecated",false},
    {"sim.setConfigurationTree",_simSetConfigurationTree,        "Deprecated",false},
    {"sim.setObjectSizeValues",_simSetObjectSizeValues,          "Deprecated",false},
    {"sim.getObjectSizeValues",_simGetObjectSizeValues,          "Deprecated",false},
    {"sim.openModule",_simOpenModule,                            "Deprecated",false},
    {"sim.closeModule",_simCloseModule,                          "Deprecated",false},
    {"sim.handleModule",_simHandleModule,                        "Deprecated",false},
    {"sim.getLastError",_simGetLastError,                        "Deprecated",false},
    {"sim._switchThread",_simSwitchThread,                       "Deprecated",false},
    {"sim.getSystemTimeInMs",_simGetSystemTimeInMs,              "Deprecated. Use sim.getSystemTime Instead",false},
    {"sim.fileDialog",_simFileDialog,                            "Deprecated. Use simUI.fileDialog instead",false},
    {"sim.msgBox",_simMsgBox,                                    "Deprecated. Use simUI.msgBox instead",false},
    {"sim.isObjectInSelection",_simIsObjectInSelection,          "Deprecated. Use sim.getObjectSelection instead",false},
    {"sim.addObjectToSelection",_simAddObjectToSelection,        "Deprecated. Use sim.setObjectSelection instead",false},
    {"sim.removeObjectFromSelection",_simRemoveObjectFromSelection,"Deprecated. Use sim.setObjectSelection instead",false},
    {"sim.getObjectUniqueIdentifier",_simGetObjectUniqueIdentifier,"Deprecated. Use sim.getObjectInt32Param with sim.objintparam_unique_id instead",false},
    {"sim.breakForceSensor",_simBreakForceSensor,                "Deprecated. Use sim.setObjectParent instead",false},
    {"sim.getJointMatrix",_simGetJointMatrix,                    "Deprecated. Use sim.getObjectChildPose instead",false},
    {"sim.setSphericalJointMatrix",_simSetSphericalJointMatrix,  "Deprecated. Use sim.setObjectChildPose instead",false},
    {"sim.setScriptAttribute",_simSetScriptAttribute,            "Deprecated. Use sim.setScriptXXXParam instead",false},
    {"sim.getScriptAttribute",_simGetScriptAttribute,            "Deprecated. Use sim.getScriptXXXParam instead",false},
    {"sim.setScriptText",_simSetScriptText,                      "Deprecated. Use sim.setScriptStringParam instead",false},
    {"sim.getJointMaxForce",_simGetJointMaxForce,                "Deprecated. Use sim.getJointTargetForce instead",false},
    {"sim.setJointMaxForce",_simSetJointMaxForce,                "Deprecated. Use sim.setJointTargetForce instead",false},
    {"sim._getObjectHandle",_sim_getObjectHandle,                "",false}, // handled via sim.getObjectHandle from sim.lua
    {"sim.removeObject",_simRemoveObject,                        "Deprecated. Use sim.removeObjects instead",false},
    {"sim.getVisionSensorResolution",_simGetVisionSensorResolution,"Deprecated. Use sim.getVisionSensorImg instead",false},
    {"sim.getVisionSensorImage",_simGetVisionSensorImage,        "Deprecated. Use sim.getVisionSensorImg instead",false},
    {"sim.setVisionSensorImage",_simSetVisionSensorImage,        "Deprecated. Use sim.setVisionSensorImg instead",false},
    {"sim.getVisionSensorCharImage",_simGetVisionSensorCharImage,"Deprecated. Use sim.getVisionSensorImg instead",false},
    {"sim.setVisionSensorCharImage",_simSetVisionSensorCharImage,"Deprecated. Use sim.setVisionSensorImg instead",false},
    {"sim.getVisionSensorDepthBuffer",_simGetVisionSensorDepthBuffer,"Deprecated. Use sim.getVisionSensorDepth instead",false},
    {"sim.createPureShape",_simCreatePureShape,                  "Deprecated. Use sim.createPrimitiveShape instead",false},
    {"sim.getScriptHandle",_simGetScriptHandle,                  "Deprecated. Use sim.getScript instead",false},
    {"sim.handleCustomizationScripts",_simHandleCustomizationScripts,"Deprecated. Use sim.handleEmbeddedScripts instead",false},
    {"sim.setDoubleSignal",_simSetDoubleSignal,                  "Deprecated. Use sim.setFloatSignal instead",false},
    {"sim.getDoubleSignal",_simGetDoubleSignal,                  "Deprecated. Use sim.getFloatSignal instead",false},
    {"sim.clearDoubleSignal",_simClearDoubleSignal,              "Deprecated. Use sim.clearFloatSignal instead",false},

    {"",nullptr,"",false}
};

const SLuaVariables simLuaVariables[]=
{
    // Scene object types (main types):
    {"sim.object_shape_type",sim_object_shape_type,true},
    {"sim.object_joint_type",sim_object_joint_type,true},
    {"sim.object_graph_type",sim_object_graph_type,true},
    {"sim.object_camera_type",sim_object_camera_type,true},
    {"sim.object_dummy_type",sim_object_dummy_type,true},
    {"sim.object_proximitysensor_type",sim_object_proximitysensor_type,true},
    {"sim.object_path_type",sim_object_path_type,false},
    {"sim.object_renderingsensor_type",sim_object_visionsensor_type,true},
    {"sim.object_visionsensor_type",sim_object_visionsensor_type,true},
    {"sim.object_mill_type",sim_object_mill_type,false},
    {"sim.object_forcesensor_type",sim_object_forcesensor_type,true},
    {"sim.object_light_type",sim_object_light_type,true},
    {"sim.object_mirror_type",sim_object_mirror_type,false},
    {"sim.object_octree_type",sim_object_octree_type,true},
    {"sim.object_pointcloud_type",sim_object_pointcloud_type,true},
    // 3D object sub-types:
    {"sim.light_omnidirectional_subtype",sim_light_omnidirectional_subtype,true},
    {"sim.light_spot_subtype",sim_light_spot_subtype,true},
    {"sim.light_directional_subtype",sim_light_directional_subtype,true},
    {"sim.joint_revolute_subtype",sim_joint_revolute_subtype,true},
    {"sim.joint_prismatic_subtype",sim_joint_prismatic_subtype,true},
    {"sim.joint_spherical_subtype",sim_joint_spherical_subtype,true},
    {"sim.shape_simpleshape_subtype",sim_shape_simpleshape_subtype,true},
    {"sim.shape_multishape_subtype",sim_shape_multishape_subtype,true},
    {"sim.proximitysensor_pyramid_subtype",sim_proximitysensor_pyramid_subtype,true},
    {"sim.proximitysensor_cylinder_subtype",sim_proximitysensor_cylinder_subtype,true},
    {"sim.proximitysensor_disc_subtype",sim_proximitysensor_disc_subtype,true},
    {"sim.proximitysensor_cone_subtype",sim_proximitysensor_cone_subtype,true},
    {"sim.proximitysensor_ray_subtype",sim_proximitysensor_ray_subtype,true},
    {"sim.mill_pyramid_subtype",sim_mill_pyramid_subtype,false},
    {"sim.mill_cylinder_subtype",sim_mill_cylinder_subtype,false},
    {"sim.mill_disc_subtype",sim_mill_disc_subtype,false},
    {"sim.mill_cone_subtype",sim_mill_cone_subtype,false},
    {"sim.object_no_subtype",sim_object_no_subtype,true},
    // Other object types:
    {"sim.appobj_object_type",sim_appobj_object_type,true},
    {"sim.appobj_simulation_type",sim_appobj_simulation_type,true},
    {"sim.appobj_script_type",sim_appobj_script_type,true},
    {"sim.appobj_texture_type",sim_appobj_texture_type,true},
    // Simulation messages:
    {"sim.message_model_loaded",sim_message_model_loaded,true},
    {"sim.message_scene_loaded",sim_message_scene_loaded,true},
    {"sim.message_object_selection_changed",sim_message_object_selection_changed,true},
    {"sim.message_keypress",sim_message_keypress,true},
    // object properties. Combine with the | operator
    {"sim.objectproperty_collapsed",sim_objectproperty_collapsed,true},
    {"sim.objectproperty_selectable",sim_objectproperty_selectable,true},
    {"sim.objectproperty_selectmodelbaseinstead",sim_objectproperty_selectmodelbaseinstead,true},
    {"sim.objectproperty_dontshowasinsidemodel",sim_objectproperty_dontshowasinsidemodel,true},
    {"sim.objectproperty_selectinvisible",sim_objectproperty_selectinvisible,true},
    {"sim.objectproperty_depthinvisible",sim_objectproperty_depthinvisible,true},
    {"sim.objectproperty_cannotdelete",sim_objectproperty_cannotdelete,true},
    {"sim.objectproperty_cannotdeleteduringsim",sim_objectproperty_cannotdeleteduringsim,true},
    {"sim.objectproperty_ignoreviewfitting",sim_objectproperty_ignoreviewfitting,true},
    // Simulation status:
    {"sim.simulation_stopped",sim_simulation_stopped,true},
    {"sim.simulation_paused",sim_simulation_paused,true},
    {"sim.simulation_advancing",sim_simulation_advancing,true},
    {"sim.simulation_advancing_firstafterstop",sim_simulation_advancing_firstafterstop,true},
    {"sim.simulation_advancing_running",sim_simulation_advancing_running,true},
    {"sim.simulation_advancing_lastbeforepause",sim_simulation_advancing_lastbeforepause,true},
    {"sim.simulation_advancing_firstafterpause",sim_simulation_advancing_firstafterpause,true},
    {"sim.simulation_advancing_abouttostop",sim_simulation_advancing_abouttostop,true},
    {"sim.simulation_advancing_lastbeforestop",sim_simulation_advancing_lastbeforestop,true},
    // Texture mapping modes:
    {"sim.texturemap_plane",sim_texturemap_plane,true},
    {"sim.texturemap_cylinder",sim_texturemap_cylinder,true},
    {"sim.texturemap_sphere",sim_texturemap_sphere,true},
    {"sim.texturemap_cube",sim_texturemap_cube,true},
    // script types:
    {"sim.scripttype_mainscript",sim_scripttype_mainscript,true},
    {"sim.scripttype_childscript",sim_scripttype_childscript,true},
    {"sim.scripttype_addonscript",sim_scripttype_addonscript,true},
    {"sim.scripttype_customizationscript",sim_scripttype_customizationscript,true},
    {"sim.scripttype_sandboxscript",sim_scripttype_sandboxscript,true},
    // System callbacks
    {"sim.syscb_init",sim_syscb_init,true},
    {"sim.syscb_cleanup",sim_syscb_cleanup,true},
    {"sim.syscb_nonsimulation",sim_syscb_nonsimulation,true},
    {"sim.syscb_beforemainscript",sim_syscb_beforemainscript,true},
    {"sim.syscb_beforesimulation",sim_syscb_beforesimulation,true},
    {"sim.syscb_aftersimulation",sim_syscb_aftersimulation,true},
    {"sim.syscb_actuation",sim_syscb_actuation,true},
    {"sim.syscb_sensing",sim_syscb_sensing,true},
    {"sim.syscb_suspended",sim_syscb_suspended,true},
    {"sim.syscb_suspend",sim_syscb_suspend,true},
    {"sim.syscb_resume",sim_syscb_resume,true},
    {"sim.syscb_beforeinstanceswitch",sim_syscb_beforeinstanceswitch,true},
    {"sim.syscb_afterinstanceswitch",sim_syscb_afterinstanceswitch,true},
    {"sim.syscb_beforecopy",sim_syscb_beforecopy,true},
    {"sim.syscb_aftercopy",sim_syscb_aftercopy,true},
    {"sim.syscb_aos_suspend",sim_syscb_aos_suspend,true},
    {"sim.syscb_aos_resume",sim_syscb_aos_resume,true},
    {"sim.syscb_joint",sim_syscb_joint,true},
    {"sim.syscb_vision",sim_syscb_vision,true},
    {"sim.syscb_trigger",sim_syscb_trigger,true},
    {"sim.syscb_contact",sim_syscb_contact,true},
    {"sim.syscb_dyn",sim_syscb_dyn,true},
    {"sim.syscb_beforedelete",sim_syscb_beforedelete,true},
    {"sim.syscb_afterdelete",sim_syscb_afterdelete,true},
    {"sim.syscb_aftercreate",sim_syscb_aftercreate,true},
    //{"sim.syscb_threadmain",sim_syscb_threadmain,false},
    {"sim.syscb_userconfig",sim_syscb_userconfig,true},
    {"sim.syscb_moduleentry",sim_syscb_moduleentry,true},
    // script params:
    {"sim.scriptintparam_execorder",sim_scriptintparam_execorder,true},
    {"sim.scriptintparam_execcount",sim_scriptintparam_execcount,true},
    {"sim.scriptintparam_type",sim_scriptintparam_type,true},
    {"sim.scriptintparam_handle",sim_scriptintparam_handle,true},
    {"sim.scriptintparam_objecthandle",sim_scriptintparam_objecthandle,true},
    {"sim.scriptintparam_enabled",sim_scriptintparam_enabled,true},
    {"sim.scriptstringparam_description",sim_scriptstringparam_description,true},
    {"sim.scriptstringparam_name",sim_scriptstringparam_name,true},
    {"sim.scriptstringparam_text",sim_scriptstringparam_text,true},
    // script execution order:
    {"sim.scriptexecorder_first",sim_scriptexecorder_first,true},
    {"sim.scriptexecorder_normal",sim_scriptexecorder_normal,true},
    {"sim.scriptexecorder_last",sim_scriptexecorder_last,true},
    // special arguments for some functions:
    {"sim.handle_world",sim_handle_world,true},
    {"sim.handle_all",sim_handle_all,true},
    {"sim.handle_all_except_explicit",sim_handle_all_except_explicit,true},
    {"sim.handle_self",sim_handle_self,true},
    {"sim.handle_main_script",sim_handle_main_script,true},
    {"sim.handle_tree",sim_handle_tree,true},
    {"sim.handle_chain",sim_handle_chain,true},
    {"sim.handle_single",sim_handle_single,true},
    {"sim.handle_default",sim_handle_default,true},
    {"sim.handle_all_except_self",sim_handle_all_except_self,true},
    {"sim.handle_parent",sim_handle_parent,true},
    {"sim.handle_scene",sim_handle_scene,true},
    {"sim.handle_app",sim_handle_app,true},
    {"sim.handle_inverse",sim_handle_inverse,true},
    // special handle flags:
    {"sim.handleflag_assembly",sim_handleflag_assembly,true},
    {"sim.handleflag_camera",sim_handleflag_camera,true},
    {"sim.handleflag_togglevisibility",sim_handleflag_togglevisibility,true},
    {"sim.handleflag_extended",sim_handleflag_extended,true},
    {"sim.handleflag_greyscale",sim_handleflag_greyscale,true},
    {"sim.handleflag_depthbuffermeters",sim_handleflag_depthbuffermeters,true},
    {"sim.handleflag_depthbuffer",sim_handleflag_depthbuffer,true},
    {"sim.handleflag_keeporiginal",sim_handleflag_keeporiginal,true},
    {"sim.handleflag_codedstring",sim_handleflag_codedstring,true},
    {"sim.handleflag_wxyzquat",sim_handleflag_wxyzquat,true},
    {"sim.handleflag_reljointbaseframe",sim_handleflag_reljointbaseframe,true},
    {"sim.handleflag_setmultiple",sim_handleflag_setmultiple,true},
    {"sim.handleflag_addmultiple",sim_handleflag_addmultiple,true},
    {"sim.handleflag_abscoords",sim_handleflag_abscoords,true},
    {"sim.handleflag_model",sim_handleflag_model,true},
    {"sim.handleflag_rawvalue",sim_handleflag_rawvalue,true},
    {"sim.handleflag_altname",sim_handleflag_altname,true},
    {"sim.handleflag_axis",sim_handleflag_axis,true},
    {"sim.handleflag_resetforce",sim_handleflag_resetforce,true},
    {"sim.handleflag_resettorque",sim_handleflag_resettorque,true},
    {"sim.handleflag_resetforcetorque",sim_handleflag_resetforcetorque,true},
    {"sim.handleflag_silenterror",sim_handleflag_silenterror,true},
    // General object main properties:
    {"sim.objectspecialproperty_collidable",sim_objectspecialproperty_collidable,true},
    {"sim.objectspecialproperty_measurable",sim_objectspecialproperty_measurable,true},
    {"sim.objectspecialproperty_detectable",sim_objectspecialproperty_detectable,true},
    // Model override properties:
    {"sim.modelproperty_not_collidable",sim_modelproperty_not_collidable,true},
    {"sim.modelproperty_not_measurable",sim_modelproperty_not_measurable,true},
    {"sim.modelproperty_not_detectable",sim_modelproperty_not_detectable,true},
    {"sim.modelproperty_not_dynamic",sim_modelproperty_not_dynamic,true},
    {"sim.modelproperty_not_respondable",sim_modelproperty_not_respondable,true},
    {"sim.modelproperty_not_reset",sim_modelproperty_not_reset,true},
    {"sim.modelproperty_not_visible",sim_modelproperty_not_visible,true},
    {"sim.modelproperty_scripts_inactive",sim_modelproperty_scripts_inactive,true},
    {"sim.modelproperty_not_showasinsidemodel",sim_modelproperty_not_showasinsidemodel,true},
    {"sim.modelproperty_not_model",sim_modelproperty_not_model,true},
    // bool parameters:
    {"sim.boolparam_hierarchy_visible",sim_boolparam_hierarchy_visible,true},
    {"sim.boolparam_console_visible",sim_boolparam_console_visible,true},
    {"sim.boolparam_dynamics_handling_enabled",sim_boolparam_dynamics_handling_enabled,true},
    {"sim.boolparam_browser_visible",sim_boolparam_browser_visible,true},
    {"sim.boolparam_scene_and_model_load_messages",sim_boolparam_scene_and_model_load_messages,true},
    {"sim.boolparam_shape_textures_are_visible",sim_boolparam_shape_textures_are_visible,true},
    {"sim.boolparam_display_enabled",sim_boolparam_display_enabled,true},
    {"sim.boolparam_infotext_visible",sim_boolparam_infotext_visible,true},
    {"sim.boolparam_statustext_open",sim_boolparam_statustext_open,true},
    {"sim.boolparam_fog_enabled",sim_boolparam_fog_enabled,true},
    {"sim.boolparam_mirrors_enabled",sim_boolparam_mirrors_enabled,true},
    {"sim.boolparam_aux_clip_planes_enabled",sim_boolparam_aux_clip_planes_enabled,true},
    {"sim.boolparam_full_model_copy_from_api",sim_boolparam_reserved3,true},
    {"sim.boolparam_realtime_simulation",sim_boolparam_realtime_simulation,true},
    {"sim.boolparam_scene_closing",sim_boolparam_scene_closing,true},
    {"sim.boolparam_use_glfinish_cmd",sim_boolparam_use_glfinish_cmd,true},
    {"sim.boolparam_video_recording_triggered",sim_boolparam_video_recording_triggered,true},
    {"sim.boolparam_fullscreen",sim_boolparam_fullscreen,true},
    {"sim.boolparam_headless",sim_boolparam_headless,true},
    {"sim.boolparam_hierarchy_toolbarbutton_enabled",sim_boolparam_hierarchy_toolbarbutton_enabled,true},
    {"sim.boolparam_browser_toolbarbutton_enabled",sim_boolparam_browser_toolbarbutton_enabled,true},
    {"sim.boolparam_objectshift_toolbarbutton_enabled",sim_boolparam_objectshift_toolbarbutton_enabled,true},
    {"sim.boolparam_objectrotate_toolbarbutton_enabled",sim_boolparam_objectrotate_toolbarbutton_enabled,true},
    {"sim.boolparam_force_calcstruct_all_visible",sim_boolparam_force_calcstruct_all_visible,true},
    {"sim.boolparam_force_calcstruct_all",sim_boolparam_force_calcstruct_all,true},
    {"sim.boolparam_exit_request",sim_boolparam_exit_request,true},
    {"sim.boolparam_play_toolbarbutton_enabled",sim_boolparam_play_toolbarbutton_enabled,true},
    {"sim.boolparam_pause_toolbarbutton_enabled",sim_boolparam_pause_toolbarbutton_enabled,true},
    {"sim.boolparam_stop_toolbarbutton_enabled",sim_boolparam_stop_toolbarbutton_enabled,true},
    {"sim.boolparam_waiting_for_trigger",sim_boolparam_waiting_for_trigger,true},
    {"sim.boolparam_objproperties_toolbarbutton_enabled",sim_boolparam_objproperties_toolbarbutton_enabled,true},
    {"sim.boolparam_calcmodules_toolbarbutton_enabled",sim_boolparam_calcmodules_toolbarbutton_enabled,true},
    {"sim.boolparam_rosinterface_donotrunmainscript",sim_boolparam_rosinterface_donotrunmainscript,true},
    {"sim.boolparam_rayvalid",sim_boolparam_rayvalid,true},

    // Integer parameters:
    {"sim.intparam_error_report_mode",sim_intparam_error_report_mode,true},
    {"sim.intparam_program_version",sim_intparam_program_version,true},
    {"sim.intparam_program_full_version",sim_intparam_program_full_version,true},
    {"sim.intparam_verbosity",sim_intparam_verbosity,true},
    {"sim.intparam_statusbarverbosity",sim_intparam_statusbarverbosity,true},
    {"sim.intparam_dlgverbosity",sim_intparam_dlgverbosity,true},
    {"sim.intparam_exitcode",sim_intparam_exitcode,true},
    {"sim.intparam_videoencoderindex",sim_intparam_videoencoder_index,true},
    {"sim.intparam_compilation_version",sim_intparam_compilation_version,true},
    {"sim.intparam_current_page",sim_intparam_current_page,true},
    {"sim.intparam_flymode_camera_handle",sim_intparam_flymode_camera_handle,true},
    {"sim.intparam_dynamic_step_divider",sim_intparam_dynamic_step_divider,true},
    {"sim.intparam_dynamic_engine",sim_intparam_dynamic_engine,true},
    {"sim.intparam_server_port_start",sim_intparam_server_port_start,true},
    {"sim.intparam_server_port_range",sim_intparam_server_port_range,true},
    {"sim.intparam_server_port_next",sim_intparam_server_port_next,true},
    {"sim.intparam_visible_layers",sim_intparam_visible_layers,true},
    {"sim.intparam_infotext_style",sim_intparam_infotext_style,true},
    {"sim.intparam_settings",sim_intparam_settings,true},
    {"sim.intparam_qt_version",sim_intparam_qt_version,true},
    {"sim.intparam_platform",sim_intparam_platform,true},
    {"sim.intparam_scene_unique_id",sim_intparam_scene_unique_id,true},
    {"sim.intparam_edit_mode_type",sim_intparam_edit_mode_type,true},
    {"sim.intparam_work_thread_count",sim_intparam_work_thread_count,true}, // deprecated
    {"sim.intparam_mouse_x",sim_intparam_mouse_x,true},
    {"sim.intparam_mouse_y",sim_intparam_mouse_y,true},
    {"sim.intparam_core_count",sim_intparam_core_count,true},
    {"sim.intparam_work_thread_calc_time_ms",sim_intparam_work_thread_calc_time_ms,true}, // deprecated
    {"sim.intparam_idle_fps",sim_intparam_idle_fps,true},
    {"sim.intparam_prox_sensor_select_down",sim_intparam_prox_sensor_select_down,true},
    {"sim.intparam_prox_sensor_select_up",sim_intparam_prox_sensor_select_up,true},
    {"sim.intparam_stop_request_counter",sim_intparam_stop_request_counter,true},
    {"sim.intparam_program_revision",sim_intparam_program_revision,true},
    {"sim.intparam_mouse_buttons",sim_intparam_mouse_buttons,true},
    {"sim.intparam_dynamic_warning_disabled_mask",sim_intparam_dynamic_warning_disabled_mask,true},
    {"sim.intparam_scene_index",sim_intparam_scene_index,true},
    {"sim.intparam_motionplanning_seed",sim_intparam_motionplanning_seed,true},
    {"sim.intparam_speedmodifier",sim_intparam_speedmodifier,true},
    {"sim.intparam_dynamic_iteration_count",sim_intparam_dynamic_iteration_count,true},
    {"sim.intparam_mouseclickcounterdown",sim_intparam_mouseclickcounterdown,true},
    {"sim.intparam_mouseclickcounterup",sim_intparam_mouseclickcounterup,true},
    {"sim.intparam_objectcreationcounter",sim_intparam_objectcreationcounter,true},
    {"sim.intparam_objectdestructioncounter",sim_intparam_objectdestructioncounter,true},
    {"sim.intparam_hierarchychangecounter",sim_intparam_hierarchychangecounter,true},

    // Float parameters:
    {"sim.floatparam_rand",sim_floatparam_rand,true},
    {"sim.floatparam_simulation_time_step",sim_floatparam_simulation_time_step,true},
    {"sim.floatparam_stereo_distance",sim_floatparam_stereo_distance,true},
    {"sim.floatparam_dynamic_step_size",sim_floatparam_dynamic_step_size,true},
    {"sim.floatparam_mouse_wheel_zoom_factor",sim_floatparam_mouse_wheel_zoom_factor,true},
    {"sim.floatparam_physicstimestep",sim_floatparam_physicstimestep,true},

    // Array parameters:
    {"sim.arrayparam_gravity",sim_arrayparam_gravity,true},
    {"sim.arrayparam_fog",sim_arrayparam_fog,true},
    {"sim.arrayparam_fog_color",sim_arrayparam_fog_color,true},
    {"sim.arrayparam_background_color1",sim_arrayparam_background_color1,true},
    {"sim.arrayparam_background_color2",sim_arrayparam_background_color2,true},
    {"sim.arrayparam_ambient_light",sim_arrayparam_ambient_light,true},
    {"sim.arrayparam_random_euler",sim_arrayparam_random_euler,true},
    {"sim.arrayparam_rayorigin",sim_arrayparam_rayorigin,true},
    {"sim.arrayparam_raydirection",sim_arrayparam_raydirection,true},

    // String parameters:
    {"sim.stringparam_application_path",sim_stringparam_application_path,true},
    {"sim.stringparam_video_filename",sim_stringparam_video_filename,true},
    {"sim.stringparam_app_arg1",sim_stringparam_app_arg1,true},
    {"sim.stringparam_app_arg2",sim_stringparam_app_arg2,true},
    {"sim.stringparam_app_arg3",sim_stringparam_app_arg3,true},
    {"sim.stringparam_app_arg4",sim_stringparam_app_arg4,true},
    {"sim.stringparam_app_arg5",sim_stringparam_app_arg5,true},
    {"sim.stringparam_app_arg6",sim_stringparam_app_arg6,true},
    {"sim.stringparam_app_arg7",sim_stringparam_app_arg7,true},
    {"sim.stringparam_app_arg8",sim_stringparam_app_arg8,true},
    {"sim.stringparam_app_arg9",sim_stringparam_app_arg9,true},
    {"sim.stringparam_scene_path_and_name",sim_stringparam_scene_path_and_name,true},
    {"sim.stringparam_remoteapi_temp_file_dir",sim_stringparam_remoteapi_temp_file_dir,true},
    {"sim.stringparam_scene_path",sim_stringparam_scene_path,true},
    {"sim.stringparam_scene_name",sim_stringparam_scene_name,true},
    {"sim.stringparam_scene_unique_id",sim_stringparam_scene_unique_id,true},
    {"sim.stringparam_machine_id",sim_stringparam_machine_id,true},
    {"sim.stringparam_machine_id_legacy",sim_stringparam_machine_id_legacy,true},
    {"sim.stringparam_verbosity",sim_stringparam_verbosity,true},
    {"sim.stringparam_statusbarverbosity",sim_stringparam_statusbarverbosity,true},
    {"sim.stringparam_dlgverbosity",sim_stringparam_dlgverbosity,true},
    {"sim.stringparam_logfilter",sim_stringparam_consolelogfilter,true},
    {"sim.stringparam_uniqueid",sim_stringparam_uniqueid,true},
    {"sim.stringparam_tempdir",sim_stringparam_tempdir,true},
    {"sim.stringparam_tempscenedir",sim_stringparam_tempscenedir,true},
    {"sim.stringparam_datadir",sim_stringparam_datadir,true},
    {"sim.stringparam_importexportdir",sim_stringparam_importexportdir,true},
    {"sim.stringparam_addonpath",sim_stringparam_addonpath,true},
    {"sim.stringparam_scenedefaultdir",sim_stringparam_scenedefaultdir,true},
    {"sim.stringparam_modeldefaultdir",sim_stringparam_modeldefaultdir,true},
    {"sim.stringparam_defaultpython",sim_stringparam_defaultpython,true},
    {"sim.stringparam_additionalpythonpath",sim_stringparam_additionalpythonpath,true},
    {"sim.stringparam_luadir",sim_stringparam_luadir,true},
    {"sim.stringparam_pythondir",sim_stringparam_pythondir,true},
    {"sim.stringparam_mujocodir",sim_stringparam_mujocodir,true},
    {"sim.stringparam_usersettingsdir",sim_stringparam_usersettingsdir,true},
    {"sim.stringparam_systemdir",sim_stringparam_systemdir,true},
    {"sim.stringparam_resourcesdir",sim_stringparam_resourcesdir,true},

    // verbosity:
    {"sim.verbosity_useglobal",sim_verbosity_useglobal,true},
    {"sim.verbosity_none",sim_verbosity_none,true},
    {"sim.verbosity_errors",sim_verbosity_errors,true},
    {"sim.verbosity_warnings",sim_verbosity_warnings,true},
    {"sim.verbosity_loadinfos",sim_verbosity_loadinfos,true},
    {"sim.verbosity_questions",sim_verbosity_questions,true},
    {"sim.verbosity_scripterrors",sim_verbosity_scripterrors,true},
    {"sim.verbosity_scriptwarnings",sim_verbosity_scriptwarnings,true},
    {"sim.verbosity_scriptinfos",sim_verbosity_scriptinfos,true},
    {"sim.verbosity_msgs",sim_verbosity_msgs,true},
    {"sim.verbosity_infos",sim_verbosity_infos,true},
    {"sim.verbosity_debug",sim_verbosity_debug,true},
    {"sim.verbosity_trace",sim_verbosity_trace,true},
    {"sim.verbosity_tracelua",sim_verbosity_tracelua,true},
    {"sim.verbosity_traceall",sim_verbosity_traceall,true},
    {"sim.verbosity_default",sim_verbosity_default,true},
    {"sim.verbosity_undecorated",sim_verbosity_undecorated,true},
    {"sim.verbosity_onlyterminal",sim_verbosity_onlyterminal,true},

    // module info:
    {"sim.moduleinfo_extversionstr",sim_moduleinfo_extversionstr,true},
    {"sim.moduleinfo_builddatestr",sim_moduleinfo_builddatestr,true},
    {"sim.moduleinfo_extversionint",sim_moduleinfo_extversionint,true},
    {"sim.moduleinfo_verbosity",sim_moduleinfo_verbosity,true},
    {"sim.moduleinfo_statusbarverbosity",sim_moduleinfo_statusbarverbosity,true},

    // Rendering attributes:
    {"sim.displayattribute_renderpass",sim_displayattribute_renderpass,true},
    {"sim.displayattribute_depthpass",sim_displayattribute_depthpass,true},
    {"sim.displayattribute_pickpass",sim_displayattribute_pickpass,true},
    {"sim.displayattribute_selected",sim_displayattribute_selected,true},
    {"sim.displayattribute_mainselection",sim_displayattribute_mainselection,true},
    {"sim.displayattribute_forcewireframe",sim_displayattribute_forcewireframe,true},
    {"sim.displayattribute_forbidwireframe",sim_displayattribute_forbidwireframe,true},
    {"sim.displayattribute_forbidedges",sim_displayattribute_forbidedges,true},
    {"sim.displayattribute_originalcolors",sim_displayattribute_originalcolors,true},
    {"sim.displayattribute_ignorelayer",sim_displayattribute_ignorelayer,true},
    {"sim.displayattribute_forvisionsensor",sim_displayattribute_forvisionsensor,true},
    {"sim.displayattribute_colorcodedpickpass",sim_displayattribute_colorcodedpickpass,true},
    {"sim.displayattribute_colorcoded",sim_displayattribute_colorcoded,true},
    {"sim.displayattribute_trianglewireframe",sim_displayattribute_trianglewireframe,true},
    {"sim.displayattribute_thickEdges",sim_displayattribute_thickEdges,true},
    {"sim.displayattribute_dynamiccontentonly",sim_displayattribute_dynamiccontentonly,true},
    {"sim.displayattribute_mirror",sim_displayattribute_mirror,true},
    {"sim.displayattribute_useauxcomponent",sim_displayattribute_useauxcomponent,true},
    {"sim.displayattribute_ignorerenderableflag",sim_displayattribute_ignorerenderableflag,true},
    {"sim.displayattribute_noopenglcallbacks",sim_displayattribute_noopenglcallbacks,true},
    {"sim.displayattribute_noghosts",sim_displayattribute_noghosts,true},
    {"sim.displayattribute_nopointclouds",sim_displayattribute_nopointclouds,true},
    {"sim.displayattribute_nodrawingobjects",sim_displayattribute_nodrawingobjects,true},
    {"sim.displayattribute_noparticles",sim_displayattribute_noparticles,true},
    {"sim.displayattribute_colorcodedtriangles",sim_displayattribute_colorcodedtriangles,true},
    // Navigation and selection modes with the mouse:
    {"sim.navigation_passive",sim_navigation_passive,true},
    {"sim.navigation_camerashift",sim_navigation_camerashift,true},
    {"sim.navigation_camerarotate",sim_navigation_camerarotate,true},
    {"sim.navigation_camerazoom",sim_navigation_camerazoom,true},
    {"sim.navigation_cameraangle",sim_navigation_cameraangle,true},
    {"sim.navigation_objectshift",sim_navigation_objectshift,true},
    {"sim.navigation_objectrotate",sim_navigation_objectrotate,true},
    {"sim.navigation_createpathpoint",sim_navigation_createpathpoint,true},
    {"sim.navigation_clickselection",sim_navigation_clickselection,true},
    {"sim.navigation_ctrlselection",sim_navigation_ctrlselection,true},
    {"sim.navigation_shiftselection",sim_navigation_shiftselection,true},
    {"sim.navigation_camerazoomwheel",sim_navigation_camerazoomwheel,true},
    {"sim.navigation_camerarotaterightbutton",sim_navigation_camerarotaterightbutton,true},
    {"sim.navigation_camerarotatemiddlebutton",sim_navigation_camerarotatemiddlebutton,true},
    // drawing objects
    {"sim.drawing_points",sim_drawing_points,true},
    {"sim.drawing_lines",sim_drawing_lines,true},
    {"sim.drawing_linestrip",sim_drawing_linestrip,true},
    {"sim.drawing_triangles",sim_drawing_triangles,true},
    {"sim.drawing_trianglepts",sim_drawing_trianglepts,true},
    {"sim.drawing_quadpts",sim_drawing_quadpts,true},
    {"sim.drawing_discpts",sim_drawing_discpts,true},
    {"sim.drawing_cubepts",sim_drawing_cubepts,true},
    {"sim.drawing_spherepts",sim_drawing_spherepts,true},
    {"sim.drawing_itemcolors",sim_drawing_itemcolors,true},
    {"sim.drawing_vertexcolors",sim_drawing_vertexcolors,true},
    {"sim.drawing_itemsizes",sim_drawing_itemsizes,true},
    {"sim.drawing_backfaceculling",sim_drawing_backfaceculling,true},
    {"sim.drawing_wireframe",sim_drawing_wireframe,true},
    {"sim.drawing_painttag",sim_drawing_painttag,true},
    {"sim.drawing_followparentvisibility",sim_drawing_followparentvisibility,true},
    {"sim.drawing_cyclic",sim_drawing_cyclic,true},
    {"sim.drawing_50percenttransparency",sim_drawing_50percenttransparency,true},
    {"sim.drawing_25percenttransparency",sim_drawing_25percenttransparency,true},
    {"sim.drawing_12percenttransparency",sim_drawing_12percenttransparency,true},
    {"sim.drawing_emissioncolor",sim_drawing_emissioncolor,true},
    {"sim.drawing_facingcamera",sim_drawing_facingcamera,true},
    {"sim.drawing_overlay",sim_drawing_overlay,true},
    {"sim.drawing_itemtransparency",sim_drawing_itemtransparency,true},
    {"sim.drawing_persistent",sim_drawing_persistent,true},
    {"sim.drawing_auxchannelcolor1",sim_drawing_auxchannelcolor1,true},
    {"sim.drawing_auxchannelcolor2",sim_drawing_auxchannelcolor2,true},
    {"sim.drawing_local",sim_drawing_local,true},
    // particle objects
    {"sim.particle_points1",sim_particle_points1,true},
    {"sim.particle_points2",sim_particle_points2,true},
    {"sim.particle_points4",sim_particle_points4,true},
    {"sim.particle_roughspheres",sim_particle_roughspheres,true},
    {"sim.particle_spheres",sim_particle_spheres,true},
    {"sim.particle_respondable1to4",sim_particle_respondable1to4,true},
    {"sim.particle_respondable5to8",sim_particle_respondable5to8,true},
    {"sim.particle_particlerespondable",sim_particle_particlerespondable,true},
    {"sim.particle_ignoresgravity",sim_particle_ignoresgravity,true},
    {"sim.particle_invisible",sim_particle_invisible,true},
    {"sim.particle_painttag",sim_particle_painttag,true},
    {"sim.particle_itemsizes",sim_particle_itemsizes,true},
    {"sim.particle_itemdensities",sim_particle_itemdensities,true},
    {"sim.particle_itemcolors",sim_particle_itemcolors,true},
    {"sim.particle_cyclic",sim_particle_cyclic,true},
    {"sim.particle_emissioncolor",sim_particle_emissioncolor,true},
    {"sim.particle_water",sim_particle_water,true},
    // joint modes
    {"sim.jointmode_kinematic",sim_jointmode_kinematic,true},
    {"sim.jointmode_dependent",sim_jointmode_dependent,true},
    {"sim.jointmode_dynamic",sim_jointmode_dynamic,true},
    // joint dyn ctrl modes
    {"sim.jointdynctrl_free",sim_jointdynctrl_free,true},
    {"sim.jointdynctrl_force",sim_jointdynctrl_force,true},
    {"sim.jointdynctrl_velocity",sim_jointdynctrl_velocity,true},
    {"sim.jointdynctrl_position",sim_jointdynctrl_position,true},
    {"sim.jointdynctrl_spring",sim_jointdynctrl_spring,true},
    {"sim.jointdynctrl_callback",sim_jointdynctrl_callback,true},
    // physics engine
    {"sim.physics_bullet",sim_physics_bullet,true},
    {"sim.physics_ode",sim_physics_ode,true},
    {"sim.physics_vortex",sim_physics_vortex,true},
    {"sim.physics_newton",sim_physics_newton,true},
    {"sim.physics_mujoco",sim_physics_mujoco,true},
    {"sim.physics_physx",sim_physics_physx,true},
    // pure primitives type
    {"sim.primitiveshape_none",sim_primitiveshape_none,true},
    {"sim.primitiveshape_plane",sim_primitiveshape_plane,true},
    {"sim.primitiveshape_disc",sim_primitiveshape_disc,true},
    {"sim.primitiveshape_cuboid",sim_primitiveshape_cuboid,true},
    {"sim.primitiveshape_spheroid",sim_primitiveshape_spheroid,true},
    {"sim.primitiveshape_cylinder",sim_primitiveshape_cylinder,true},
    {"sim.primitiveshape_cone",sim_primitiveshape_cone,true},
    {"sim.primitiveshape_heightfield",sim_primitiveshape_heightfield,true},
    {"sim.primitiveshape_capsule",sim_primitiveshape_capsule,true},
    // dummy-dummy link types
    {"sim.dummylink_dynloopclosure",sim_dummylink_dynloopclosure,true},
    {"sim.dummylink_dyntendon",sim_dummylink_dyntendon,true},
    // color components
    {"sim.colorcomponent_ambient",sim_colorcomponent_ambient,true},
    {"sim.colorcomponent_ambient_diffuse",sim_colorcomponent_ambient_diffuse,true},
    {"sim.colorcomponent_diffuse",sim_colorcomponent_diffuse,true},
    {"sim.colorcomponent_specular",sim_colorcomponent_specular,true},
    {"sim.colorcomponent_emission",sim_colorcomponent_emission,true},
    {"sim.colorcomponent_transparency",sim_colorcomponent_transparency,true},
    {"sim.colorcomponent_auxiliary",sim_colorcomponent_auxiliary,true},
    // volume types
    {"sim.volume_ray",sim_volume_ray,true},
    {"sim.volume_randomizedray",sim_volume_randomizedray,true},
    {"sim.volume_pyramid",sim_volume_pyramid,true},
    {"sim.volume_cylinder",sim_volume_cylinder,true},
    {"sim.volume_disc",sim_volume_disc,true},
    {"sim.volume_cone",sim_volume_cone,true},
    // Object int/float/string parameters
    // scene objects
    {"sim.objintparam_visibility_layer",sim_objintparam_visibility_layer,true},
    {"sim.objfloatparam_abs_x_velocity",sim_objfloatparam_abs_x_velocity,true},
    {"sim.objfloatparam_abs_y_velocity",sim_objfloatparam_abs_y_velocity,true},
    {"sim.objfloatparam_abs_z_velocity",sim_objfloatparam_abs_z_velocity,true},
    {"sim.objfloatparam_abs_rot_velocity",sim_objfloatparam_abs_rot_velocity,true},
    {"sim.objfloatparam_objbbox_min_x",sim_objfloatparam_objbbox_min_x,true},
    {"sim.objfloatparam_objbbox_min_y",sim_objfloatparam_objbbox_min_y,true},
    {"sim.objfloatparam_objbbox_min_z",sim_objfloatparam_objbbox_min_z,true},
    {"sim.objfloatparam_objbbox_max_x",sim_objfloatparam_objbbox_max_x,true},
    {"sim.objfloatparam_objbbox_max_y",sim_objfloatparam_objbbox_max_y,true},
    {"sim.objfloatparam_objbbox_max_z",sim_objfloatparam_objbbox_max_z,true},
    {"sim.objfloatparam_modelbbox_min_x",sim_objfloatparam_modelbbox_min_x,true},
    {"sim.objfloatparam_modelbbox_min_y",sim_objfloatparam_modelbbox_min_y,true},
    {"sim.objfloatparam_modelbbox_min_z",sim_objfloatparam_modelbbox_min_z,true},
    {"sim.objfloatparam_modelbbox_max_x",sim_objfloatparam_modelbbox_max_x,true},
    {"sim.objfloatparam_modelbbox_max_y",sim_objfloatparam_modelbbox_max_y,true},
    {"sim.objfloatparam_modelbbox_max_z",sim_objfloatparam_modelbbox_max_z,true},
    {"sim.objintparam_collection_self_collision_indicator",sim_objintparam_collection_self_collision_indicator,true},
    {"sim.objfloatparam_transparency_offset",sim_objfloatparam_transparency_offset,true},
    {"sim.objfloatparam_size_factor",sim_objfloatparam_size_factor,true},
    {"sim.objintparam_child_role",sim_objintparam_child_role,true},
    {"sim.objintparam_parent_role",sim_objintparam_parent_role,true},
    {"sim.objintparam_manipulation_permissions",sim_objintparam_manipulation_permissions,true},
    {"sim.objintparam_illumination_handle",sim_objintparam_illumination_handle,true},
    {"sim.objstringparam_dna",sim_objstringparam_dna,true},
    {"sim.objstringparam_unique_id",sim_objstringparam_unique_id,true},
    {"sim.objintparam_visible",sim_objintparam_visible,true},
    {"sim.objintparam_unique_id",sim_objintparam_unique_id,true},


    // vision_sensors
    {"sim.visionfloatparam_near_clipping",sim_visionfloatparam_near_clipping,true},
    {"sim.visionfloatparam_far_clipping",sim_visionfloatparam_far_clipping,true},
    {"sim.visionintparam_resolution_x",sim_visionintparam_resolution_x,true},
    {"sim.visionintparam_resolution_y",sim_visionintparam_resolution_y,true},
    {"sim.visionfloatparam_perspective_angle",sim_visionfloatparam_perspective_angle,true},
    {"sim.visionfloatparam_ortho_size",sim_visionfloatparam_ortho_size,true},
    {"sim.visionintparam_disabled_light_components",sim_visionintparam_disabled_light_components,true},
    {"sim.visionintparam_rendering_attributes",sim_visionintparam_rendering_attributes,true},
    {"sim.visionintparam_entity_to_render",sim_visionintparam_entity_to_render,true},
    {"sim.visionintparam_windowed_size_x",sim_visionintparam_windowed_size_x,true},
    {"sim.visionintparam_windowed_size_y",sim_visionintparam_windowed_size_y,true},
    {"sim.visionintparam_windowed_pos_x",sim_visionintparam_windowed_pos_x,true},
    {"sim.visionintparam_windowed_pos_y",sim_visionintparam_windowed_pos_y,true},
    {"sim.visionintparam_pov_focal_blur",sim_visionintparam_pov_focal_blur,true},
    {"sim.visionfloatparam_pov_blur_distance",sim_visionfloatparam_pov_blur_distance,true},
    {"sim.visionfloatparam_pov_aperture",sim_visionfloatparam_pov_aperture,true},
    {"sim.visionintparam_pov_blur_sampled",sim_visionintparam_pov_blur_sampled,true},
    {"sim.visionintparam_render_mode",sim_visionintparam_render_mode,true},
    {"sim.visionintparam_perspective_operation",sim_visionintparam_perspective_operation,true},
    {"sim.visionfarrayparam_viewfrustum",sim_visionfarrayparam_viewfrustum,true},

    // joints
    {"sim.jointintparam_motor_enabled",sim_jointintparam_motor_enabled,true},
    {"sim.jointintparam_ctrl_enabled",sim_jointintparam_ctrl_enabled,true},
    {"sim.jointfloatparam_intrinsic_x",sim_jointfloatparam_intrinsic_x,true},
    {"sim.jointfloatparam_intrinsic_y",sim_jointfloatparam_intrinsic_y,true},
    {"sim.jointfloatparam_intrinsic_z",sim_jointfloatparam_intrinsic_z,true},
    {"sim.jointfloatparam_intrinsic_qx",sim_jointfloatparam_intrinsic_qx,true},
    {"sim.jointfloatparam_intrinsic_qy",sim_jointfloatparam_intrinsic_qy,true},
    {"sim.jointfloatparam_intrinsic_qz",sim_jointfloatparam_intrinsic_qz,true},
    {"sim.jointfloatparam_intrinsic_qw",sim_jointfloatparam_intrinsic_qw,true},
    {"sim.jointfloatparam_velocity",sim_jointfloatparam_velocity,true},
    {"sim.jointfloatparam_spherical_qx",sim_jointfloatparam_spherical_qx,true},
    {"sim.jointfloatparam_spherical_qy",sim_jointfloatparam_spherical_qy,true},
    {"sim.jointfloatparam_spherical_qz",sim_jointfloatparam_spherical_qz,true},
    {"sim.jointfloatparam_spherical_qw",sim_jointfloatparam_spherical_qw,true},
    {"sim.jointfloatparam_kc_k",sim_jointfloatparam_kc_k,true},
    {"sim.jointfloatparam_kc_c",sim_jointfloatparam_kc_c,true},
    {"sim.jointfloatparam_error_x",sim_jointfloatparam_error_x,true},
    {"sim.jointfloatparam_error_y",sim_jointfloatparam_error_y,true},
    {"sim.jointfloatparam_error_z",sim_jointfloatparam_error_z,true},
    {"sim.jointfloatparam_error_a",sim_jointfloatparam_error_a,true},
    {"sim.jointfloatparam_error_b",sim_jointfloatparam_error_b,true},
    {"sim.jointfloatparam_error_g",sim_jointfloatparam_error_g,true},
    {"sim.jointfloatparam_error_pos",sim_jointfloatparam_error_pos,true},
    {"sim.jointfloatparam_error_angle",sim_jointfloatparam_error_angle,true},
    {"sim.jointintparam_velocity_lock",sim_jointintparam_velocity_lock,true},
    {"sim.jointintparam_vortex_dep_handle",sim_jointintparam_vortex_dep_handle,true},
    {"sim.jointfloatparam_vortex_dep_multiplication",sim_jointfloatparam_vortex_dep_multiplication,true},
    {"sim.jointfloatparam_vortex_dep_offset",sim_jointfloatparam_vortex_dep_offset,true},
    {"sim.jointfloatparam_screw_pitch",sim_jointfloatparam_screw_pitch,true},
    {"sim.jointfloatparam_maxvel",sim_jointfloatparam_maxvel,true},
    {"sim.jointfloatparam_maxaccel",sim_jointfloatparam_maxaccel,true},
    {"sim.jointfloatparam_maxjerk",sim_jointfloatparam_maxjerk,true},
    {"sim.jointintparam_dynctrlmode",sim_jointintparam_dynctrlmode,true},
    {"sim.jointintparam_dynvelctrltype",sim_jointintparam_dynvelctrltype,true},
    {"sim.jointintparam_dynposctrltype",sim_jointintparam_dynposctrltype,true},

    // shapes
    {"sim.shapefloatparam_init_velocity_x",sim_shapefloatparam_init_velocity_x,true},
    {"sim.shapefloatparam_init_velocity_y",sim_shapefloatparam_init_velocity_y,true},
    {"sim.shapefloatparam_init_velocity_z",sim_shapefloatparam_init_velocity_z,true},
    {"sim.shapeintparam_static",sim_shapeintparam_static,true},
    {"sim.shapeintparam_kinematic",sim_shapeintparam_kinematic,true},
    {"sim.shapeintparam_respondable",sim_shapeintparam_respondable,true},
    {"sim.shapefloatparam_mass",sim_shapefloatparam_mass,true},
    {"sim.shapefloatparam_texture_x",sim_shapefloatparam_texture_x,true},
    {"sim.shapefloatparam_texture_y",sim_shapefloatparam_texture_y,true},
    {"sim.shapefloatparam_texture_z",sim_shapefloatparam_texture_z,true},
    {"sim.shapefloatparam_texture_a",sim_shapefloatparam_texture_a,true},
    {"sim.shapefloatparam_texture_b",sim_shapefloatparam_texture_b,true},
    {"sim.shapefloatparam_texture_g",sim_shapefloatparam_texture_g,true},
    {"sim.shapefloatparam_texture_scaling_x",sim_shapefloatparam_texture_scaling_x,true},
    {"sim.shapefloatparam_texture_scaling_y",sim_shapefloatparam_texture_scaling_y,true},
    {"sim.shapeintparam_culling",sim_shapeintparam_culling,true},
    {"sim.shapeintparam_wireframe",sim_shapeintparam_wireframe,true},
    {"sim.shapeintparam_compound",sim_shapeintparam_compound,true},
    {"sim.shapeintparam_convex",sim_shapeintparam_convex,true},
    {"sim.shapeintparam_convex_check",sim_shapeintparam_convex_check,true},
    {"sim.shapeintparam_respondable_mask",sim_shapeintparam_respondable_mask,true},
    {"sim.shapefloatparam_init_ang_velocity_x",sim_shapefloatparam_init_ang_velocity_x,true},
    {"sim.shapefloatparam_init_ang_velocity_y",sim_shapefloatparam_init_ang_velocity_y,true},
    {"sim.shapefloatparam_init_ang_velocity_z",sim_shapefloatparam_init_ang_velocity_z,true},
    {"sim.shapefloatparam_init_velocity_a",sim_shapefloatparam_init_ang_velocity_x,false},
    {"sim.shapefloatparam_init_velocity_b",sim_shapefloatparam_init_ang_velocity_y,false},
    {"sim.shapefloatparam_init_velocity_g",sim_shapefloatparam_init_ang_velocity_z,false},
    {"sim.shapestringparam_color_name",sim_shapestringparam_color_name,true},
    {"sim.shapeintparam_edge_visibility",sim_shapeintparam_edge_visibility,true},
    {"sim.shapefloatparam_shading_angle",sim_shapefloatparam_shading_angle,true},
    {"sim.shapefloatparam_edge_angle",sim_shapefloatparam_edge_angle,true},
    {"sim.shapeintparam_edge_borders_hidden",sim_shapeintparam_edge_borders_hidden,true},
    {"sim.shapeintparam_component_cnt",sim_shapeintparam_component_cnt,true},
    {"sim.shapeintparam_sleepmodestart",sim_shapeintparam_sleepmodestart,true},
    {"sim.shapeintparam_respondablesuspendcnt",sim_shapeintparam_respondablesuspendcnt,true},
    // proximity sensors
    {"sim.proxintparam_ray_invisibility",sim_proxintparam_ray_invisibility,true},
    {"sim.proxintparam_volume_type",sim_proxintparam_volume_type,true},
    {"sim.proxintparam_entity_to_detect",sim_proxintparam_entity_to_detect,true},
    // proximity sensors
    {"sim.forcefloatparam_error_x",sim_forcefloatparam_error_x,true},
    {"sim.forcefloatparam_error_y",sim_forcefloatparam_error_y,true},
    {"sim.forcefloatparam_error_z",sim_forcefloatparam_error_z,true},
    {"sim.forcefloatparam_error_a",sim_forcefloatparam_error_a,true},
    {"sim.forcefloatparam_error_b",sim_forcefloatparam_error_b,true},
    {"sim.forcefloatparam_error_g",sim_forcefloatparam_error_g,true},
    {"sim.forcefloatparam_error_pos",sim_forcefloatparam_error_pos,true},
    {"sim.forcefloatparam_error_angle",sim_forcefloatparam_error_angle,true},
    // lights
    {"sim.lightintparam_pov_casts_shadows",sim_lightintparam_pov_casts_shadows,true},
    {"sim.lightfloatparam_spot_exponent",sim_lightfloatparam_spot_exponent,true},
    {"sim.lightfloatparam_spot_cutoff",sim_lightfloatparam_spot_cutoff,true},
    {"sim.lightfloatparam_const_attenuation",sim_lightfloatparam_const_attenuation,true},
    {"sim.lightfloatparam_lin_attenuation",sim_lightfloatparam_lin_attenuation,true},
    {"sim.lightfloatparam_quad_attenuation",sim_lightfloatparam_quad_attenuation,true},

    // cameras
    {"sim.camerafloatparam_near_clipping",sim_camerafloatparam_near_clipping,true},
    {"sim.camerafloatparam_far_clipping",sim_camerafloatparam_far_clipping,true},
    {"sim.cameraintparam_disabled_light_components",sim_cameraintparam_disabled_light_components,true},
    {"sim.camerafloatparam_perspective_angle",sim_camerafloatparam_perspective_angle,true},
    {"sim.camerafloatparam_ortho_size",sim_camerafloatparam_ortho_size,true},
    {"sim.cameraintparam_rendering_attributes",sim_cameraintparam_rendering_attributes,true},
    {"sim.cameraintparam_pov_focal_blur",sim_cameraintparam_pov_focal_blur,true},
    {"sim.camerafloatparam_pov_blur_distance",sim_camerafloatparam_pov_blur_distance,true},
    {"sim.camerafloatparam_pov_aperture",sim_camerafloatparam_pov_aperture,true},
    {"sim.cameraintparam_pov_blur_samples",sim_cameraintparam_pov_blur_samples,true},
    {"sim.cameraintparam_perspective_operation",sim_cameraintparam_perspective_operation,true},
    {"sim.cameraintparam_trackedobject",sim_cameraintparam_trackedobject,true},
    {"sim.cameraintparam_remotecameramode",sim_cameraintparam_remotecameramode,true},
    {"sim.camerafarrayparam_viewfrustum",sim_camerafarrayparam_viewfrustum,true},

    // dummies
    {"sim.dummyintparam_link_type",sim_dummyintparam_link_type,true},
    {"sim.dummyfloatparam_size",sim_dummyfloatparam_size,true},
    // graph
    {"sim.graphintparam_needs_refresh",sim_graphintparam_needs_refresh,true},
    // mills
    {"sim.millintparam_volume_type",sim_millintparam_volume_type,true},
    // mirrors
    {"sim.mirrorfloatparam_width",sim_mirrorfloatparam_width,true},
    {"sim.mirrorfloatparam_height",sim_mirrorfloatparam_height,true},
    {"sim.mirrorfloatparam_reflectance",sim_mirrorfloatparam_reflectance,true},
    {"sim.mirrorintparam_enable",sim_mirrorintparam_enable,true},
    // Bullet engine params
    {"sim.bullet_global_internalscalingfactor",sim_bullet_global_internalscalingfactor,true},
    {"sim.bullet_global_collisionmarginfactor",sim_bullet_global_collisionmarginfactor,true},
    {"sim.bullet_global_constraintsolvingiterations",sim_bullet_global_constraintsolvingiterations,true},
    {"sim.bullet_global_bitcoded",sim_bullet_global_bitcoded,true},
    {"sim.bullet_global_constraintsolvertype",sim_bullet_global_constraintsolvertype,true},
    {"sim.bullet_global_fullinternalscaling",sim_bullet_global_fullinternalscaling,true},
    {"sim.bullet_global_computeinertias",sim_bullet_global_computeinertias,true},
    {"sim.bullet_joint_stoperp",sim_bullet_joint_stoperp,true},
    {"sim.bullet_joint_stopcfm",sim_bullet_joint_stopcfm,true},
    {"sim.bullet_joint_normalcfm",sim_bullet_joint_normalcfm,true},
    {"sim.bullet_joint_pospid1",sim_bullet_joint_pospid1,true},
    {"sim.bullet_joint_pospid2",sim_bullet_joint_pospid2,true},
    {"sim.bullet_joint_pospid3",sim_bullet_joint_pospid3,true},
    {"sim.bullet_body_restitution",sim_bullet_body_restitution,true},
    {"sim.bullet_body_oldfriction",sim_bullet_body_oldfriction,true},
    {"sim.bullet_body_friction",sim_bullet_body_friction,true},
    {"sim.bullet_body_lineardamping",sim_bullet_body_lineardamping,true},
    {"sim.bullet_body_angulardamping",sim_bullet_body_angulardamping,true},
    {"sim.bullet_body_nondefaultcollisionmargingfactor",sim_bullet_body_nondefaultcollisionmargingfactor,true},
    {"sim.bullet_body_nondefaultcollisionmargingfactorconvex",sim_bullet_body_nondefaultcollisionmargingfactorconvex,true},
    {"sim.bullet_body_bitcoded",sim_bullet_body_bitcoded,true},
    {"sim.bullet_body_sticky",sim_bullet_body_sticky,true},
    {"sim.bullet_body_usenondefaultcollisionmargin",sim_bullet_body_usenondefaultcollisionmargin,true},
    {"sim.bullet_body_usenondefaultcollisionmarginconvex",sim_bullet_body_usenondefaultcollisionmarginconvex,true},
    {"sim.bullet_body_autoshrinkconvex",sim_bullet_body_autoshrinkconvex,true},
    // Ode engine params
    {"sim.ode_global_internalscalingfactor",sim_ode_global_internalscalingfactor,true},
    {"sim.ode_global_cfm",sim_ode_global_cfm,true},
    {"sim.ode_global_erp",sim_ode_global_erp,true},
    {"sim.ode_global_constraintsolvingiterations",sim_ode_global_constraintsolvingiterations,true},
    {"sim.ode_global_bitcoded",sim_ode_global_bitcoded,true},
    {"sim.ode_global_randomseed",sim_ode_global_randomseed,true},
    {"sim.ode_global_fullinternalscaling",sim_ode_global_fullinternalscaling,true},
    {"sim.ode_global_quickstep",sim_ode_global_quickstep,true},
    {"sim.ode_global_computeinertias",sim_ode_global_computeinertias,true},
    {"sim.ode_joint_stoperp",sim_ode_joint_stoperp,true},
    {"sim.ode_joint_stopcfm",sim_ode_joint_stopcfm,true},
    {"sim.ode_joint_bounce",sim_ode_joint_bounce,true},
    {"sim.ode_joint_fudgefactor",sim_ode_joint_fudgefactor,true},
    {"sim.ode_joint_normalcfm",sim_ode_joint_normalcfm,true},
    {"sim.ode_joint_pospid1",sim_ode_joint_pospid1,true},
    {"sim.ode_joint_pospid2",sim_ode_joint_pospid2,true},
    {"sim.ode_joint_pospid3",sim_ode_joint_pospid3,true},
    {"sim.ode_body_friction",sim_ode_body_friction,true},
    {"sim.ode_body_softerp",sim_ode_body_softerp,true},
    {"sim.ode_body_softcfm",sim_ode_body_softcfm,true},
    {"sim.ode_body_lineardamping",sim_ode_body_lineardamping,true},
    {"sim.ode_body_angulardamping",sim_ode_body_angulardamping,true},
    {"sim.ode_body_maxcontacts",sim_ode_body_maxcontacts,true},
    // Vortex engine params
    {"sim.vortex_global_internalscalingfactor",sim_vortex_global_internalscalingfactor,true},
    {"sim.vortex_global_contacttolerance",sim_vortex_global_contacttolerance,true},
    {"sim.vortex_global_constraintlinearcompliance",sim_vortex_global_constraintlinearcompliance,true},
    {"sim.vortex_global_constraintlineardamping",sim_vortex_global_constraintlineardamping,true},
    {"sim.vortex_global_constraintlinearkineticloss",sim_vortex_global_constraintlinearkineticloss,true},
    {"sim.vortex_global_constraintangularcompliance",sim_vortex_global_constraintangularcompliance,true},
    {"sim.vortex_global_constraintangulardamping",sim_vortex_global_constraintangulardamping,true},
    {"sim.vortex_global_constraintangularkineticloss",sim_vortex_global_constraintangularkineticloss,true},
    {"sim.vortex_global_bitcoded",sim_vortex_global_bitcoded,true},
    {"sim.vortex_global_autosleep",sim_vortex_global_autosleep,true},
    {"sim.vortex_global_multithreading",sim_vortex_global_multithreading,true},
    {"sim.vortex_global_computeinertias",sim_vortex_global_computeinertias,true},
    {"sim.vortex_joint_lowerlimitdamping",sim_vortex_joint_lowerlimitdamping,true},
    {"sim.vortex_joint_upperlimitdamping",sim_vortex_joint_upperlimitdamping,true},
    {"sim.vortex_joint_lowerlimitstiffness",sim_vortex_joint_lowerlimitstiffness,true},
    {"sim.vortex_joint_upperlimitstiffness",sim_vortex_joint_upperlimitstiffness,true},
    {"sim.vortex_joint_lowerlimitrestitution",sim_vortex_joint_lowerlimitrestitution,true},
    {"sim.vortex_joint_upperlimitrestitution",sim_vortex_joint_upperlimitrestitution,true},
    {"sim.vortex_joint_lowerlimitmaxforce",sim_vortex_joint_lowerlimitmaxforce,true},
    {"sim.vortex_joint_upperlimitmaxforce",sim_vortex_joint_upperlimitmaxforce,true},
    {"sim.vortex_joint_motorconstraintfrictioncoeff",sim_vortex_joint_motorconstraintfrictioncoeff,true},
    {"sim.vortex_joint_motorconstraintfrictionmaxforce",sim_vortex_joint_motorconstraintfrictionmaxforce,true},
    {"sim.vortex_joint_motorconstraintfrictionloss",sim_vortex_joint_motorconstraintfrictionloss,true},
    {"sim.vortex_joint_p0loss",sim_vortex_joint_p0loss,true},
    {"sim.vortex_joint_p0stiffness",sim_vortex_joint_p0stiffness,true},
    {"sim.vortex_joint_p0damping",sim_vortex_joint_p0damping,true},
    {"sim.vortex_joint_p0frictioncoeff",sim_vortex_joint_p0frictioncoeff,true},
    {"sim.vortex_joint_p0frictionmaxforce",sim_vortex_joint_p0frictionmaxforce,true},
    {"sim.vortex_joint_p0frictionloss",sim_vortex_joint_p0frictionloss,true},
    {"sim.vortex_joint_p1loss",sim_vortex_joint_p1loss,true},
    {"sim.vortex_joint_p1stiffness",sim_vortex_joint_p1stiffness,true},
    {"sim.vortex_joint_p1damping",sim_vortex_joint_p1damping,true},
    {"sim.vortex_joint_p1frictioncoeff",sim_vortex_joint_p1frictioncoeff,true},
    {"sim.vortex_joint_p1frictionmaxforce",sim_vortex_joint_p1frictionmaxforce,true},
    {"sim.vortex_joint_p1frictionloss",sim_vortex_joint_p1frictionloss,true},
    {"sim.vortex_joint_p2loss",sim_vortex_joint_p2loss,true},
    {"sim.vortex_joint_p2stiffness",sim_vortex_joint_p2stiffness,true},
    {"sim.vortex_joint_p2damping",sim_vortex_joint_p2damping,true},
    {"sim.vortex_joint_p2frictioncoeff",sim_vortex_joint_p2frictioncoeff,true},
    {"sim.vortex_joint_p2frictionmaxforce",sim_vortex_joint_p2frictionmaxforce,true},
    {"sim.vortex_joint_p2frictionloss",sim_vortex_joint_p2frictionloss,true},
    {"sim.vortex_joint_a0loss",sim_vortex_joint_a0loss,true},
    {"sim.vortex_joint_a0stiffness",sim_vortex_joint_a0stiffness,true},
    {"sim.vortex_joint_a0damping",sim_vortex_joint_a0damping,true},
    {"sim.vortex_joint_a0frictioncoeff",sim_vortex_joint_a0frictioncoeff,true},
    {"sim.vortex_joint_a0frictionmaxforce",sim_vortex_joint_a0frictionmaxforce,true},
    {"sim.vortex_joint_a0frictionloss",sim_vortex_joint_a0frictionloss,true},
    {"sim.vortex_joint_a1loss",sim_vortex_joint_a1loss,true},
    {"sim.vortex_joint_a1stiffness",sim_vortex_joint_a1stiffness,true},
    {"sim.vortex_joint_a1damping",sim_vortex_joint_a1damping,true},
    {"sim.vortex_joint_a1frictioncoeff",sim_vortex_joint_a1frictioncoeff,true},
    {"sim.vortex_joint_a1frictionmaxforce",sim_vortex_joint_a1frictionmaxforce,true},
    {"sim.vortex_joint_a1frictionloss",sim_vortex_joint_a1frictionloss,true},
    {"sim.vortex_joint_a2loss",sim_vortex_joint_a2loss,true},
    {"sim.vortex_joint_a2stiffness",sim_vortex_joint_a2stiffness,true},
    {"sim.vortex_joint_a2damping",sim_vortex_joint_a2damping,true},
    {"sim.vortex_joint_a2frictioncoeff",sim_vortex_joint_a2frictioncoeff,true},
    {"sim.vortex_joint_a2frictionmaxforce",sim_vortex_joint_a2frictionmaxforce,true},
    {"sim.vortex_joint_a2frictionloss",sim_vortex_joint_a2frictionloss,true},
    {"sim.vortex_joint_dependencyfactor",sim_vortex_joint_dependencyfactor,true},
    {"sim.vortex_joint_dependencyoffset",sim_vortex_joint_dependencyoffset,true},
    {"sim.vortex_joint_bitcoded",sim_vortex_joint_bitcoded,true},
    {"sim.vortex_joint_relaxationenabledbc",sim_vortex_joint_relaxationenabledbc,true},
    {"sim.vortex_joint_frictionenabledbc",sim_vortex_joint_frictionenabledbc,true},
    {"sim.vortex_joint_frictionproportionalbc",sim_vortex_joint_frictionproportionalbc,true},
    {"sim.vortex_joint_objectid",sim_vortex_joint_objectid,true},
    {"sim.vortex_joint_dependentobjectid",sim_vortex_joint_dependentobjectid,true},
    {"sim.vortex_joint_motorfrictionenabled",sim_vortex_joint_motorfrictionenabled,true},
    {"sim.vortex_joint_proportionalmotorfriction",sim_vortex_joint_proportionalmotorfriction,true},
    {"sim.vortex_joint_pospid1",sim_vortex_joint_pospid1,true},
    {"sim.vortex_joint_pospid2",sim_vortex_joint_pospid2,true},
    {"sim.vortex_joint_pospid3",sim_vortex_joint_pospid3,true},
    {"sim.vortex_body_primlinearaxisfriction",sim_vortex_body_primlinearaxisfriction,true},
    {"sim.vortex_body_seclinearaxisfriction",sim_vortex_body_seclinearaxisfriction,true},
    {"sim.vortex_body_primangularaxisfriction",sim_vortex_body_primangularaxisfriction,true},
    {"sim.vortex_body_secangularaxisfriction",sim_vortex_body_secangularaxisfriction,true},
    {"sim.vortex_body_normalangularaxisfriction",sim_vortex_body_normalangularaxisfriction,true},
    {"sim.vortex_body_primlinearaxisstaticfrictionscale",sim_vortex_body_primlinearaxisstaticfrictionscale,true},
    {"sim.vortex_body_seclinearaxisstaticfrictionscale",sim_vortex_body_seclinearaxisstaticfrictionscale,true},
    {"sim.vortex_body_primangularaxisstaticfrictionscale",sim_vortex_body_primangularaxisstaticfrictionscale,true},
    {"sim.vortex_body_secangularaxisstaticfrictionscale",sim_vortex_body_secangularaxisstaticfrictionscale,true},
    {"sim.vortex_body_normalangularaxisstaticfrictionscale",sim_vortex_body_normalangularaxisstaticfrictionscale,true},
    {"sim.vortex_body_compliance",sim_vortex_body_compliance,true},
    {"sim.vortex_body_damping",sim_vortex_body_damping,true},
    {"sim.vortex_body_restitution",sim_vortex_body_restitution,true},
    {"sim.vortex_body_restitutionthreshold",sim_vortex_body_restitutionthreshold,true},
    {"sim.vortex_body_adhesiveforce",sim_vortex_body_adhesiveforce,true},
    {"sim.vortex_body_linearvelocitydamping",sim_vortex_body_linearvelocitydamping,true},
    {"sim.vortex_body_angularvelocitydamping",sim_vortex_body_angularvelocitydamping,true},
    {"sim.vortex_body_primlinearaxisslide",sim_vortex_body_primlinearaxisslide,true},
    {"sim.vortex_body_seclinearaxisslide",sim_vortex_body_seclinearaxisslide,true},
    {"sim.vortex_body_primangularaxisslide",sim_vortex_body_primangularaxisslide,true},
    {"sim.vortex_body_secangularaxisslide",sim_vortex_body_secangularaxisslide,true},
    {"sim.vortex_body_normalangularaxisslide",sim_vortex_body_normalangularaxisslide,true},
    {"sim.vortex_body_primlinearaxisslip",sim_vortex_body_primlinearaxisslip,true},
    {"sim.vortex_body_seclinearaxisslip",sim_vortex_body_seclinearaxisslip,true},
    {"sim.vortex_body_primangularaxisslip",sim_vortex_body_primangularaxisslip,true},
    {"sim.vortex_body_secangularaxisslip",sim_vortex_body_secangularaxisslip,true},
    {"sim.vortex_body_normalangularaxisslip",sim_vortex_body_normalangularaxisslip,true},
    {"sim.vortex_body_autosleeplinearspeedthreshold",sim_vortex_body_autosleeplinearspeedthreshold,true},
    {"sim.vortex_body_autosleeplinearaccelthreshold",sim_vortex_body_autosleeplinearaccelthreshold,true},
    {"sim.vortex_body_autosleepangularspeedthreshold",sim_vortex_body_autosleepangularspeedthreshold,true},
    {"sim.vortex_body_autosleepangularaccelthreshold",sim_vortex_body_autosleepangularaccelthreshold,true},
    {"sim.vortex_body_skinthickness",sim_vortex_body_skinthickness,true},
    {"sim.vortex_body_autoangulardampingtensionratio",sim_vortex_body_autoangulardampingtensionratio,true},
    {"sim.vortex_body_primaxisvectorx",sim_vortex_body_primaxisvectorx,true},
    {"sim.vortex_body_primaxisvectory",sim_vortex_body_primaxisvectory,true},
    {"sim.vortex_body_primaxisvectorz",sim_vortex_body_primaxisvectorz,true},
    {"sim.vortex_body_primlinearaxisfrictionmodel",sim_vortex_body_primlinearaxisfrictionmodel,true},
    {"sim.vortex_body_seclinearaxisfrictionmodel",sim_vortex_body_seclinearaxisfrictionmodel,true},
    {"sim.vortex_body_primangulararaxisfrictionmodel",sim_vortex_body_primangulararaxisfrictionmodel,true},
    {"sim.vortex_body_secmangulararaxisfrictionmodel",sim_vortex_body_secmangulararaxisfrictionmodel,false}, // wrong spelling
    {"sim.vortex_body_secangularaxisfrictionmodel",sim_vortex_body_secangularaxisfrictionmodel,true},
    {"sim.vortex_body_normalmangulararaxisfrictionmodel",sim_vortex_body_normalmangulararaxisfrictionmodel,false}, // wrong spelling
    {"sim.vortex_body_normalangularaxisfrictionmodel",sim_vortex_body_normalangularaxisfrictionmodel,true},
    {"sim.vortex_body_bitcoded",sim_vortex_body_bitcoded,true},
    {"sim.vortex_body_autosleepsteplivethreshold",sim_vortex_body_autosleepsteplivethreshold,true},
    {"sim.vortex_body_materialuniqueid",sim_vortex_body_materialuniqueid,true},
    {"sim.vortex_body_pureshapesasconvex",sim_vortex_body_pureshapesasconvex,true},
    {"sim.vortex_body_convexshapesasrandom",sim_vortex_body_convexshapesasrandom,true},
    {"sim.vortex_body_randomshapesasterrain",sim_vortex_body_randomshapesasterrain,true},
    {"sim.vortex_body_fastmoving",sim_vortex_body_fastmoving,true},
    {"sim.vortex_body_autoslip",sim_vortex_body_autoslip,true},
    {"sim.vortex_body_seclinaxissameasprimlinaxis",sim_vortex_body_seclinaxissameasprimlinaxis,true},
    {"sim.vortex_body_secangaxissameasprimangaxis",sim_vortex_body_secangaxissameasprimangaxis,true},
    {"sim.vortex_body_normangaxissameasprimangaxis",sim_vortex_body_normangaxissameasprimangaxis,true},
    {"sim.vortex_body_autoangulardamping",sim_vortex_body_autoangulardamping,true},
    // Newton engine params
    {"sim.newton_global_contactmergetolerance",sim_newton_global_contactmergetolerance,true},
    {"sim.newton_global_constraintsolvingiterations",sim_newton_global_constraintsolvingiterations,true},
    {"sim.newton_global_bitcoded",sim_newton_global_bitcoded,true},
    {"sim.newton_global_multithreading",sim_newton_global_multithreading,true},
    {"sim.newton_global_exactsolver",sim_newton_global_exactsolver,true},
    {"sim.newton_global_highjointaccuracy",sim_newton_global_highjointaccuracy,true},
    {"sim.newton_global_computeinertias",sim_newton_global_computeinertias,true},
    {"sim.newton_joint_dependencyfactor",sim_newton_joint_dependencyfactor,true},
    {"sim.newton_joint_dependencyoffset",sim_newton_joint_dependencyoffset,true},
    {"sim.newton_joint_objectid",sim_newton_joint_objectid,true},
    {"sim.newton_joint_dependentobjectid",sim_newton_joint_dependentobjectid,true},
    {"sim.newton_joint_pospid1",sim_newton_joint_pospid1,true},
    {"sim.newton_joint_pospid2",sim_newton_joint_pospid2,true},
    {"sim.newton_joint_pospid3",sim_newton_joint_pospid3,true},
    {"sim.newton_body_staticfriction",sim_newton_body_staticfriction,true},
    {"sim.newton_body_kineticfriction",sim_newton_body_kineticfriction,true},
    {"sim.newton_body_restitution",sim_newton_body_restitution,true},
    {"sim.newton_body_lineardrag",sim_newton_body_lineardrag,true},
    {"sim.newton_body_angulardrag",sim_newton_body_angulardrag,true},
    {"sim.newton_body_bitcoded",sim_newton_body_bitcoded,true},
    {"sim.newton_body_fastmoving",sim_newton_body_fastmoving,true},
    //Mujoco engine params
    {"sim.mujoco_global_impratio",sim_mujoco_global_impratio,true},
    {"sim.mujoco_global_wind1",sim_mujoco_global_wind1,true},
    {"sim.mujoco_global_wind2",sim_mujoco_global_wind2,true},
    {"sim.mujoco_global_wind3",sim_mujoco_global_wind3,true},
    {"sim.mujoco_global_density",sim_mujoco_global_density,true},
    {"sim.mujoco_global_viscosity",sim_mujoco_global_viscosity,true},
    {"sim.mujoco_global_boundmass",sim_mujoco_global_boundmass,true},
    {"sim.mujoco_global_boundinertia",sim_mujoco_global_boundinertia,true},
    {"sim.mujoco_global_overridemargin",sim_mujoco_global_overridemargin,true},
    {"sim.mujoco_global_overridesolref1",sim_mujoco_global_overridesolref1,true},
    {"sim.mujoco_global_overridesolref2",sim_mujoco_global_overridesolref2,true},
    {"sim.mujoco_global_overridesolimp1",sim_mujoco_global_overridesolimp1,true},
    {"sim.mujoco_global_overridesolimp2",sim_mujoco_global_overridesolimp2,true},
    {"sim.mujoco_global_overridesolimp3",sim_mujoco_global_overridesolimp3,true},
    {"sim.mujoco_global_overridesolimp4",sim_mujoco_global_overridesolimp4,true},
    {"sim.mujoco_global_overridesolimp5",sim_mujoco_global_overridesolimp5,true},
    {"sim.mujoco_global_kinmass",sim_mujoco_global_kinmass,true},
    {"sim.mujoco_global_kininertia",sim_mujoco_global_kininertia,true},
    {"sim.mujoco_global_bitcoded",sim_mujoco_global_bitcoded,true},
    {"sim.mujoco_global_iterations",sim_mujoco_global_iterations,true},
    {"sim.mujoco_global_integrator",sim_mujoco_global_integrator,true},
    {"sim.mujoco_global_solver",sim_mujoco_global_solver,true},
    {"sim.mujoco_global_njmax",sim_mujoco_global_njmax,true},
    {"sim.mujoco_global_nconmax",sim_mujoco_global_nconmax,true},
    {"sim.mujoco_global_nstack",sim_mujoco_global_nstack,true},
    {"sim.mujoco_global_rebuildtrigger",sim_mujoco_global_rebuildtrigger,true},
    {"sim.mujoco_global_cone",sim_mujoco_global_cone,true},
    {"sim.mujoco_global_overridekin",sim_mujoco_global_overridekin,true},
    {"sim.mujoco_global_computeinertias",sim_mujoco_global_computeinertias,true},
    {"sim.mujoco_global_multithreaded",sim_mujoco_global_multithreaded,true},
    {"sim.mujoco_global_multiccd",sim_mujoco_global_multiccd,true},
    {"sim.mujoco_global_balanceinertias",sim_mujoco_global_balanceinertias,true},
    {"sim.mujoco_global_overridecontacts",sim_mujoco_global_overridecontacts,true},
    {"sim.mujoco_joint_solreflimit1",sim_mujoco_joint_solreflimit1,true},
    {"sim.mujoco_joint_solreflimit2",sim_mujoco_joint_solreflimit2,true},
    {"sim.mujoco_joint_solimplimit1",sim_mujoco_joint_solimplimit1,true},
    {"sim.mujoco_joint_solimplimit2",sim_mujoco_joint_solimplimit2,true},
    {"sim.mujoco_joint_solimplimit3",sim_mujoco_joint_solimplimit3,true},
    {"sim.mujoco_joint_solimplimit4",sim_mujoco_joint_solimplimit4,true},
    {"sim.mujoco_joint_solimplimit5",sim_mujoco_joint_solimplimit5,true},
    {"sim.mujoco_joint_frictionloss",sim_mujoco_joint_frictionloss,true},
    {"sim.mujoco_joint_solreffriction1",sim_mujoco_joint_solreffriction1,true},
    {"sim.mujoco_joint_solreffriction2",sim_mujoco_joint_solreffriction2,true},
    {"sim.mujoco_joint_solimpfriction1",sim_mujoco_joint_solimpfriction1,true},
    {"sim.mujoco_joint_solimpfriction2",sim_mujoco_joint_solimpfriction2,true},
    {"sim.mujoco_joint_solimpfriction3",sim_mujoco_joint_solimpfriction3,true},
    {"sim.mujoco_joint_solimpfriction4",sim_mujoco_joint_solimpfriction4,true},
    {"sim.mujoco_joint_solimpfriction5",sim_mujoco_joint_solimpfriction5,true},
    {"sim.mujoco_joint_stiffness",sim_mujoco_joint_stiffness,true},
    {"sim.mujoco_joint_damping",sim_mujoco_joint_damping,true},
    {"sim.mujoco_joint_springref",sim_mujoco_joint_springref,true},
    {"sim.mujoco_joint_springdamper1",sim_mujoco_joint_springdamper1,true},
    {"sim.mujoco_joint_springdamper2",sim_mujoco_joint_springdamper2,true},
    {"sim.mujoco_joint_armature",sim_mujoco_joint_armature,true},
    {"sim.mujoco_joint_margin",sim_mujoco_joint_margin,true},
    {"sim.mujoco_joint_polycoef1",sim_mujoco_joint_polycoef1,true},
    {"sim.mujoco_joint_polycoef2",sim_mujoco_joint_polycoef2,true},
    {"sim.mujoco_joint_polycoef3",sim_mujoco_joint_polycoef3,true},
    {"sim.mujoco_joint_polycoef4",sim_mujoco_joint_polycoef4,true},
    {"sim.mujoco_joint_polycoef5",sim_mujoco_joint_polycoef5,true},
    {"sim.mujoco_joint_dependentobjectid",sim_mujoco_joint_dependentobjectid,true},
    {"sim.mujoco_joint_pospid1",sim_mujoco_joint_pospid1,true},
    {"sim.mujoco_joint_pospid2",sim_mujoco_joint_pospid2,true},
    {"sim.mujoco_joint_pospid3",sim_mujoco_joint_pospid3,true},
    {"sim.mujoco_body_friction1",sim_mujoco_body_friction1,true},
    {"sim.mujoco_body_friction2",sim_mujoco_body_friction2,true},
    {"sim.mujoco_body_friction3",sim_mujoco_body_friction3,true},
    {"sim.mujoco_body_solref1",sim_mujoco_body_solref1,true},
    {"sim.mujoco_body_solref2",sim_mujoco_body_solref2,true},
    {"sim.mujoco_body_solimp1",sim_mujoco_body_solimp1,true},
    {"sim.mujoco_body_solimp2",sim_mujoco_body_solimp2,true},
    {"sim.mujoco_body_solimp3",sim_mujoco_body_solimp3,true},
    {"sim.mujoco_body_solimp4",sim_mujoco_body_solimp4,true},
    {"sim.mujoco_body_solimp5",sim_mujoco_body_solimp5,true},
    {"sim.mujoco_body_solmix",sim_mujoco_body_solmix,true},
    {"sim.mujoco_body_margin",sim_mujoco_body_margin,true},
    {"sim.mujoco_body_condim",sim_mujoco_body_condim,true},
    {"sim.mujoco_body_priority",sim_mujoco_body_priority,true},
    {"sim.mujoco_dummy_range1",sim_mujoco_dummy_range1,true},
    {"sim.mujoco_dummy_range2",sim_mujoco_dummy_range2,true},
    {"sim.mujoco_dummy_solreflimit1",sim_mujoco_dummy_solreflimit1,true},
    {"sim.mujoco_dummy_solreflimit2",sim_mujoco_dummy_solreflimit2,true},
    {"sim.mujoco_dummy_solimplimit1",sim_mujoco_dummy_solimplimit1,true},
    {"sim.mujoco_dummy_solimplimit2",sim_mujoco_dummy_solimplimit2,true},
    {"sim.mujoco_dummy_solimplimit3",sim_mujoco_dummy_solimplimit3,true},
    {"sim.mujoco_dummy_solimplimit4",sim_mujoco_dummy_solimplimit4,true},
    {"sim.mujoco_dummy_solimplimit5",sim_mujoco_dummy_solimplimit5,true},
    {"sim.mujoco_dummy_margin",sim_mujoco_dummy_margin,true},
    {"sim.mujoco_dummy_springlength",sim_mujoco_dummy_springlength,true},
    {"sim.mujoco_dummy_stiffness",sim_mujoco_dummy_stiffness,true},
    {"sim.mujoco_dummy_damping",sim_mujoco_dummy_damping,true},
    {"sim.mujoco_dummy_bitcoded",sim_mujoco_dummy_bitcoded,true},
    {"sim.mujoco_dummy_proxyjointid",sim_mujoco_dummy_proxyjointid,true},
    {"sim.mujoco_dummy_limited",sim_mujoco_dummy_limited,true},
    // Vortex friction models
    {"sim.vortex_bodyfrictionmodel_box",sim_vortex_bodyfrictionmodel_box,true},
    {"sim.vortex_bodyfrictionmodel_scaledbox",sim_vortex_bodyfrictionmodel_scaledbox,true},
    {"sim.vortex_bodyfrictionmodel_proplow",sim_vortex_bodyfrictionmodel_proplow,true},
    {"sim.vortex_bodyfrictionmodel_prophigh",sim_vortex_bodyfrictionmodel_prophigh,true},
    {"sim.vortex_bodyfrictionmodel_scaledboxfast",sim_vortex_bodyfrictionmodel_scaledboxfast,true},
    {"sim.vortex_bodyfrictionmodel_neutral",sim_vortex_bodyfrictionmodel_neutral,true},
    {"sim.vortex_bodyfrictionmodel_none",sim_vortex_bodyfrictionmodel_none,true},
    // Bullet constraint solver types
    {"sim.bullet_constraintsolvertype_sequentialimpulse",sim_bullet_constraintsolvertype_sequentialimpulse,true},
    {"sim.bullet_constraintsolvertype_nncg",sim_bullet_constraintsolvertype_nncg,true},
    {"sim.bullet_constraintsolvertype_dantzig",sim_bullet_constraintsolvertype_dantzig,true},
    {"sim.bullet_constraintsolvertype_projectedgaussseidel",sim_bullet_constraintsolvertype_projectedgaussseidel,true},
    // Filter component types:
    {"sim.filtercomponent_originalimage",sim_filtercomponent_originalimage_deprecated,true},
    {"sim.filtercomponent_originaldepth",sim_filtercomponent_originaldepth_deprecated,true},
    {"sim.filtercomponent_uniformimage",sim_filtercomponent_uniformimage_deprecated,true},
    {"sim.filtercomponent_tooutput",sim_filtercomponent_tooutput_deprecated,true},
    {"sim.filtercomponent_tobuffer1",sim_filtercomponent_tobuffer1_deprecated,true},
    {"sim.filtercomponent_tobuffer2",sim_filtercomponent_tobuffer2_deprecated,true},
    {"sim.filtercomponent_frombuffer1",sim_filtercomponent_frombuffer1_deprecated,true},
    {"sim.filtercomponent_frombuffer2",sim_filtercomponent_frombuffer2_deprecated,true},
    {"sim.filtercomponent_swapbuffers",sim_filtercomponent_swapbuffers_deprecated,true},
    {"sim.filtercomponent_addbuffer1",sim_filtercomponent_addbuffer1_deprecated,true},
    {"sim.filtercomponent_subtractbuffer1",sim_filtercomponent_subtractbuffer1_deprecated,true},
    {"sim.filtercomponent_multiplywithbuffer1",sim_filtercomponent_multiplywithbuffer1_deprecated,true},
    {"sim.filtercomponent_horizontalflip",sim_filtercomponent_horizontalflip_deprecated,true},
    {"sim.filtercomponent_verticalflip",sim_filtercomponent_verticalflip_deprecated,true},
    {"sim.filtercomponent_rotate",sim_filtercomponent_rotate_deprecated,true},
    {"sim.filtercomponent_shift",sim_filtercomponent_shift_deprecated,true},
    {"sim.filtercomponent_resize",sim_filtercomponent_resize_deprecated,true},
    {"sim.filtercomponent_3x3filter",sim_filtercomponent_3x3filter_deprecated,true},
    {"sim.filtercomponent_5x5filter",sim_filtercomponent_5x5filter_deprecated,true},
    {"sim.filtercomponent_sharpen",sim_filtercomponent_sharpen_deprecated,true},
    {"sim.filtercomponent_edge",sim_filtercomponent_edge_deprecated,true},
    {"sim.filtercomponent_rectangularcut",sim_filtercomponent_rectangularcut_deprecated,true},
    {"sim.filtercomponent_circularcut",sim_filtercomponent_circularcut_deprecated,true},
    {"sim.filtercomponent_normalize",sim_filtercomponent_normalize_deprecated,true},
    {"sim.filtercomponent_intensityscale",sim_filtercomponent_intensityscale_deprecated,true},
    {"sim.filtercomponent_keeporremovecolors",sim_filtercomponent_keeporremovecolors_deprecated,true},
    {"sim.filtercomponent_scaleandoffsetcolors",sim_filtercomponent_scaleandoffsetcolors_deprecated,true},
    {"sim.filtercomponent_binary",sim_filtercomponent_binary_deprecated,true},
    {"sim.filtercomponent_swapwithbuffer1",sim_filtercomponent_swapwithbuffer1_deprecated,true},
    {"sim.filtercomponent_addtobuffer1",sim_filtercomponent_addtobuffer1_deprecated,true},
    {"sim.filtercomponent_subtractfrombuffer1",sim_filtercomponent_subtractfrombuffer1_deprecated,true},
    {"sim.filtercomponent_correlationwithbuffer1",sim_filtercomponent_correlationwithbuffer1_deprecated,true},
    {"sim.filtercomponent_colorsegmentation",sim_filtercomponent_colorsegmentation_deprecated,true},
    {"sim.filtercomponent_blobextraction",sim_filtercomponent_blobextraction_deprecated,true},
    {"sim.filtercomponent_imagetocoord",sim_filtercomponent_imagetocoord_deprecated,true},
    {"sim.filtercomponent_pixelchange",sim_filtercomponent_pixelchange_deprecated,true},
    {"sim.filtercomponent_velodyne",sim_filtercomponent_velodyne_deprecated,true},
    {"sim.filtercomponent_todepthoutput",sim_filtercomponent_todepthoutput_deprecated,true},
    {"sim.filtercomponent_customized",sim_filtercomponent_customized_deprecated,true},
    // buffer types:
    {"sim.buffer_uint8",sim_buffer_uint8,true},
    {"sim.buffer_int8",sim_buffer_int8,true},
    {"sim.buffer_uint16",sim_buffer_uint16,true},
    {"sim.buffer_int16",sim_buffer_int16,true},
    {"sim.buffer_uint32",sim_buffer_uint32,true},
    {"sim.buffer_int32",sim_buffer_int32,true},
    {"sim.buffer_float",sim_buffer_float,true},
    {"sim.buffer_double",sim_buffer_double,true},
    {"sim.buffer_uint8rgb",sim_buffer_uint8rgb,true},
    {"sim.buffer_uint8bgr",sim_buffer_uint8bgr,true},
    {"sim.buffer_uint8rgba",sim_buffer_uint8rgba,true},
    {"sim.buffer_uint8argb",sim_buffer_uint8argb,true},
    {"sim.buffer_base64",sim_buffer_base64,true},
    {"sim.buffer_split",sim_buffer_split,true},
    {"sim.buffer_clamp",sim_buffer_clamp,true},
    // Image combination:
    {"sim.imgcomb_vertical",sim_imgcomb_vertical,true},
    {"sim.imgcomb_horizontal",sim_imgcomb_horizontal,true},
    // Graph data stream transformations:
    {"sim.stream_transf_raw",sim_stream_transf_raw,true},
    {"sim.stream_transf_derivative",sim_stream_transf_derivative,true},
    {"sim.stream_transf_integral",sim_stream_transf_integral,true},
    {"sim.stream_transf_cumulative",sim_stream_transf_cumulative,true},
    // predefined material types:
    {"sim.dynmat_default",sim_dynmat_default,true},
    {"sim.dynmat_highfriction",sim_dynmat_highfriction,true},
    {"sim.dynmat_lowfriction",sim_dynmat_lowfriction,true},
    {"sim.dynmat_nofriction",sim_dynmat_nofriction,true},
    {"sim.dynmat_reststackgrasp",sim_dynmat_reststackgrasp,true},
    {"sim.dynmat_foot",sim_dynmat_foot,true},
    {"sim.dynmat_wheel",sim_dynmat_wheel,true},
    {"sim.dynmat_gripper",sim_dynmat_gripper,true},
    {"sim.dynmat_floor",sim_dynmat_floor,true},
    // Ruckig:
    {"sim.ruckig_phasesync",sim_ruckig_phasesync,true},
    {"sim.ruckig_timesync",sim_ruckig_timesync,true},
    {"sim.ruckig_nosync",sim_ruckig_nosync,true},
    {"sim.ruckig_minvel",sim_ruckig_minvel,true},
    {"sim.ruckig_minaccel",sim_ruckig_minaccel,true},

    // deprecated!
    {"sim.boolparam_force_show_wireless_emission",sim_boolparam_force_show_wireless_emission,false},
    {"sim.boolparam_force_show_wireless_reception",sim_boolparam_force_show_wireless_reception,false},
    {"sim.boolparam_collision_handling_enabled",sim_boolparam_collision_handling_enabled,false},
    {"sim.boolparam_distance_handling_enabled",sim_boolparam_distance_handling_enabled,false},
    {"sim.boolparam_proximity_sensor_handling_enabled",sim_boolparam_proximity_sensor_handling_enabled,false},
    {"sim.boolparam_vision_sensor_handling_enabled",sim_boolparam_vision_sensor_handling_enabled,false},
    {"sim.boolparam_rendering_sensor_handling_enabled",sim_boolparam_vision_sensor_handling_enabled,false},
    {"sim.boolparam_mill_handling_enabled",sim_boolparam_mill_handling_enabled,false},
    {"sim.modelproperty_not_renderable",sim_modelproperty_not_renderable,false},
    {"sim.objectspecialproperty_detectable_ultrasonic",sim_objectspecialproperty_detectable_ultrasonic,false},
    {"sim.objectspecialproperty_detectable_infrared",sim_objectspecialproperty_detectable_infrared,false},
    {"sim.objectspecialproperty_detectable_laser",sim_objectspecialproperty_detectable_laser,false},
    {"sim.objectspecialproperty_detectable_inductive",sim_objectspecialproperty_detectable_inductive,false},
    {"sim.objectspecialproperty_detectable_capacitive",sim_objectspecialproperty_detectable_capacitive,false},
    {"sim.objectspecialproperty_renderable",sim_objectspecialproperty_renderable,false},
    {"sim.objectspecialproperty_detectable_all",sim_objectspecialproperty_detectable,false},
    {"sim.objectspecialproperty_pathplanning_ignored",sim_objectspecialproperty_pathplanning_ignored,false},
    {"sim.objectproperty_canupdatedna",sim_objectproperty_canupdatedna,false},
    {"sim.objectproperty_hierarchyhiddenmodelchild",sim_objectproperty_hierarchyhiddenmodelchild,false},
    {"sim.jointmode_ik",sim_jointmode_ik_deprecated,false},
    {"sim.jointmode_ikdependent",sim_jointmode_reserved_previously_ikdependent,false},
    {"sim.appobj_ik_type",sim_appobj_ik_type,false},
    {"sim.ik_pseudo_inverse_method",sim_ik_pseudo_inverse_method,false},
    {"sim.ik_damped_least_squares_method",sim_ik_damped_least_squares_method,false},
    {"sim.ik_jacobian_transpose_method",sim_ik_jacobian_transpose_method,false},
    {"sim.ik_undamped_pseudo_inverse_method",sim_ik_undamped_pseudo_inverse_method,false},
    {"sim.ik_x_constraint",sim_ik_x_constraint,false},
    {"sim.ik_y_constraint",sim_ik_y_constraint,false},
    {"sim.ik_z_constraint",sim_ik_z_constraint,false},
    {"sim.ik_alpha_beta_constraint",sim_ik_alpha_beta_constraint,false},
    {"sim.ik_gamma_constraint",sim_ik_gamma_constraint,false},
    {"sim.ikresult_not_performed",sim_ikresult_not_performed,false},
    {"sim.ikresult_success",sim_ikresult_success,false},
    {"sim.ikresult_fail",sim_ikresult_fail,false},
    {"sim.dummy_linktype_gcs_loop_closure",sim_dummy_linktype_gcs_loop_closure,false},
    {"sim.dummy_linktype_gcs_tip",sim_dummy_linktype_gcs_tip,false},
    {"sim.dummy_linktype_gcs_target",sim_dummy_linktype_gcs_target,false},
    {"sim.dummy_linktype_ik_tip_target",sim_dummy_linktype_ik_tip_target,false},
    {"sim.dummy_linktype_dynamics_force_constraint",sim_dummy_linktype_dynamics_force_constraint,false},
    {"sim.jointfloatparam_step_size",sim_jointfloatparam_step_size,false},
    {"sim.jointfloatparam_ik_weight",sim_jointfloatparam_ik_weight,false},
    {"sim.boolparam_ik_handling_enabled",sim_boolparam_ik_handling_enabled,false},
    {"sim.boolparam_gcs_handling_enabled",sim_boolparam_gcs_handling_enabled,false},
    {"sim.appobj_2delement_type",sim_appobj_ui_type,false},  // for backward compatibility
    {"sim.appobj_ui_type",sim_appobj_ui_type,false},
    {"sim.appobj_pathplanning_type",sim_appobj_pathplanning_type,false},
    {"sim.scripttype_threaded",sim_scripttype_threaded_old,false},
    {"sim.navigation_camerafly",sim_navigation_camerafly_old,false},
    {"sim.banner_left",sim_banner_left,false},
    {"sim.banner_right",sim_banner_right,false},
    {"sim.banner_nobackground",sim_banner_nobackground,false},
    {"sim.banner_overlay",sim_banner_overlay,false},
    {"sim.banner_followparentvisibility",sim_banner_followparentvisibility,false},
    {"sim.banner_clickselectsparent",sim_banner_clickselectsparent,false},
    {"sim.banner_clicktriggersevent",sim_banner_clicktriggersevent,false},
    {"sim.banner_facingcamera",sim_banner_facingcamera,false},
    {"sim.banner_fullyfacingcamera",sim_banner_fullyfacingcamera,false},
    {"sim.banner_backfaceculling",sim_banner_backfaceculling,false},
    {"sim.banner_keepsamesize",sim_banner_keepsamesize,false},
    {"sim.banner_bitmapfont",sim_banner_bitmapfont,false},
    {"sim.scriptdebug_none",sim_scriptdebug_none,false},
    {"sim.scriptdebug_syscalls",sim_scriptdebug_syscalls,false},
    {"sim.scriptdebug_vars_interval",sim_scriptdebug_vars_interval,false},
    {"sim.scriptdebug_allcalls",sim_scriptdebug_allcalls,false},
    {"sim.scriptdebug_vars",sim_scriptdebug_vars,false},
    {"sim.scriptdebug_callsandvars",sim_scriptdebug_callsandvars,false},
    {"sim.scriptthreadresume_allnotyetresumed",sim_scriptthreadresume_allnotyetresumed,false},
    {"sim.scriptthreadresume_default",sim_scriptthreadresume_default,false},
    {"sim.scriptthreadresume_actuation_first",sim_scriptthreadresume_actuation_first,false},
    {"sim.scriptthreadresume_actuation_last",sim_scriptthreadresume_actuation_last,false},
    {"sim.scriptthreadresume_sensing_first",sim_scriptthreadresume_sensing_first,false},
    {"sim.scriptthreadresume_sensing_last",sim_scriptthreadresume_sensing_last,false},
    {"sim.scriptthreadresume_custom",sim_scriptthreadresume_custom,false},
    {"sim.callbackid_rossubscriber",sim_callbackid_rossubscriber,false},
    {"sim.callbackid_dynstep",sim_callbackid_dynstep,false},
    {"sim.callbackid_userdefined",sim_callbackid_userdefined,false},
    {"sim.buttonproperty_button",sim_buttonproperty_button,false},
    {"sim.buttonproperty_label",sim_buttonproperty_label,false},
    {"sim.buttonproperty_editbox",sim_buttonproperty_editbox,false},
    {"sim.buttonproperty_slider",sim_buttonproperty_slider,false},
    {"sim.buttonproperty_staydown",sim_buttonproperty_staydown,false},
    {"sim.buttonproperty_enabled",sim_buttonproperty_enabled,false},
    {"sim.buttonproperty_borderless",sim_buttonproperty_borderless,false},
    {"sim.buttonproperty_horizontallycentered",sim_buttonproperty_horizontallycentered,false},
    {"sim.buttonproperty_ignoremouse",sim_buttonproperty_ignoremouse,false},
    {"sim.buttonproperty_isdown",sim_buttonproperty_isdown,false},
    {"sim.buttonproperty_transparent",sim_buttonproperty_transparent,false},
    {"sim.buttonproperty_nobackgroundcolor",sim_buttonproperty_nobackgroundcolor,false},
    {"sim.buttonproperty_rollupaction",sim_buttonproperty_rollupaction,false},
    {"sim.buttonproperty_closeaction",sim_buttonproperty_closeaction,false},
    {"sim.buttonproperty_verticallycentered",sim_buttonproperty_verticallycentered,false},
    {"sim.buttonproperty_downupevent",sim_buttonproperty_downupevent,false},
    {"sim.pathproperty_show_line",sim_pathproperty_show_line,false},
    {"sim.pathproperty_show_orientation",sim_pathproperty_show_orientation,false},
    {"sim.pathproperty_closed_path",sim_pathproperty_closed_path,false},
    {"sim.pathproperty_automatic_orientation",sim_pathproperty_automatic_orientation,false},
    {"sim.pathproperty_flat_path",sim_pathproperty_flat_path,false},
    {"sim.pathproperty_show_position",sim_pathproperty_show_position,false},
    {"sim.pathproperty_keep_x_up",sim_pathproperty_keep_x_up,false},
    {"sim.distcalcmethod_dl",sim_distcalcmethod_dl,false},
    {"sim.distcalcmethod_dac",sim_distcalcmethod_dac,false},
    {"sim.distcalcmethod_max_dl_dac",sim_distcalcmethod_max_dl_dac,false},
    {"sim.distcalcmethod_dl_and_dac",sim_distcalcmethod_dl_and_dac,false},
    {"sim.distcalcmethod_sqrt_dl2_and_dac2",sim_distcalcmethod_sqrt_dl2_and_dac2,false},
    {"sim.distcalcmethod_dl_if_nonzero",sim_distcalcmethod_dl_if_nonzero,false},
    {"sim.distcalcmethod_dac_if_nonzero",sim_distcalcmethod_dac_if_nonzero,false},
    {"sim.appobj_collision_type",sim_appobj_collision_type,false},
    {"sim.appobj_distance_type",sim_appobj_distance_type,false},
    {"sim.appobj_collection_type",sim_appobj_collection_type,false},
    {"sim.message_ui_button_state_change",sim_message_ui_button_state_change,false},
    {"sim.message_bannerclicked",sim_message_bannerclicked,false},
    {"sim.message_prox_sensor_select_down",sim_message_prox_sensor_select_down,false},
    {"sim.message_prox_sensor_select_up",sim_message_prox_sensor_select_up,false},
    {"sim.message_pick_select_down",sim_message_pick_select_down,false},
    {"sim.scripttype_addonfunction",sim_scripttype_addonfunction,false},
    {"sim.customizationscriptattribute_activeduringsimulation",sim_customizationscriptattribute_activeduringsimulation,false},
    {"sim.childscriptattribute_automaticcascadingcalls",sim_childscriptattribute_automaticcascadingcalls,false},
    {"sim.customizationscriptattribute_cleanupbeforesave",sim_customizationscriptattribute_cleanupbeforesave,false},
    {"sim.scriptattribute_debuglevel",sim_scriptattribute_debuglevel,false},
    {"sim.script_no_error",sim_script_no_error,false},
    {"sim.script_main_script_nonexistent",sim_script_main_script_nonexistent,false},
    {"sim.script_main_not_called",sim_script_main_script_not_called,false},
    {"sim.script_reentrance_error",sim_script_reentrance_error,false},
    {"sim.script_lua_error",sim_script_lua_error,false},
    {"sim.script_call_error",sim_script_call_error,false},
    {"sim.api_error_report",sim_api_error_report,false},
    {"sim.api_error_output",sim_api_error_output,false},
    {"sim.api_warning_output",sim_api_warning_output,false},
    {"sim.boolparam_rml2_available",sim_boolparam_rml2_available,false},
    {"sim.boolparam_rml4_available",sim_boolparam_rml4_available,false},
    {"sim.rml_phase_sync_if_possible",sim_ruckig_phasesync,false},
    {"sim.rml_only_time_sync",sim_ruckig_timesync,false},
    {"sim.rml_only_phase_sync",sim_ruckig_timesync,false},
    {"sim.rml_no_sync",sim_ruckig_nosync,false},
    {"sim.rml_keep_target_vel",0,false},
    {"sim.rml_recompute_trajectory",0,false},
    {"sim.rml_disable_extremum_motion_states_calc",0,false},
    {"sim.rml_keep_current_vel_if_fallback_strategy",0,false},
    {"sim.filedlg_type_load",sim_filedlg_type_load,false},
    {"sim.filedlg_type_save",sim_filedlg_type_save,false},
    {"sim.filedlg_type_load_multiple",sim_filedlg_type_load_multiple,false},
    {"sim.filedlg_type_folder",sim_filedlg_type_folder,false},
    {"sim.msgbox_type_info",sim_msgbox_type_info,false},
    {"sim.msgbox_type_question",sim_msgbox_type_question,false},
    {"sim.msgbox_type_warning",sim_msgbox_type_warning,false},
    {"sim.msgbox_type_critical",sim_msgbox_type_critical,false},
    {"sim.msgbox_buttons_ok",sim_msgbox_buttons_ok,false},
    {"sim.msgbox_buttons_yesno",sim_msgbox_buttons_yesno,false},
    {"sim.msgbox_buttons_yesnocancel",sim_msgbox_buttons_yesnocancel,false},
    {"sim.msgbox_buttons_okcancel",sim_msgbox_buttons_okcancel,false},
    {"sim.msgbox_return_cancel",sim_msgbox_return_cancel,false},
    {"sim.msgbox_return_no",sim_msgbox_return_no,false},
    {"sim.msgbox_return_yes",sim_msgbox_return_yes,false},
    {"sim.msgbox_return_ok",sim_msgbox_return_ok,false},
    {"sim.msgbox_return_error",sim_msgbox_return_error,false},
    {"sim.dlgstyle_message",sim_dlgstyle_message,false},
    {"sim.dlgstyle_input",sim_dlgstyle_input,false},
    {"sim.dlgstyle_ok",sim_dlgstyle_ok,false},
    {"sim.dlgstyle_ok_cancel",sim_dlgstyle_ok_cancel,false},
    {"sim.dlgstyle_yes_no",sim_dlgstyle_yes_no,false},
    {"sim.dlgstyle_dont_center",sim_dlgstyle_dont_center,false},
    {"sim.dlgret_still_open",sim_dlgret_still_open,false},
    {"sim.dlgret_ok",sim_dlgret_ok,false},
    {"sim.dlgret_cancel",sim_dlgret_cancel,false},
    {"sim.dlgret_yes",sim_dlgret_yes,false},
    {"sim.dlgret_no",sim_dlgret_no,false},
    {"sim.scriptattribute_executionorder",sim_scriptattribute_executionorder,false},
    {"sim.scriptattribute_executioncount",sim_scriptattribute_executioncount,false},
    {"sim.childscriptattribute_enabled",sim_childscriptattribute_enabled,false},
    {"sim.scriptattribute_enabled",sim_scriptattribute_enabled,false},
    {"sim.scriptattribute_scripttype",sim_scriptattribute_scripttype,false},
    {"sim.scriptattribute_scripthandle",sim_scriptattribute_scripthandle,false},
    {"sim.drawing_trianglepoints",sim_drawing_trianglepoints,false},
    {"sim.drawing_quadpoints",sim_drawing_quadpoints,false},
    {"sim.drawing_discpoints",sim_drawing_discpoints,false},
    {"sim.drawing_cubepoints",sim_drawing_cubepoints,false},
    {"sim.drawing_spherepoints",sim_drawing_spherepoints,false},
    {"sim.pure_primitive_none",sim_primitiveshape_none,false},
    {"sim.pure_primitive_plane",sim_primitiveshape_plane,false},
    {"sim.pure_primitive_disc",sim_primitiveshape_disc,false},
    {"sim.pure_primitive_cuboid",sim_primitiveshape_cuboid,false},
    {"sim.pure_primitive_spheroid",sim_primitiveshape_spheroid,false},
    {"sim.pure_primitive_cylinder",sim_primitiveshape_cylinder,false},
    {"sim.pure_primitive_cone",sim_primitiveshape_cone,false},
    {"sim.pure_primitive_heightfield",sim_primitiveshape_heightfield,false},
    {"sim.jointmode_passive",sim_jointmode_kinematic,false},
    {"sim.jointmode_force",sim_jointmode_dynamic,false},
    {"sim.dummy_linktype_dynamics_loop_closure",sim_dummylink_dynloopclosure,false},
    {"sim.bullet_global_stepsize",sim_bullet_global_stepsize,false},
    {"sim.ode_global_stepsize",sim_ode_global_stepsize,false},
    {"sim.vortex_global_stepsize",sim_vortex_global_stepsize,false},
    {"sim.newton_global_stepsize",sim_newton_global_stepsize,false},
    {"sim.jointfloatparam_pid_p",sim_jointfloatparam_pid_p,false},
    {"sim.jointfloatparam_pid_i",sim_jointfloatparam_pid_i,false},
    {"sim.jointfloatparam_pid_d",sim_jointfloatparam_pid_d,false},
    {"sim.jointfloatparam_upper_limit",sim_jointfloatparam_upper_limit,false},
    {"sim.intparam_simulation_warning_disabled_mask",sim_intparam_simulation_warning_disabled_mask,false},
    {"sim.syscb_contactcallback",sim_syscb_contactcallback,false},
    {"sim.syscb_dyncallback",sim_syscb_dyncallback,false},
    {"sim.syscb_customcallback1",sim_syscb_customcallback1,false},
    {"sim.syscb_customcallback2",sim_syscb_customcallback2,false},
    {"sim.syscb_customcallback3",sim_syscb_customcallback3,false},
    {"sim.syscb_customcallback4",sim_syscb_customcallback4,false},
    {"sim.syscb_jointcallback",sim_syscb_jointcallback,false},
    {"sim.navigation_cameratilt",sim_navigation_cameratilt,false},

    {"",-1}
};

void _registerTableFunction(luaWrap_lua_State* L,char const* const tableName,char const* const functionName,luaWrap_lua_CFunction functionCallback)
{
    luaWrap_lua_rawgeti(L,luaWrapGet_LUA_REGISTRYINDEX(),luaWrapGet_LUA_RIDX_GLOBALS()); // table of globals
    luaWrap_lua_getfield(L,-1,tableName);
    if (!luaWrap_lua_istable(L,-1))
    { // we first need to create the table
        luaWrap_lua_createtable(L,0,1);
        luaWrap_lua_setfield(L,-3,tableName);
        luaWrap_lua_pop(L,1);
        luaWrap_lua_getfield(L,-1,tableName);
    }
    luaWrap_lua_pushstring(L,functionName);
    luaWrap_lua_pushcfunction(L,functionCallback);
    luaWrap_lua_settable(L,-3);
    luaWrap_lua_pop(L,1);
    luaWrap_lua_pop(L,1); // pop table of globals
}

void getFloatsFromTable(luaWrap_lua_State* L,int tablePos,size_t floatCount,float* arrayField)
{
    for (size_t i=0;i<floatCount;i++)
    {
        luaWrap_lua_rawgeti(L,tablePos,int(i+1));
        arrayField[i]=(float)luaWrap_lua_tonumber(L,-1);
        luaWrap_lua_pop(L,1); // we pop one element from the stack;
    }
}

void getDoublesFromTable(luaWrap_lua_State* L,int tablePos,size_t doubleCount,double* arrayField)
{
    for (size_t i=0;i<doubleCount;i++)
    {
        luaWrap_lua_rawgeti(L,tablePos,int(i+1));
        arrayField[i]=luaWrap_lua_tonumber(L,-1);
        luaWrap_lua_pop(L,1); // we pop one element from the stack;
    }
}

bool getIntsFromTable(luaWrap_lua_State* L,int tablePos,size_t intCount,int* arrayField)
{
    for (size_t i=0;i<intCount;i++)
    {
        luaWrap_lua_rawgeti(L,tablePos,int(i+1));
        if (!luaWrap_lua_isnumber(L,-1))
        {
            luaWrap_lua_pop(L,1); // we pop one element from the stack;
            return(false); // Not a number!!
        }
        arrayField[i]=luaToInt(L,-1);
        luaWrap_lua_pop(L,1); // we pop one element from the stack;
    }
    return(true);
}

bool getUIntsFromTable(luaWrap_lua_State* L,int tablePos,size_t intCount,unsigned int* arrayField)
{
    for (size_t i=0;i<intCount;i++)
    {
        luaWrap_lua_rawgeti(L,tablePos,int(i+1));
        if (!luaWrap_lua_isnumber(L,-1))
        {
            luaWrap_lua_pop(L,1); // we pop one element from the stack;
            return(false); // Not a number!!
        }
        arrayField[i]=(unsigned int)luaToInt(L,-1);
        luaWrap_lua_pop(L,1); // we pop one element from the stack;
    }
    return(true);
}

bool getUCharsFromTable(luaWrap_lua_State* L,int tablePos,size_t intCount,unsigned char* arrayField)
{
    for (size_t i=0;i<intCount;i++)
    {
        luaWrap_lua_rawgeti(L,tablePos,int(i+1));
        if (!luaWrap_lua_isnumber(L,-1))
        {
            luaWrap_lua_pop(L,1); // we pop one element from the stack;
            return(false); // Not a number!!
        }
        arrayField[i]=(unsigned char)luaToInt(L,-1);
        luaWrap_lua_pop(L,1); // we pop one element from the stack;
    }
    return(true);
}

void getCharBoolsFromTable(luaWrap_lua_State* L,int tablePos,size_t boolCount,char* arrayField)
{
    for (size_t i=0;i<boolCount;i++)
    {
        luaWrap_lua_rawgeti(L,tablePos,int(i+1));
        arrayField[i]=(char)luaToBool(L,-1);
        luaWrap_lua_pop(L,1); // we pop one element from the stack;
    }
}

void pushFloatTableOntoStack(luaWrap_lua_State* L,size_t floatCount,const float* arrayField)
{
    luaWrap_lua_newtable(L);
    int newTablePos=luaWrap_lua_gettop(L);
    for (size_t i=0;i<floatCount;i++)
    {
        luaWrap_lua_pushnumber(L,arrayField[i]);
        luaWrap_lua_rawseti(L,newTablePos,int(i+1));
    }
}

void pushDoubleTableOntoStack(luaWrap_lua_State* L,size_t doubleCount,const double* arrayField)
{
    luaWrap_lua_newtable(L);
    int newTablePos=luaWrap_lua_gettop(L);
    for (size_t i=0;i<doubleCount;i++)
    {
        luaWrap_lua_pushnumber(L,arrayField[i]);
        luaWrap_lua_rawseti(L,newTablePos,int(i+1));
    }
}

void pushIntTableOntoStack(luaWrap_lua_State* L,size_t intCount,const int* arrayField)
{
    luaWrap_lua_newtable(L);
    int newTablePos=luaWrap_lua_gettop(L);
    for (size_t i=0;i<intCount;i++)
    {
        luaWrap_lua_pushinteger(L,arrayField[i]);
        luaWrap_lua_rawseti(L,newTablePos,int(i+1));
    }
}

void pushUIntTableOntoStack(luaWrap_lua_State* L,size_t intCount,const unsigned int* arrayField)
{
    luaWrap_lua_newtable(L);
    int newTablePos=luaWrap_lua_gettop(L);
    for (size_t i=0;i<intCount;i++)
    {
        luaWrap_lua_pushinteger(L,arrayField[i]);
        luaWrap_lua_rawseti(L,newTablePos,int(i+1));
    }
}

void pushUCharTableOntoStack(luaWrap_lua_State* L,size_t intCount,const unsigned char* arrayField)
{
    luaWrap_lua_newtable(L);
    int newTablePos=luaWrap_lua_gettop(L);
    for (size_t i=0;i<intCount;i++)
    {
        luaWrap_lua_pushinteger(L,arrayField[i]);
        luaWrap_lua_rawseti(L,newTablePos,int(i+1));
    }
}

void pushStringTableOntoStack(luaWrap_lua_State* L,const std::vector<std::string>& stringTable)
{
    luaWrap_lua_newtable(L);
    int newTablePos=luaWrap_lua_gettop(L);
    for (size_t i=0;i<stringTable.size();i++)
    {
        luaWrap_lua_pushstring(L,stringTable[i].c_str());
        luaWrap_lua_rawseti(L,newTablePos,(int)i+1);
    }
}

void pushLStringTableOntoStack(luaWrap_lua_State* L,const std::vector<std::string>& stringTable)
{
    luaWrap_lua_newtable(L);
    int newTablePos=luaWrap_lua_gettop(L);
    for (size_t i=0;i<stringTable.size();i++)
    {
        luaWrap_lua_pushlstring(L,stringTable[i].c_str(),stringTable[i].size());
        luaWrap_lua_rawseti(L,newTablePos,(int)i+1);
    }
}

int luaToInt(luaWrap_lua_State* L,int pos)
{
    return((int)luaWrap_lua_tointeger(L,pos));
}

double luaToDouble(luaWrap_lua_State* L,int pos)
{
    return(luaWrap_lua_tonumber(L,pos));
}

bool luaToBool(luaWrap_lua_State* L,int pos)
{
    return(luaWrap_lua_toboolean(L,pos)!=0);
}

int checkOneGeneralInputArgument(luaWrap_lua_State* L,int index,
                           int type,int cnt_orZeroIfNotTable,bool optional,bool nilInsteadOfTypeAndCountAllowed,std::string* errStr)
{ // return -1 means error, 0 means data is missing, 1 means data is nil, 2 means data is ok
    // if cnt_orZeroIfNotTable is -1, we are expecting a table, which could also be empty
    // 1. We check if there is something on the stack at that position:
    if (luaWrap_lua_gettop(L)<index)
    { // That data is missing:
        if (optional)
            return(0);
        if (errStr!=nullptr)
            errStr->assign(SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS);
        return(-1);
    }
    // 2. We check if we have nil on the stack:
    if (luaWrap_lua_isnil(L,index))
    { // We have nil.
        // Did we expect a boolean? If yes, it is ok
        if ( (type==lua_arg_bool)&&(cnt_orZeroIfNotTable==0) )
            return(2);
        if ( (type==lua_arg_nil)&&(cnt_orZeroIfNotTable==0) )
            return(2);
        if (nilInsteadOfTypeAndCountAllowed)
            return(1);
        if (errStr!=nullptr)
            errStr->assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
        return(-1);
    }
    // 3. we check if we expect a table:
    if (cnt_orZeroIfNotTable!=0) // was >=1 until 18/2/2016
    { 
        // We check if we really have a table at that position:
        if (!luaWrap_lua_istable(L,index))
        {
            if (errStr!=nullptr)
                errStr->assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
            return(-1);
        }
        // we check the table size:
        if (int(luaWrap_lua_rawlen(L,index))<cnt_orZeroIfNotTable)
        { // the size is not correct
            if (errStr!=nullptr)
                errStr->assign(SIM_ERROR_ONE_TABLE_SIZE_IS_WRONG);
            return(-1);
        }
        else
        { // we have the correct size
            // Now we need to check each element individually for the correct type:
            for (int i=0;i<cnt_orZeroIfNotTable;i++)
            {
                luaWrap_lua_rawgeti(L,index,i+1);
                if (!checkOneInputArgument(L,-1,type,errStr))
                    return(-1);
                luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
            }
            // Everything went fine:
            return(2);
        }
    }
    else
    { // we expect a non-table type
        if (checkOneInputArgument(L,index,type,errStr))
            return(2);
        return(-1);
    }
}

bool checkOneInputArgument(luaWrap_lua_State* L,int index,int type,std::string* errStr)
{
    // 1. We check if there is something on the stack at that position:
    if (luaWrap_lua_gettop(L)<index)
    { // That data is missing:
        if (errStr!=nullptr)
            errStr->assign(SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS);
        return(false);
    }
    if (type==lua_arg_number)
    {
        if (!luaWrap_lua_isnumber(L,index))
        {
            if (errStr!=nullptr)
                errStr->assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
            return(false); // error
        }
        return(true);
    }
    if (type==lua_arg_integer)
    {
        if (!luaWrap_lua_isinteger(L,index))
        {
            if (errStr!=nullptr)
                errStr->assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
            return(false); // error
        }
        return(true);
    }
    if (type==lua_arg_bool)
    { // since anything can be a bool value, we don't generate any error!
        return(true);
    }
    if (type==lua_arg_nil)
    { // Here we expect a nil value:
        if (!luaWrap_lua_isnil(L,index))
        {
            if (errStr!=nullptr)
                errStr->assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
            return(false);
        }
        return(true);
    }
    if (type==lua_arg_string)
    {
        if (!luaWrap_lua_isstring(L,index))
        {
            if (errStr!=nullptr)
                errStr->assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
            return(false); // error
        }
        return(true);
    }
    if (type==lua_arg_table)
    {
        if (!luaWrap_lua_istable(L,index))
        {
            if (errStr!=nullptr)
                errStr->assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
            return(false); // error
        }
        return(true);
    }
    if (type==lua_arg_function)
    {
        if (!luaWrap_lua_isfunction(L,index))
        {
            if (errStr!=nullptr)
                errStr->assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
            return(false); // error
        }
        return(true);
    }
    if (type==lua_arg_userdata)
    {
        if (!luaWrap_lua_isuserdata(L,index))
        {
            if (errStr!=nullptr)
                errStr->assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
            return(false); // error
        }
        return(true);
    }
    // Here we have a table
    if (errStr!=nullptr)
        errStr->assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
    return(false);
}

bool checkInputArguments(luaWrap_lua_State* L,std::string* errStr,
                         int type1,int type1Cnt_zeroIfNotTable,
                         int type2,int type2Cnt_zeroIfNotTable,
                         int type3,int type3Cnt_zeroIfNotTable,
                         int type4,int type4Cnt_zeroIfNotTable,
                         int type5,int type5Cnt_zeroIfNotTable,
                         int type6,int type6Cnt_zeroIfNotTable,
                         int type7,int type7Cnt_zeroIfNotTable,
                         int type8,int type8Cnt_zeroIfNotTable,
                         int type9,int type9Cnt_zeroIfNotTable,
                         int type10,int type10Cnt_zeroIfNotTable,
                         int type11,int type11Cnt_zeroIfNotTable)
{ // all arguments, except L and the functionName have default values
    // CAREFUL!!! if typeXCnt_zeroIfNotTable is 0, it means it is a simple value (not table)
    // if typeXCnt_zeroIfNotTable is >=1 then we are expecting a table with at least typeXCnt_zeroIfNotTable elements!!
    // if typeXCnt_zeroIfNotTable is =-1 then we are expecting a table that can have any number of elements
    int inArgs[11]={type1,type2,type3,type4,type5,type6,type7,type8,type9,type10,type11};
    int inArgsCnt_zeroIfNotTable[11]={type1Cnt_zeroIfNotTable,type2Cnt_zeroIfNotTable,type3Cnt_zeroIfNotTable,type4Cnt_zeroIfNotTable,type5Cnt_zeroIfNotTable,type6Cnt_zeroIfNotTable,type7Cnt_zeroIfNotTable,type8Cnt_zeroIfNotTable,type9Cnt_zeroIfNotTable,type10Cnt_zeroIfNotTable,type11Cnt_zeroIfNotTable};
    int totArgs=0;
    for (int i=0;i<11;i++)
    {
        if (inArgs[i]==lua_arg_empty)
            break;
        if (inArgsCnt_zeroIfNotTable[i]<-1)
        {
            App::logMsg(sim_verbosity_errors,"error in call to 'checkInputArguments' routine.");
            App::beep();
            while (true);
        }
        totArgs++;
    }
    int numberOfArguments=luaWrap_lua_gettop(L);
    if (numberOfArguments<totArgs)
    { // we don't have enough arguments!
        if (errStr!=nullptr)
            errStr->assign(SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS);
        return(false); // error
    }
    for (int i=0;i<totArgs;i++)
    {
//        if (checkOneGeneralInputArgument(L,i+1,inArgs[i],inArgsCnt_zeroIfNotTable[i],false,false,errStr)==-1)
        if (checkOneGeneralInputArgument(L,i+1,inArgs[i],inArgsCnt_zeroIfNotTable[i],false,false,errStr)!=2)
            return(false); // error
    }
    return(true);
}

bool doesEntityExist(std::string* errStr,int identifier)
{
    if (identifier>=SIM_IDSTART_COLLECTION)
    {
        if (App::currentWorld->collections->getObjectFromHandle(identifier)==nullptr)
        {
            if (errStr!=nullptr)
                errStr[0]=SIM_ERROR_ENTITY_INEXISTANT;
            return(false);
        }
        return(true);
    }
    else
    {
        if (App::currentWorld->sceneObjects->getObjectFromHandle(identifier)==nullptr)
        {
            if (errStr!=nullptr)
                errStr[0]=SIM_ERROR_ENTITY_INEXISTANT;
            return(false);
        }
        return(true);
    }
}

int _genericFunctionHandler(luaWrap_lua_State* L,CScriptCustomFunction* func,std::string& raiseErrorWithMsg)
{
    TRACE_LUA_API;

    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* itObj=App::worldContainer->getScriptFromHandle(currentScriptID);

    CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
    CScriptObject::buildFromInterpreterStack_lua(L,stack,1,0); // all stack

    // Now we retrieve the object ID this script might be attached to:
    int linkedObject=-1;
    if (itObj->getScriptType()==sim_scripttype_childscript)
    {
        CSceneObject* obj=App::currentWorld->sceneObjects->getObjectFromHandle(itObj->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_childscript));
        if (obj!=nullptr)
            linkedObject=obj->getObjectHandle();
    }
    if (itObj->getScriptType()==sim_scripttype_customizationscript)
    {
        CSceneObject* obj=App::currentWorld->sceneObjects->getObjectFromHandle(itObj->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_customizationscript));
        if (obj!=nullptr)
            linkedObject=obj->getObjectHandle();
    }

    // We prepare the callback structure:
    SScriptCallBack* cb=new SScriptCallBack;
    cb->objectID=linkedObject;
    cb->scriptID=currentScriptID;
    cb->stackID=stack->getId();
    cb->waitUntilZero=0; // old
    char raiseErrorMsg[258]; // old
    raiseErrorMsg[0]='\0'; // old
    cb->raiseErrorWithMessage=(char*)&raiseErrorMsg; // old
    std::string source(luaWrap_getCurrentCodeSource(L));
    cb->source=(char*)source.c_str();
    cb->line=luaWrap_getCurrentCodeLine(L);

    // Now we can call the callback:
    CScriptObject::setInExternalCall(currentScriptID);
    func->callBackFunction_new(cb);
    CScriptObject::setInExternalCall(-1);

    bool dontDeleteStructureYet=false;
    while (cb->waitUntilZero!=0)
    { // backward compatibility (for real threads)
        if (!CThreadPool_old::switchBackToPreviousThread())
            break;
        if (CThreadPool_old::getSimulationStopRequestedAndActivated())
        { // give a chance to the c app to set the waitUntilZero to zero! (above turns true only 1-2 secs after the stop request arrived)
            // Following: the extension module might still write 0 into that position to signal "no more waiting" in
            // case this while loop got interrupted by a stop request.
            dontDeleteStructureYet=true;
            break;
        }
    }

    // Now we have to build the returned data onto the stack:
    CScriptObject::buildOntoInterpreterStack_lua(L,stack,false);

    if (strlen(cb->raiseErrorWithMessage)!=0)
        raiseErrorWithMsg+=cb->raiseErrorWithMessage; // is this mechanism used?! We probably simply use simSetLastError..?

    // And we return the number of arguments:
    int outputArgCount=stack->getStackSize();
    if (dontDeleteStructureYet)
    {   // We cannot yet delete the structure because an extension module might still write '0' into
        // p->waitUntilZero!! We delete the structure at the end of the simulation.
        App::currentWorld->embeddedScriptContainer->addCallbackStructureObjectToDestroyAtTheEndOfSimulation_new(cb);
    }
    else
        delete cb;
    App::worldContainer->interfaceStackContainer->destroyStack(stack);
    return(outputArgCount);
}

int _simGenericFunctionHandler(luaWrap_lua_State* L)
{   // THIS FUNCTION SHOULD NOT LOCK THE API (AT LEAST NOT WHILE CALLING THE CALLBACK!!) SINCE IT IS NOT DIRECTLY ACCESSING THE API!!!!
    TRACE_LUA_API;
    LUA_START("sim.genericFunctionHandler");

//    CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
//    it->printInterpreterStack();

    luaWrap_lua_pushvalue(L,luaWrap_lua_upvalueindex(1));
    int id=luaToInt(L,-1)-1;
    luaWrap_lua_pop(L,1); // we have to pop the pushed value to get the original stack state


//    it->printInterpreterStack();

    int outputArgCount=0;
    for (size_t j=0;j<App::worldContainer->scriptCustomFuncAndVarContainer->getCustomFunctionCount();j++)
    { // we now search for the callback to call:
        CScriptCustomFunction* it=App::worldContainer->scriptCustomFuncAndVarContainer->getCustomFunctionFromIndex(j);
        if (it->getFunctionID()==id)
        { // we have the right one! Now we need to prepare the input and output argument arrays:
            functionName=it->getFunctionName();
            App::logMsg(sim_verbosity_debug,(std::string("sim.genericFunctionHandler: ")+functionName).c_str());
            if (it->getPluginName().size()!=0)
            {
                functionName+="@simExt";
                functionName+=it->getPluginName();
            }
            else
                functionName+="@plugin";

            if (it->getUsesStackToExchangeData())
                outputArgCount=_genericFunctionHandler(L,it,errorString);
            else
                outputArgCount=_genericFunctionHandler_old(L,it);
            break;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(outputArgCount);
}

int _simHandleChildScripts(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.handleChildScripts");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int callType=luaToInt(L,1);
        int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        CScriptObject* it=App::currentWorld->embeddedScriptContainer->getScriptFromHandle(currentScriptID);
        if (it!=nullptr)
        {
            if (it->getScriptType()==sim_scripttype_mainscript)
            { // only the main script can call this function
                CInterfaceStack* inStack=App::worldContainer->interfaceStackContainer->createStack();
                CScriptObject::buildFromInterpreterStack_lua(L,inStack,2,0); // skip the first arg
                retVal=App::currentWorld->embeddedScriptContainer->callChildAndEmbeddedScripts(sim_scripttype_childscript,callType,inStack,nullptr);
                App::worldContainer->interfaceStackContainer->destroyStack(inStack);
            }
            /*
            silent error here, for backward compatibility!
            else
                errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT;
                */
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simHandleEmbeddedScripts(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.handleEmbeddedScripts");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int callType=luaToInt(L,1);
        int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        CScriptObject* it=App::currentWorld->embeddedScriptContainer->getScriptFromHandle(currentScriptID);
        if (it!=nullptr)
        {
            if (it->getScriptType()==sim_scripttype_mainscript)
            { // only the main script can call this function
                CInterfaceStack* inStack=App::worldContainer->interfaceStackContainer->createStack();
                CScriptObject::buildFromInterpreterStack_lua(L,inStack,2,0); // skip the first arg
                if (!App::userSettings->enableOldScriptTraversal)
                    retVal=App::currentWorld->embeddedScriptContainer->callChildAndEmbeddedScripts(-1,callType,inStack,nullptr);
                else
                {
                    retVal=App::currentWorld->embeddedScriptContainer->callChildAndEmbeddedScripts(sim_scripttype_childscript,callType,inStack,nullptr);
                    App::currentWorld->embeddedScriptContainer->callChildAndEmbeddedScripts(sim_scripttype_customizationscript,callType,inStack,nullptr);
                }
                App::worldContainer->interfaceStackContainer->destroyStack(inStack);
            }
            else
                errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simHandleDynamics(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.handleDynamics");

    int retVal=-1; // means error
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* itScrObj=App::worldContainer->getScriptFromHandle(currentScriptID);
    if ( (itScrObj->getScriptType()==sim_scripttype_mainscript)||(itScrObj->getScriptType()==sim_scripttype_childscript) )
    {
        if (checkInputArguments(L,&errorString,lua_arg_number,0))
            retVal=simHandleDynamics_internal(luaToDouble(L,1));
    }
    else
        errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT_OR_CHILD_SCRIPT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simHandleProximitySensor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.handleProximitySensor");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double detPt[4];
        int detectedObjectID;
        double surfaceNormal[3];
        retVal=simHandleProximitySensor_internal(luaToInt(L,1),detPt,&detectedObjectID,surfaceNormal);
        if (retVal==1)
        {
            luaWrap_lua_pushinteger(L,retVal);
            luaWrap_lua_pushnumber(L,detPt[3]);
            pushDoubleTableOntoStack(L,3,detPt);
            luaWrap_lua_pushinteger(L,detectedObjectID);
            pushDoubleTableOntoStack(L,3,surfaceNormal);
            LUA_END(5);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    double dummy[3]={0.0,0.0,0.0};
    luaWrap_lua_pushnumber(L,0.0);
    pushDoubleTableOntoStack(L,3,dummy);
    luaWrap_lua_pushinteger(L,-1);
    pushDoubleTableOntoStack(L,3,dummy);
    LUA_END(5);
}

int _simReadProximitySensor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.readProximitySensor");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double detPt[4];
        int detectedObjectID;
        double surfaceNormal[3];
        retVal=simReadProximitySensor_internal(luaToInt(L,1),detPt,&detectedObjectID,surfaceNormal);
        if (retVal==1)
        {
            luaWrap_lua_pushinteger(L,retVal);
            luaWrap_lua_pushnumber(L,detPt[3]);
            pushDoubleTableOntoStack(L,3,detPt);
            luaWrap_lua_pushinteger(L,detectedObjectID);
            pushDoubleTableOntoStack(L,3,surfaceNormal);
            LUA_END(5);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    luaWrap_lua_pushnumber(L,0.0);
    double ft[3]={0.0,0.0,0.0};
    pushDoubleTableOntoStack(L,3,ft);
    luaWrap_lua_pushinteger(L,-1);
    pushDoubleTableOntoStack(L,3,ft);
    LUA_END(5);
}

int _simHandleVisionSensor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.handleVisionSensor");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double* auxVals=nullptr;
        int* auxValsCount=nullptr;
        retVal=simHandleVisionSensor_internal(luaToInt(L,1),&auxVals,&auxValsCount);
        if (retVal!=-1)
        {
            luaWrap_lua_pushinteger(L,retVal);
            int tableCount=0;
            if (auxValsCount!=nullptr)
            {
                tableCount=auxValsCount[0];
                int off=0;
                for (int i=0;i<tableCount;i++)
                {
                    pushDoubleTableOntoStack(L,auxValsCount[i+1],auxVals+off);
                    off+=auxValsCount[i+1];
                }
                delete[] auxValsCount;
                delete[] auxVals;
            }
            for (int i=tableCount;i<2;i++)
            {
                pushIntTableOntoStack(L,0,nullptr); // return at least 2 aux packets, even empty
                tableCount++;
            }
            LUA_END(1+tableCount);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simReadVisionSensor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.readVisionSensor");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double* auxVals=nullptr;
        int* auxValsCount=nullptr;
        retVal=simReadVisionSensor_internal(luaToInt(L,1),&auxVals,&auxValsCount);
        if (retVal!=-1)
        {
            luaWrap_lua_pushinteger(L,retVal);
            int tableCount=0;
            if (auxValsCount!=nullptr)
            {
                tableCount=auxValsCount[0];
                int off=0;
                for (int i=0;i<tableCount;i++)
                {
                    pushDoubleTableOntoStack(L,auxValsCount[i+1],auxVals+off);
                    off+=auxValsCount[i+1];
                }
                delete[] auxValsCount;
                delete[] auxVals;
            }
            for (int i=tableCount;i<2;i++)
            {
                pushIntTableOntoStack(L,0,nullptr); // return at least 2 aux packets, even empty
                tableCount++;
            }
            LUA_END(1+tableCount);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simResetProximitySensor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.resetProximitySensor");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simResetProximitySensor_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simResetVisionSensor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.resetVisionSensor");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simResetVisionSensor_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetVisionSensorImg(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getVisionSensorImg");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int sensorHandle=luaToInt(L,1);
        int options=0;
        double rgbaCutOff=0.0;
        int pos[2]={0,0};
        int size[2]={0,0};
        int res;
        res=checkOneGeneralInputArgument(L,2,lua_arg_integer,0,true,false,&errorString); // options
        if ((res==0)||(res==2))
        {
            if (res==2)
                options=luaToInt(L,2);
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString); // rgbaCutOff
            if ((res==0)||(res==2))
            {
                if (res==2)
                    rgbaCutOff=luaToDouble(L,3);
                res=checkOneGeneralInputArgument(L,4,lua_arg_integer,2,true,false,&errorString); // pos
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        getIntsFromTable(L,4,2,pos);
                    res=checkOneGeneralInputArgument(L,5,lua_arg_integer,2,true,false,&errorString); // size
                    if ((res==0)||(res==2))
                    {
                        if (res==2)
                            getIntsFromTable(L,5,2,size);
                        int resolution[2];
                        unsigned char* img=simGetVisionSensorImg_internal(sensorHandle,options,rgbaCutOff,pos,size,resolution);
                        if (img!=nullptr)
                        {
                            int s=3;
                            if ((options&1)!=0)
                                s=1; // greyscale
                            if ((options&2)!=0)
                                s++; //+ alpha channel
                            if (size[0]==0)
                                size[0]=resolution[0];
                            if (size[1]==0)
                                size[1]=resolution[1];
                            luaWrap_lua_pushlstring(L,(const char*)img,s*size[0]*size[1]);
                            delete[] ((char*)img);
                            pushIntTableOntoStack(L,2,resolution);
                            LUA_END(2);
                        }
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetVisionSensorImg(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setVisionSensorImg");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int sensorHandle=luaToInt(L,1);
        CSceneObject* it=App::currentWorld->sceneObjects->getObjectFromHandle(sensorHandle);
        if (it!=nullptr)
        { // Ok we have a valid object
            if (it->getObjectType()==sim_object_visionsensor_type)
            { // ok we have a valid vision sensor
                int resolution[2];
                ((CVisionSensor*)it)->getResolution(resolution);
                if (luaWrap_lua_isstring(L,2))
                {
                    size_t l;
                    unsigned char* img=(unsigned char*)luaWrap_lua_tolstring(L,2,&l);


                    int options=0;
                    int pos[2]={0,0};
                    int size[2]={resolution[0],resolution[1]};
                    int res;
                    res=checkOneGeneralInputArgument(L,3,lua_arg_integer,0,true,false,&errorString); // options
                    if ((res==0)||(res==2))
                    {
                        if (res==2)
                            options=luaToInt(L,3);
                        res=checkOneGeneralInputArgument(L,4,lua_arg_integer,2,true,false,&errorString); // pos
                        if ((res==0)||(res==2))
                        {
                            if (res==2)
                                getIntsFromTable(L,4,2,pos);
                            res=checkOneGeneralInputArgument(L,5,lua_arg_integer,2,true,false,&errorString); // size
                            if ((res==0)||(res==2))
                            {
                                if (res==2)
                                    getIntsFromTable(L,5,2,size);
                                int s=3;
                                if ((options&1)!=0)
                                    s=1; // greyscale
                                if ((options&2)!=0)
                                    s++; // + alpha channel
                                if (int(l)>=size[0]*size[1]*s)
                                    retVal=simSetVisionSensorImg_internal(sensorHandle,img,options,pos,size);
                                else
                                    errorString=SIM_ERROR_INCORRECT_BUFFER_SIZE;
                            }
                        }
                    }
                }
                else
                    errorString=SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG;
            }
            else
                errorString=SIM_ERROR_OBJECT_NOT_VISION_SENSOR;
        }
        else
            errorString=SIM_ERROR_OBJECT_INEXISTANT;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetVisionSensorDepth(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getVisionSensorDepth");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int sensorHandle=luaToInt(L,1);
        int options=0;
        int pos[2]={0,0};
        int size[2]={0,0};
        int res;
        res=checkOneGeneralInputArgument(L,2,lua_arg_integer,0,true,false,&errorString); // options
        if ((res==0)||(res==2))
        {
            if (res==2)
                options=luaToInt(L,2);
            res=checkOneGeneralInputArgument(L,3,lua_arg_integer,2,true,false,&errorString); // pos
            if ((res==0)||(res==2))
            {
                if (res==2)
                    getIntsFromTable(L,3,2,pos);
                res=checkOneGeneralInputArgument(L,4,lua_arg_integer,2,true,false,&errorString); // size
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        getIntsFromTable(L,4,2,size);
                    int resolution[2];
                    float* depth=simGetVisionSensorDepth_internal(sensorHandle,options,pos,size,resolution);
                    if (depth!=nullptr)
                    {
                        if (size[0]==0)
                            size[0]=resolution[0];
                        if (size[1]==0)
                            size[1]=resolution[1];
                        luaWrap_lua_pushlstring(L,(const char*)depth,size[0]*size[1]*sizeof(float));
                        delete[] ((char*)depth);
                        pushIntTableOntoStack(L,2,resolution);
                        LUA_END(2);
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simCheckProximitySensor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.checkProximitySensor");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        CProxSensor* it=App::currentWorld->sceneObjects->getProximitySensorFromHandle(handle);
        if (it!=nullptr)
        {
            double detPt[4];
            double n[3];
            int detectedObjectHandle;
            int options=0;
            if (it->getFrontFaceDetection())
                options=options|1;
            if (it->getBackFaceDetection())
                options=options|2;
            if (!it->getClosestObjectMode())
                options=options|4;
            if (it->getNormalCheck())
                options=options|8;
            retVal=simCheckProximitySensorEx_internal(handle,luaToInt(L,2),options,FLOAT_MAX,it->getAllowedNormal(),detPt,&detectedObjectHandle,n);
            if (retVal==1)
            {
                luaWrap_lua_pushinteger(L,retVal);
                luaWrap_lua_pushnumber(L,detPt[3]);
                pushDoubleTableOntoStack(L,3,detPt);
                luaWrap_lua_pushinteger(L,detectedObjectHandle);
                pushDoubleTableOntoStack(L,3,n);
                LUA_END(5);
            }
        }
        else
            errorString="invalid proximity sensor handle.";
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    double dummy[3]={0.0,0.0,0.0};
    luaWrap_lua_pushnumber(L,0.0);
    pushDoubleTableOntoStack(L,3,dummy);
    luaWrap_lua_pushinteger(L,-1);
    pushDoubleTableOntoStack(L,3,dummy);
    LUA_END(5);
}

int _simCheckProximitySensorEx(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.checkProximitySensorEx");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        double detPt[4];
        int detObj;
        double normVect[3];
        retVal=simCheckProximitySensorEx_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3),luaToDouble(L,4),luaToDouble(L,5),detPt,&detObj,normVect);
        if (retVal==1)
        {
            luaWrap_lua_pushinteger(L,retVal);
            luaWrap_lua_pushnumber(L,detPt[3]);
            pushDoubleTableOntoStack(L,3,detPt);
            luaWrap_lua_pushinteger(L,detObj);
            pushDoubleTableOntoStack(L,3,normVect);
            LUA_END(5);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    double dummy[3]={0.0,0.0,0.0};
    luaWrap_lua_pushnumber(L,0.0);
    pushDoubleTableOntoStack(L,3,dummy);
    luaWrap_lua_pushinteger(L,-1);
    pushDoubleTableOntoStack(L,3,dummy);
    LUA_END(5);
}

int _simCheckProximitySensorEx2(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.checkProximitySensorEx2");

    int retVal=-1; // means error
    if (checkOneGeneralInputArgument(L,1,lua_arg_number,0,false,false,&errorString)==2)
    { // first argument (sensor handle)
        int sensorID=luaToInt(L,1);
        if (checkOneGeneralInputArgument(L,3,lua_arg_number,0,false,false,&errorString)==2)
        { // third argument (item type)
            int itemType=luaToInt(L,3);
            if (checkOneGeneralInputArgument(L,4,lua_arg_number,0,false,false,&errorString)==2)
            { // forth argument (item count)
                int itemCount=luaToInt(L,4);
                int requiredValues=itemCount*3*(itemType+1);
                if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,requiredValues,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
                {
                    int mode=luaToInt(L,5);
                    double threshold=luaToDouble(L,6);
                    double maxAngle=luaToDouble(L,7);
                    double* vertices=new double[requiredValues];
                    getDoublesFromTable(L,2,requiredValues,vertices);

                    double detPt[4];
                    double normVect[3];
                    retVal=simCheckProximitySensorEx2_internal(sensorID,vertices,itemType,itemCount,mode,threshold,maxAngle,detPt,normVect);
                    delete[] vertices;
                    if (retVal==1)
                    {
                        luaWrap_lua_pushinteger(L,retVal);
                        luaWrap_lua_pushnumber(L,detPt[3]);
                        pushDoubleTableOntoStack(L,3,detPt);
                        pushDoubleTableOntoStack(L,3,normVect);
                        LUA_END(4);
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    double dummy[3]={0.0,0.0,0.0};
    luaWrap_lua_pushnumber(L,0.0);
    pushDoubleTableOntoStack(L,3,dummy);
    pushDoubleTableOntoStack(L,3,dummy);
    LUA_END(4);
}

int _simCheckVisionSensor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.checkVisionSensor");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        double* auxVals=nullptr;
        int* auxValsCount=nullptr;
        retVal=simCheckVisionSensor_internal(luaToInt(L,1),luaToInt(L,2),&auxVals,&auxValsCount);
        if (retVal!=-1)
        {
            luaWrap_lua_pushinteger(L,retVal);
            int tableCount=0;
            if (auxValsCount!=nullptr)
            {
                tableCount=auxValsCount[0];
                int off=0;
                for (int i=0;i<tableCount;i++)
                {
                    pushDoubleTableOntoStack(L,auxValsCount[i+1],auxVals+off);
                    off+=auxValsCount[i+1];
                }
                delete[] auxValsCount;
                delete[] auxVals;
            }
            for (int i=tableCount;i<2;i++)
            {
                pushIntTableOntoStack(L,0,nullptr); // return at least 2 aux packets, even empty
                tableCount++;
            }
            LUA_END(1+tableCount);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCheckVisionSensorEx(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.checkVisionSensorEx");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_bool,0))
    {
        bool returnImage=luaToBool(L,3);
        int arg1=luaToInt(L,1);
        int handleFlags=arg1&0xff00000;
        int sensHandle=arg1&0xfffff;
        int res[2];
        simGetVisionSensorRes_internal(sensHandle,res);
        float* buffer=simCheckVisionSensorEx_internal(luaToInt(L,1),luaToInt(L,2),returnImage);
        if (buffer!=nullptr)
        {
            if ((handleFlags&sim_handleflag_codedstring)!=0)
            {
                if (returnImage)
                {
                    unsigned char* buff2=new unsigned char[res[0]*res[1]*3];
                    for (size_t i=0;i<res[0]*res[1]*3;i++)
                        buff2[i]=(unsigned char)(buffer[i]*255.1);
                    luaWrap_lua_pushlstring(L,(const char*)buff2,res[0]*res[1]*3);
                    delete[] buff2;
                }
                else
                    luaWrap_lua_pushlstring(L,(const char*)buffer,res[0]*res[1]*sizeof(float));
            }
            else
            {
                if (returnImage)
                    pushFloatTableOntoStack(L,res[0]*res[1]*3,buffer);
                else
                    pushFloatTableOntoStack(L,res[0]*res[1],buffer);
            }
            simReleaseBuffer_internal(buffer);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _sim_getObject(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim._getObject");

    int retVal=-1; // means error

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        int index=-1;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if (res>=0)
        {
            if (res==2)
                index=luaToInt(L,2);
            int proxyForSearch=-1;
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if (res>=0)
            {
                if (res==2)
                    proxyForSearch=luaToInt(L,3);

                int options=0;
                res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
                if (res>=0)
                {
                    if (res==2)
                        options=luaToInt(L,4);
                    std::string name(luaWrap_lua_tostring(L,1));
                    setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
                    retVal=simGetObject_internal(name.c_str(),index,proxyForSearch,options);
                    setCurrentScriptInfo_cSide(-1,-1);
                }
            }
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectUid(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectUid");

    long long int retVal=-1; // means error

    if (checkInputArguments(L,&errorString,lua_arg_integer,0))
        retVal=simGetObjectUid_internal(luaToInt(L,1));
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _sim_getObjectFromUid(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim._getObjectFromUid");

    int retVal=-1; // means error

    if (checkInputArguments(L,&errorString,lua_arg_integer,0))
    {
        long long int uid=luaWrap_lua_tointeger(L,1);
        int options=0;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_integer,0,true,false,&errorString);
        if (res>=0)
        {
            if (res==2)
                options=luaToInt(L,2);
            retVal=simGetObjectFromUid_internal(uid,options);
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetScript(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getScript");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_integer,0))
    {
        int scriptType=luaToInt(L,1);
        if (scriptType==sim_handle_self)
            retVal=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        else
        {
            int objectHandle=-1;
            std::string scriptName;
            if (scriptType==sim_scripttype_addonscript)
            {
                if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_integer,0,lua_arg_string,0))
                    scriptName=luaWrap_lua_tostring(L,3);
            }
            if ( (scriptType==sim_scripttype_childscript)||(scriptType==sim_scripttype_customizationscript) )
            {
                if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_integer,0))
                    objectHandle=luaToInt(L,2);
            }
            if ( ( (scriptName.size()>0)||(objectHandle>=0) )||((scriptType==sim_scripttype_mainscript)||(scriptType==sim_scripttype_sandboxscript)) )
            {
                setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
                retVal=simGetScriptHandleEx_internal(scriptType,objectHandle,scriptName.c_str());
                setCurrentScriptInfo_cSide(-1,-1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAddScript(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.addScript");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int scriptType=luaToInt(L,1);
        retVal=simAddScript_internal(scriptType);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAssociateScriptWithObject(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.associateScriptWithObject");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int scriptHandle=luaToInt(L,1);
        int objectHandle=luaToInt(L,2);
        retVal=simAssociateScriptWithObject_internal(scriptHandle,objectHandle);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simRemoveScript(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.removeScript");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        if (handle==sim_handle_self)
            handle=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        if (sim_handle_all!=handle)
            retVal=simRemoveScript_internal(handle);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectPosition(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectPosition");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        double coord[3];
        if (simGetObjectPosition_internal(luaToInt(L,1),luaToInt(L,2),coord)==1)
        {
            pushDoubleTableOntoStack(L,3,coord);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetObjectOrientation(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectOrientation");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        double coord[3];
        if (simGetObjectOrientation_internal(luaToInt(L,1),luaToInt(L,2),coord)==1)
        {
            pushDoubleTableOntoStack(L,3,coord);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectPosition(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectPosition");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,3))
    {
        double coord[3];
        getDoublesFromTable(L,3,3,coord);
        retVal=simSetObjectPosition_internal(luaToInt(L,1),luaToInt(L,2),coord);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetObjectOrientation(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectOrientation");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,3))
    {
        double coord[3];
        getDoublesFromTable(L,3,3,coord);
        retVal=simSetObjectOrientation_internal(luaToInt(L,1),luaToInt(L,2),coord);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetJointPosition(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getJointPosition");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double jointVal[1];
        if (simGetJointPosition_internal(luaToInt(L,1),jointVal)!=-1)
        {
            luaWrap_lua_pushnumber(L,jointVal[0]);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetJointPosition(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setJointPosition");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetJointPosition_internal(luaToInt(L,1),luaToDouble(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetJointTargetPosition(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setJointTargetPosition");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int res=checkOneGeneralInputArgument(L,3,lua_arg_number,-1,true,false,&errorString);
        if (res>=0)
        {
            int h=luaToInt(L,1);
            if (res==2)
            {
                CJoint* joint=App::currentWorld->sceneObjects->getJointFromHandle(h);
                if (joint!=nullptr)
                {
                    if ( (joint->getJointMode()==sim_jointmode_kinematic)||(joint->getJointMode()==sim_jointmode_dynamic) )
                    {
                        double maxVelAccelJerk[3];
                        joint->getMaxVelAccelJerk(maxVelAccelJerk);
                        int cnt=1; // only max. vel. with primitive, built-in controller
                        if ( (joint->getJointMode()==sim_jointmode_kinematic)||(joint->getDynPosCtrlType()==1) )
                            cnt=3; // Motion profile (max. vel, max. accel and max. jerk)
                        getDoublesFromTable(L,3,std::min<size_t>(luaWrap_lua_rawlen(L,3),cnt),maxVelAccelJerk);
                        joint->setMaxVelAccelJerk(maxVelAccelJerk);
                    }
                }
            }
            retVal=simSetJointTargetPosition_internal(h,luaToDouble(L,2));
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetJointTargetPosition(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getJointTargetPosition");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double targetPos;
        if (simGetJointTargetPosition_internal(luaToInt(L,1),&targetPos)!=-1)
        {
            luaWrap_lua_pushnumber(L,targetPos);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushnumber(L,0.0);
    LUA_END(1);
}

int _simGetJointTargetForce(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getJointTargetForce");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double jointF[1];
        if (simGetJointTargetForce_internal(luaToInt(L,1),jointF)>0)
        {
            luaWrap_lua_pushnumber(L,jointF[0]);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetJointTargetForce(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setJointTargetForce");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        bool signedValue=true;
        int res=checkOneGeneralInputArgument(L,3,lua_arg_bool,0,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
                signedValue=luaToBool(L,3);
            retVal=simSetJointTargetForce_internal(luaToInt(L,1),luaToDouble(L,2),signedValue);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetJointTargetVelocity(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setJointTargetVelocity");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int res=checkOneGeneralInputArgument(L,3,lua_arg_number,-1,true,false,&errorString);
        if (res>=0)
        {
            int h=luaToInt(L,1);
            if (res==2)
            {
                CJoint* joint=App::currentWorld->sceneObjects->getJointFromHandle(h);
                if (joint!=nullptr)
                {
                    if ( (joint->getJointMode()==sim_jointmode_kinematic)||(joint->getJointMode()==sim_jointmode_dynamic) )
                    {
                        if ( (joint->getJointMode()==sim_jointmode_kinematic)||(joint->getDynPosCtrlType()==1) )
                        {
                            double maxVelAccelJerk[3];
                            joint->getMaxVelAccelJerk(maxVelAccelJerk);
                            getDoublesFromTable(L,3,std::min<size_t>(luaWrap_lua_rawlen(L,3),2),maxVelAccelJerk+1);
                            joint->setMaxVelAccelJerk(maxVelAccelJerk);
                        }
                    }
                    res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
                    if (res==2)
                    {
                        double initVel=luaToDouble(L,4);
                        joint->setKinematicMotionType(2,true,initVel);
                    }
                }
            }
            if (errorString.size()==0)
                retVal=simSetJointTargetVelocity_internal(h,luaToDouble(L,2));
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetJointTargetVelocity(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getJointTargetVelocity");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double targetVel;
        if (simGetJointTargetVelocity_internal(luaToInt(L,1),&targetVel)!=-1)
        {
            luaWrap_lua_pushnumber(L,targetVel);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simRefreshDialogs(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.refreshDialogs");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simRefreshDialogs_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetModuleName(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getModuleName");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        unsigned char version;
        char* name=simGetModuleName_internal(luaToInt(L,1),&version);
        if (name!=nullptr)
        {
            luaWrap_lua_pushstring(L,name);
            simReleaseBuffer_internal(name);
            luaWrap_lua_pushinteger(L,(int)version);
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetSimulationTime(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getSimulationTime");

    double theTime=simGetSimulationTime_internal();
    if (theTime>=0.0)
    {
        luaWrap_lua_pushnumber(L,theTime);
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,-1);
    LUA_END(1);
}

int _simGetSimulationState(luaWrap_lua_State* L)
{
    // In case we copy-paste a script during execution, the new script doesn't get the sim_simulation_starting message,
    // but that is ok!!! sim_simulation_starting is only for a simulation start. For a first run in a script, use some
    // random variable and check whether it is != from nil!! or use simGetScriptExecutionCount
    TRACE_LUA_API;
    LUA_START("sim.getSimulationState");

    int retVal=simGetSimulationState_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetSystemTime(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getSystemTime");

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED();
    luaWrap_lua_pushnumber(L,VDateTime::getTime());
    LUA_END(1);
}

int _simCheckCollision(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.checkCollision");
    int retVal=0;
    int collidingIds[2]={-1,-1};
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int entity1Handle=luaToInt(L,1);
        int entity2Handle=luaToInt(L,2);
        if (doesEntityExist(&errorString,entity1Handle))
        {
            if ( (entity2Handle==sim_handle_all)||doesEntityExist(&errorString,entity2Handle) )
            {
                if (entity2Handle==sim_handle_all)
                    entity2Handle=-1;

                if (App::currentWorld->mainSettings->collisionDetectionEnabled)
                {
                    if (CCollisionRoutine::doEntitiesCollide(entity1Handle,entity2Handle,nullptr,true,true,collidingIds))
                        retVal=1;
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    pushIntTableOntoStack(L,2,collidingIds);
    LUA_END(2);
}

int _simCheckCollisionEx(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.checkCollisionEx");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        double* intersections[1];
        retVal=simCheckCollisionEx_internal(luaToInt(L,1),luaToInt(L,2),intersections);
        if (retVal>0)
        {
            luaWrap_lua_pushinteger(L,retVal);
            pushDoubleTableOntoStack(L,retVal*6,(*intersections));
            simReleaseBuffer_internal((char*)(*intersections));
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    pushIntTableOntoStack(L,0,nullptr); // empty table
    LUA_END(2);
}

int _simCheckDistance(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.checkDistance");
    int retVal=-1;
    double distanceData[7]={0.0,0.0,0.0,0.0,0.0,0.0,0.0};
    int tb[2]={-1,-1};
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int entity1Handle=luaToInt(L,1);
        int entity2Handle=luaToInt(L,2);
        int res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,true,&errorString);
        if (res>=0)
        {
            double threshold=-1.0;
            if (res==2)
                threshold=luaToDouble(L,3);
            if (doesEntityExist(&errorString,entity1Handle))
            {
                if ( (entity2Handle==sim_handle_all)||doesEntityExist(&errorString,entity2Handle) )
                {
                    if (entity2Handle==sim_handle_all)
                        entity2Handle=-1;
                    retVal=0;
                    if (App::currentWorld->mainSettings->distanceCalculationEnabled)
                    {
                        int buffer[4];
                        App::currentWorld->cacheData->getCacheDataDist(entity1Handle,entity2Handle,buffer);
                        if (threshold<=0.0)
                            threshold=FLOAT_MAX;
                        bool result=CDistanceRoutine::getDistanceBetweenEntitiesIfSmaller(entity1Handle,entity2Handle,threshold,distanceData,buffer,buffer+2,true,true);
                        App::currentWorld->cacheData->setCacheDataDist(entity1Handle,entity2Handle,buffer);
                        if (result)
                        {
                            retVal=1;
                            tb[0]=buffer[0];
                            tb[1]=buffer[2];
                        }
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    pushDoubleTableOntoStack(L,7,distanceData);
    pushIntTableOntoStack(L,2,tb);
    LUA_END(3);
}

int _simGetSimulationTimeStep(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getSimulationTimeStep");

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushnumber(L,simGetSimulationTimeStep_internal());
    LUA_END(1);
}

int _simGetSimulatorMessage(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getSimulatorMessage");
    int auxVals[4]={0,0,0,0};
    double aux2Vals[8]={0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0};
    int aux2Cnt=0;
    CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
    int commandID=it->extractCommandFromOutsideCommandQueue(auxVals,aux2Vals,aux2Cnt);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,commandID);
    pushIntTableOntoStack(L,4,auxVals);
    pushDoubleTableOntoStack(L,aux2Cnt,aux2Vals);
    LUA_END(3);
}

int _simResetGraph(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.resetGraph");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simResetGraph_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simHandleGraph(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.handleGraph");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simHandleGraph_internal(luaToInt(L,1),luaToDouble(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAddGraphStream(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.addGraphStream");

    if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_string,0,lua_arg_string,0))
    {
        int graphHandle=luaToInt(L,1);
        std::string streamName(luaWrap_lua_tostring(L,2));
        std::string unitStr(luaWrap_lua_tostring(L,3));
        if (streamName.size()!=0)
        {
            int options=0;
            int res=checkOneGeneralInputArgument(L,4,lua_arg_integer,0,true,false,&errorString);
            if (res==2)
                options=luaToInt(L,4);
            if ( (res==0)||(res==2) )
            {
                float col[3]={1.0,0.0,0.0};
                res=checkOneGeneralInputArgument(L,5,lua_arg_number,3,true,false,&errorString);
                if (res==2)
                    getFloatsFromTable(L,5,3,col);
                if ( (res==0)||(res==2) )
                {
                    double cyclicRange=piValue;
                    res=checkOneGeneralInputArgument(L,6,lua_arg_number,0,true,false,&errorString);
                    if (res==2)
                        cyclicRange=luaToDouble(L,6);
                    if ( (res==0)||(res==2) )
                    {
                        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
                        int retVal=simAddGraphStream_internal(graphHandle,streamName.c_str(),unitStr.c_str(),options,col,cyclicRange);
                        setCurrentScriptInfo_cSide(-1,-1);
                        luaWrap_lua_pushinteger(L,retVal);
                        LUA_END(1);
                    }
                }
            }
        }
        else
            errorString=SIM_ERROR_EMPTY_STRING_NOT_ALLOWED;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simDestroyGraphCurve(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.destroyGraphCurve");

    if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_integer,0))
        simDestroyGraphCurve_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetGraphStreamTransformation(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setGraphStreamTransformation");

    if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_integer,0,lua_arg_integer,0))
    {
        double mult=1.0;
        int res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
        if (res==2)
            mult=luaToDouble(L,4);
        if ( (res==0)||(res==2) )
        {
            double off=0.0;
            res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,true,false,&errorString);
            if (res==2)
                off=luaToDouble(L,5);
            if ( (res==0)||(res==2) )
            {
                int movAvgP=1;
                res=checkOneGeneralInputArgument(L,6,lua_arg_integer,0,true,false,&errorString);
                if (res==2)
                    movAvgP=luaToInt(L,6);
                if ( (res==0)||(res==2) )
                    simSetGraphStreamTransformation_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3),mult,off,movAvgP);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simDuplicateGraphCurveToStatic(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.duplicateGraphCurveToStatic");
    if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_integer,0))
    {
        std::string name;
        const char* str=nullptr;
        int res=checkOneGeneralInputArgument(L,3,lua_arg_string,0,true,false,&errorString);
        if (res==2)
        {
            name=luaWrap_lua_tostring(L,3);
            if (name.length()>0)
                str=&name[0];
        }
        if ( (res==0)||(res==2) )
        {
            setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
            simDuplicateGraphCurveToStatic_internal(luaToInt(L,1),luaToInt(L,2),str);
            setCurrentScriptInfo_cSide(-1,-1);
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simAddGraphCurve(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.addGraphCurve");
    if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_string,0,lua_arg_integer,0,lua_arg_integer,2,lua_arg_number,2))
    {
        int graphHandle=luaToInt(L,1);
        std::string curveName(luaWrap_lua_tostring(L,2));
        int dim=luaToInt(L,3);
        int res=checkOneGeneralInputArgument(L,4,lua_arg_integer,dim,false,false,&errorString);
        if (res==2)
        {
            int streamIds[3];
            getIntsFromTable(L,4,dim,streamIds);
            int res=checkOneGeneralInputArgument(L,5,lua_arg_number,dim,false,false,&errorString);
            if (res==2)
            {
                double defaultVals[3];
                getDoublesFromTable(L,5,dim,defaultVals);
                if (curveName.size()!=0)
                {
                    std::string unitStr;
                    char* _unitStr=nullptr;
                    res=checkOneGeneralInputArgument(L,6,lua_arg_string,0,true,false,&errorString);
                    if (res==2)
                    {
                        unitStr=luaWrap_lua_tostring(L,6);
                        if (unitStr.size()>0)
                            _unitStr=&unitStr[0];
                    }
                    if ( (res==0)||(res==2) )
                    {
                        int options=0;
                        int res=checkOneGeneralInputArgument(L,7,lua_arg_integer,0,true,false,&errorString);
                        if (res==2)
                            options=luaToInt(L,7);
                        if ( (res==0)||(res==2) )
                        {
                            float col[3]={1.0,1.0,0.0};
                            res=checkOneGeneralInputArgument(L,8,lua_arg_number,3,true,false,&errorString);
                            if (res==2)
                                getFloatsFromTable(L,8,3,col);
                            if ( (res==0)||(res==2) )
                            {
                                int curveWidth=2;
                                res=checkOneGeneralInputArgument(L,9,lua_arg_integer,0,true,false,&errorString);
                                if (res==2)
                                    curveWidth=luaToInt(L,9);
                                if ( (res==0)||(res==2) )
                                {
                                    setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
                                    int retVal=simAddGraphCurve_internal(graphHandle,curveName.c_str(),dim,streamIds,defaultVals,_unitStr,options,col,curveWidth);
                                    setCurrentScriptInfo_cSide(-1,-1);
                                    luaWrap_lua_pushinteger(L,retVal);
                                    LUA_END(1);
                                }
                            }
                        }
                    }
                }
                else
                    errorString=SIM_ERROR_EMPTY_STRING_NOT_ALLOWED;
            }
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetGraphStreamValue(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setGraphStreamValue");

    if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_integer,0,lua_arg_number,0))
        simSetGraphStreamValue_internal(luaToInt(L,1),luaToInt(L,2),luaToDouble(L,3));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simAddLog(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.addLog");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int v=luaToInt(L,1);
        int res=checkOneGeneralInputArgument(L,2,lua_arg_string,0,false,true,&errorString);
        if (res>0)
        {
            if (res==1)
                App::clearStatusbar();
            else
            {
                std::string msg(luaWrap_lua_tostring(L,2));
                CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
                std::string nm("???");
                if (it!=nullptr)
                    nm=it->getShortDescriptiveName();
                App::logScriptMsg(nm.c_str(),v,msg.c_str());
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simStopSimulation(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.stopSimulation");

    int retVal=-1;// error
    retVal=simStopSimulation_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simPauseSimulation(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.pauseSimulation");

    int retVal=-1;// error
    retVal=simPauseSimulation_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simStartSimulation(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.startSimulation");

    int retVal=-1;// error
    retVal=simStartSimulation_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectMatrix(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectMatrix");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        double arr[12];
        if (simGetObjectMatrix_internal(luaToInt(L,1),luaToInt(L,2),arr)==1)
        {
            pushDoubleTableOntoStack(L,12,arr); // Success
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectMatrix(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectMatrix");

    int retVal=-1; // error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,12))
    {
        double arr[12];
        getDoublesFromTable(L,3,12,arr);
        retVal=simSetObjectMatrix_internal(luaToInt(L,1),luaToInt(L,2),arr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectPose(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectPose");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        double arr[7];
        if (simGetObjectPose_internal(luaToInt(L,1),luaToInt(L,2),arr)==1)
        {
            pushDoubleTableOntoStack(L,7,arr); // Success
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectPose(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectPose");

    int retVal=-1; // error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,7))
    {
        double arr[7];
        getDoublesFromTable(L,3,7,arr);
        retVal=simSetObjectPose_internal(luaToInt(L,1),luaToInt(L,2),arr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectChildPose(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectChildPose");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double arr[7];
        if (simGetObjectChildPose_internal(luaToInt(L,1),arr)==1)
        {
            pushDoubleTableOntoStack(L,7,arr); // Success
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectChildPose(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectChildPose");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,7))
    {
        double arr[7];
        getDoublesFromTable(L,2,7,arr);
        simSetObjectChildPose_internal(luaToInt(L,1),arr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simBuildIdentityMatrix(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.buildIdentityMatrix");

    double arr[12];
    simBuildIdentityMatrix_internal(arr);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    pushDoubleTableOntoStack(L,12,arr);
    LUA_END(1);
}

int _simBuildMatrix(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.buildMatrix");

    if (checkInputArguments(L,&errorString,lua_arg_number,3,lua_arg_number,3))
    {
        double arr[12];
        double pos[3];
        double euler[3];
        getDoublesFromTable(L,1,3,pos);
        getDoublesFromTable(L,2,3,euler);
        if (simBuildMatrix_internal(pos,euler,arr)==1)
        {
            pushDoubleTableOntoStack(L,12,arr);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simBuildPose(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.buildPose");

    if (checkInputArguments(L,&errorString,lua_arg_number,3,lua_arg_number,3))
    {
        double tr[7];
        double pos[3];
        double axis1[3];
        double axis2[3];
        int mode=0;
        getDoublesFromTable(L,1,3,pos);
        getDoublesFromTable(L,2,3,axis1);
        int res=checkOneGeneralInputArgument(L,3,lua_arg_integer,0,true,false,&errorString);
        if (res>=0)
        {
            if (res==2)
                mode=luaToInt(L,3);
            if (mode==0)
            {
                if (simBuildPose_internal(pos,axis1,tr)==1)
                {
                    pushDoubleTableOntoStack(L,7,tr);
                    LUA_END(1);
                }
            }
            else
            {
                res=checkOneGeneralInputArgument(L,4,lua_arg_number,3,mode<4,false,&errorString);
                if (res>=0)
                {
                    if (res==2)
                        getDoublesFromTable(L,4,3,axis2);
                    C3X3Matrix m;
                    C3Vector a1(axis1);
                    a1.normalize();
                    if (mode<4)
                    {
                        int i1=mode-1;
                        int i2=i1+1;
                        if (i2>2) i2=0;
                        int i3=i2+1;
                        if (i3>2) i3=0;
                        C3Vector a2;
                        if (a1(2)<0.8)
                            a2.setData(0.0,0.0,1.0);
                        else
                            a2.setData(1.0,0.0,0.0);
                        m.axis[i1]=a1;
                        m.axis[i3]=(a1^a2).getNormalized();
                        m.axis[i2]=m.axis[i3]^a1;
                    }
                    else
                    {
                        int i1=mode-4;
                        if (mode>=7)
                            i1=mode-7;
                        int i2=i1+1;
                        if (i2>2) i2=0;
                        int i3=i2+1;
                        if (i3>2) i3=0;
                        C3Vector a2(axis2);
                        a2.normalize();
                        m.axis[i1]=a1;
                        if (mode<7)
                        {
                            m.axis[i3]=(a1^a2).getNormalized();
                            m.axis[i2]=m.axis[i3]^a1;
                        }
                        else
                        {
                            m.axis[i2]=(a2^a1).getNormalized();
                            m.axis[i3]=a1^m.axis[i2];
                        }
                    }
                    tr[0]=pos[0];
                    tr[1]=pos[1];
                    tr[2]=pos[2];
                    m.getQuaternion().getData(tr+3,true);
                    pushDoubleTableOntoStack(L,7,tr);
                    LUA_END(1);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetEulerAnglesFromMatrix(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getEulerAnglesFromMatrix");

    if (checkInputArguments(L,&errorString,lua_arg_number,12))
    {
        double arr[12];
        double euler[3];
        getDoublesFromTable(L,1,12,arr);
        if (simGetEulerAnglesFromMatrix_internal(arr,euler)==1)
        {
            pushDoubleTableOntoStack(L,3,euler);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simInvertMatrix(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.invertMatrix");
    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,12))
    {
        double arr[12];
        getDoublesFromTable(L,1,12,arr);
        retVal=simInvertMatrix_internal(arr);
        for (int i=0;i<12;i++)
        {
            luaWrap_lua_pushnumber(L,arr[i]);
            luaWrap_lua_rawseti(L,1,i+1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simInvertPose(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.invertPose");
    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,7))
    {
        double arr[7];
        getDoublesFromTable(L,1,7,arr);
        retVal=simInvertPose_internal(arr);
        for (int i=0;i<7;i++)
        {
            luaWrap_lua_pushnumber(L,arr[i]);
            luaWrap_lua_rawseti(L,1,i+1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simMultiplyMatrices(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.multiplyMatrices");

    if (checkInputArguments(L,&errorString,lua_arg_number,12,lua_arg_number,12))
    {
        double inM0[12];
        double inM1[12];
        double outM[12];
        getDoublesFromTable(L,1,12,inM0);
        getDoublesFromTable(L,2,12,inM1);
        if (simMultiplyMatrices_internal(inM0,inM1,outM)!=-1)
        {
            pushDoubleTableOntoStack(L,12,outM);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simMultiplyPoses(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.multiplyPoses");

    if (checkInputArguments(L,&errorString,lua_arg_number,7,lua_arg_number,7))
    {
        double inP0[7];
        double inP1[7];
        double outP[7];
        getDoublesFromTable(L,1,7,inP0);
        getDoublesFromTable(L,2,7,inP1);
        if (simMultiplyPoses_internal(inP0,inP1,outP)!=-1)
        {
            pushDoubleTableOntoStack(L,7,outP);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simInterpolateMatrices(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.interpolateMatrices");

    if (checkInputArguments(L,&errorString,lua_arg_number,12,lua_arg_number,12,lua_arg_number,0))
    {
        double inM0[12];
        double inM1[12];
        double outM[12];
        getDoublesFromTable(L,1,12,inM0);
        getDoublesFromTable(L,2,12,inM1);
        if (simInterpolateMatrices_internal(inM0,inM1,luaToDouble(L,3),outM)!=-1)
        {
            pushDoubleTableOntoStack(L,12,outM);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simInterpolatePoses(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.interpolatePoses");

    if (checkInputArguments(L,&errorString,lua_arg_number,7,lua_arg_number,7,lua_arg_number,0))
    {
        double inP0[7];
        double inP1[7];
        double outP[7];
        getDoublesFromTable(L,1,7,inP0);
        getDoublesFromTable(L,2,7,inP1);
        if (simInterpolatePoses_internal(inP0,inP1,luaToDouble(L,3),outP)!=-1)
        {
            pushDoubleTableOntoStack(L,7,outP);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simPoseToMatrix(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.poseToMatrix");

    if (checkInputArguments(L,&errorString,lua_arg_number,7))
    {
        double inP[7];
        double outM[12];
        getDoublesFromTable(L,1,7,inP);
        if (simPoseToMatrix_internal(inP,outM)!=-1)
        {
            pushDoubleTableOntoStack(L,12,outM);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simMatrixToPose(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.matrixToPose");

    if (checkInputArguments(L,&errorString,lua_arg_number,12))
    {
        double inM[12];
        double outP[7];
        getDoublesFromTable(L,1,12,inM);
        if (simMatrixToPose_internal(inM,outP)!=-1)
        {
            pushDoubleTableOntoStack(L,7,outP);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simMultiplyVector(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.multiplyVector");

    if (checkInputArguments(L,&errorString,lua_arg_number,7,lua_arg_number,3))
    {
        double matr[12];
        std::vector<double> vect;
        size_t cnt=luaWrap_lua_rawlen(L,2)/3;
        vect.resize(cnt*3);
        getDoublesFromTable(L,2,cnt*3,&vect[0]);

        if (luaWrap_lua_rawlen(L,1)>=12)
        { // we have a matrix
            getDoublesFromTable(L,1,12,matr);
            C4X4Matrix m;
            m.setData(matr);
            for (size_t i=0;i<cnt;i++)
            {
                C3Vector v(&vect[3*i]);
                (m*v).getData(&vect[3*i]);
            }
        }
        else
        { // we have a pose
            getDoublesFromTable(L,1,7,matr);
            C7Vector tr;
            tr.X.setData(matr);
            tr.Q.setData(matr+3,true);
            for (size_t i=0;i<cnt;i++)
            {
                C3Vector v(&vect[3*i]);
                (tr*v).getData(&vect[3*i]);
            }
        }

        pushDoubleTableOntoStack(L,3*cnt,&vect[0]);
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetObjectParent(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectParent");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetObjectParent_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectChild(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectChild");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simGetObjectChild_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetObjectParent(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectParent");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_integer,0))
    {
        bool keepInPlace=true;
        int res=checkOneGeneralInputArgument(L,3,lua_arg_bool,0,true,false,&errorString);
        if (res>=0)
        {
            if (res==2)
                keepInPlace=luaToBool(L,3);
            retVal=simSetObjectParent_internal(luaToInt(L,1),luaToInt(L,2),keepInPlace);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectType(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectType");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetObjectType_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetJointType(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getJointType");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetJointType_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetBoolParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setBoolParam");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_bool,0))
        retVal=simSetBoolParam_internal(luaToInt(L,1),luaToBool(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetBoolParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getBoolParam");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int retVal=simGetBoolParam_internal(luaToInt(L,1));
        if (retVal!=-1)
        {
            luaWrap_lua_pushboolean(L,retVal!=0);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetInt32Param(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setInt32Param");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int paramIndex=luaToInt(L,1);
        int v=luaToInt(L,2);
        if (paramIndex==sim_intparam_error_report_mode)
        { // for backward compatibility (2020)
            CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
            if (it!=nullptr)
            {
                bool r=true; // default
                if ( (v&sim_api_error_report)==0 )
                    r=false;
                it->setRaiseErrors_backCompatibility(r);
                retVal=1;
            }
        }
        else
            retVal=simSetInt32Param_internal(paramIndex,v);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetInt32Param(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getInt32Param");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int paramIndex=luaToInt(L,1);
        if (paramIndex==sim_intparam_error_report_mode)
        { // for backward compatibility (2020)
            CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
            if (it!=nullptr)
            {
                int v=1; // default
                if (!it->getRaiseErrors_backCompatibility())
                    v=0;
                luaWrap_lua_pushinteger(L,v);
                LUA_END(1);
            }
        }
        else
        {
            int v;
            int retVal=simGetInt32Param_internal(paramIndex,&v);
            if (retVal!=-1)
            {
                luaWrap_lua_pushinteger(L,v);
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetFloatParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setFloatParam");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetFloatParam_internal(luaToInt(L,1),luaToDouble(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetFloatParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getFloatParam");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double v;
        int retVal=simGetFloatParam_internal(luaToInt(L,1),&v);
        if (retVal!=-1)
        {
            luaWrap_lua_pushnumber(L,v);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(1);
}

int _simSetStringParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setStringParam");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
        retVal=simSetStringParam_internal(luaToInt(L,1),luaWrap_lua_tostring(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetStringParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getStringParam");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int param=luaToInt(L,1);
        if (sim_stringparam_addonpath==param)
        {
            std::string s;
            CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
            if (it!=nullptr)
                s=it->getAddOnFilePath();
            luaWrap_lua_pushstring(L,s.c_str());
            LUA_END(1);
        }
        else
        {
            char* s=simGetStringParam_internal(param);
            if (s!=nullptr)
            {
                luaWrap_lua_pushstring(L,s);
                delete[] s;
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetArrayParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setArrayParam");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_table,0))
    {
        int parameter=luaToInt(L,1);
        if (true)
        { // for now all array parameters are tables of 3 floats
            double theArray[3];
            getDoublesFromTable(L,2,3,theArray);
            retVal=simSetArrayParam_internal(parameter,theArray);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetArrayParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getArrayParam");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int parameter=luaToInt(L,1);
        if (true)
        { // for now all parameters are tables of 3 floats
            double theArray[3];
            int retVal=simGetArrayParam_internal(parameter,theArray);
            if (retVal!=-1)
            {
                pushDoubleTableOntoStack(L,3,theArray);
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simRemoveObjects(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.removeObjects");
    if (checkInputArguments(L,&errorString,lua_arg_number,-1))
    {
        std::vector<int> handles;
        int cnt=int(luaWrap_lua_rawlen(L,1));
        if (cnt>0)
        {
            handles.resize(cnt);
            getIntsFromTable(L,1,cnt,&handles[0]);
            int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
            CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
            simRemoveObjects_internal(&handles[0],cnt);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simRemoveModel(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.removeModel");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int objId=luaToInt(L,1);
        int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
        if (!it->getThreadedExecution_oldThreads())
            retVal=simRemoveModel_internal(objId);
        else
        { // this script runs threaded and wants to destroy other objects. We need to make sure that it will only destroy objects that do not have any scripts attached with a non-nullptr lua state:
            CSceneObject* objBase=App::currentWorld->sceneObjects->getObjectFromHandle(objId);
            if (objBase!=nullptr)
            {
                if (objBase->getModelBase())
                {
                    std::vector<int> modelObjects;
                    modelObjects.push_back(objId);
                    CSceneObjectOperations::addRootObjectChildrenToSelection(modelObjects);
                    bool ok=true;
                    for (size_t j=0;j<modelObjects.size();j++)
                    {
                        std::vector<CScriptObject*> scripts;
                        App::currentWorld->embeddedScriptContainer->getScriptsFromObjectAttachedTo(modelObjects[j],scripts);
                        for (size_t i=0;i<scripts.size();i++)
                        {
                            if ( (it!=scripts[i])&&scripts[i]->hasInterpreterState() )
                                ok=false;
                        }
                    }
                    if (ok)
                        retVal=simRemoveModel_internal(objId);
                    else
                        errorString=SIM_ERROR_THREADED_SCRIPT_DESTROYING_OBJECTS_WITH_ACTIVE_SCRIPTS;
                }
                else
                    errorString=SIM_ERROR_OBJECT_NOT_MODEL_BASE;
            }
            else
                errorString=SIM_ERROR_OBJECT_INEXISTANT;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectAlias(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectAlias");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int options=-1;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if (res>=0)
        {
            if (res==2)
                options=luaToInt(L,2);
            char* name=simGetObjectAlias_internal(luaToInt(L,1),options);
            if (name!=nullptr)
            {
                luaWrap_lua_pushstring(L,name);
                simReleaseBuffer_internal(name);
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectAlias(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectAlias");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
        retVal=simSetObjectAlias_internal(luaToInt(L,1),luaWrap_lua_tostring(L,2),0);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetJointInterval(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getJointInterval");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        bool cyclic;
        double interval[2];
        if (simGetJointInterval_internal(luaToInt(L,1),&cyclic,interval)==1)
        {
            luaWrap_lua_pushboolean(L,cyclic!=0);
            pushDoubleTableOntoStack(L,2,interval);
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetJointInterval(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setJointInterval");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_bool,0,lua_arg_number,2))
    {
        double interval[2];
        getDoublesFromTable(L,3,2,interval);
        retVal=simSetJointInterval_internal(luaToInt(L,1),luaToBool(L,2),interval);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simLoadScene(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.loadScene");

    int retVal=-1;// error
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* script=App::worldContainer->getScriptFromHandle(currentScriptID);
    if ( (script!=nullptr)&&((script->getScriptType()==sim_scripttype_addonfunction)||(script->getScriptType()==sim_scripttype_addonscript)||(script->getScriptType()==sim_scripttype_sandboxscript)) )
    {
        if (checkInputArguments(L,&errorString,lua_arg_string,0))
        {
            size_t dataLength;
            const char* data=((char*)luaWrap_lua_tolstring(L,1,&dataLength));
            if (dataLength<1000)
                retVal=simLoadScene_internal(luaWrap_lua_tostring(L,1)); // loading from file
            else
            { // loading from buffer
                std::string nm(data+dataLength-12,12);
                std::vector<char> buffer(data,data+dataLength);
                if (nm=="@keepCurrent")
                {
                    buffer.erase(buffer.end()-12,buffer.end());
                    CFileOperations::createNewScene(false,true);
                }
                if (!CFileOperations::loadScene(nullptr,false,false,&buffer))
                    errorString=SIM_ERROR_SCENE_COULD_NOT_BE_READ;
                else
                    retVal=1;
            }
        }
    }
    else
        errorString=SIM_ERROR_MUST_BE_CALLED_FROM_ADDON_OR_SANDBOX_SCRIPT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCloseScene(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.closeScene");

    int retVal=-1;// error
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* script=App::worldContainer->getScriptFromHandle(currentScriptID);
    if ( (script!=nullptr)&&((script->getScriptType()==sim_scripttype_addonfunction)||(script->getScriptType()==sim_scripttype_addonscript)||(script->getScriptType()==sim_scripttype_sandboxscript)) )
        retVal=simCloseScene_internal();
    else
        errorString=SIM_ERROR_MUST_BE_CALLED_FROM_ADDON_OR_SANDBOX_SCRIPT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSaveScene(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.saveScene");

    int retVal=-1;// error
    if (luaWrap_lua_gettop(L)!=0)
    { // to file
        if (checkInputArguments(L,&errorString,lua_arg_string,0))
            retVal=simSaveScene_internal(luaWrap_lua_tostring(L,1));
    }
    else
    { // to buffer
        if (!App::currentWorld->environment->getSceneLocked())
        {
            std::vector<char> buffer;
            if (CFileOperations::saveScene(nullptr,false,false,false,&buffer))
            {
                luaWrap_lua_pushlstring(L,&buffer[0],buffer.size());
                LUA_END(1);
            }
            else
                errorString=SIM_ERROR_SCENE_COULD_NOT_BE_SAVED;
        }
        else
            errorString=SIM_ERROR_SCENE_LOCKED;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simLoadModel(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.loadModel");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        int res=checkOneGeneralInputArgument(L,2,lua_arg_bool,0,true,true,&errorString);
        if (res>=0)
        {
            bool onlyThumbnails=false;
            if (res==2)
                onlyThumbnails=luaToBool(L,2);
            size_t dataLength;
            const char* data=((char*)luaWrap_lua_tolstring(L,1,&dataLength));
            if (dataLength<1000)
            { // loading from file:
                std::string path(data,dataLength);
                size_t atCopyPos=path.find("@copy");
                bool forceAsCopy=(atCopyPos!=std::string::npos);
                if (forceAsCopy)
                    path.erase(path.begin()+atCopyPos,path.end());

                if (VFile::doesFileExist(path.c_str()))
                {
                    if (CFileOperations::loadModel(path.c_str(),false,false,false,nullptr,onlyThumbnails,forceAsCopy))
                    {
                        if (onlyThumbnails)
                        {
                            char* buff=new char[128*128*4];
                            bool opRes=App::currentWorld->environment->modelThumbnail_notSerializedHere.copyUncompressedImageToBuffer(buff);
                            if (opRes)
                            {
                                luaWrap_lua_pushlstring(L,buff,128*128*4);
                                delete[] buff;
                                LUA_END(1);
                            }
                            delete[] buff;
                            LUA_END(0);
                        }
                        else
                            retVal=App::currentWorld->sceneObjects->getLastSelectionHandle();
                    }
                    else
                        errorString=SIM_ERROR_MODEL_COULD_NOT_BE_READ;
                }
                else
                    errorString=SIM_ERROR_FILE_NOT_FOUND;
            }
            else
            { // loading from buffer:
                std::vector<char> buffer(data,data+dataLength);
                if (CFileOperations::loadModel(nullptr,false,false,false,&buffer,onlyThumbnails,false))
                {
                    if (onlyThumbnails)
                    {
                        char* buff=new char[128*128*4];
                        bool opRes=App::currentWorld->environment->modelThumbnail_notSerializedHere.copyUncompressedImageToBuffer(buff);
                        if (opRes)
                        {
                            luaWrap_lua_pushlstring(L,buff,128*128*4);
                            delete[] buff;
                            LUA_END(1);
                        }
                        delete[] buff;
                        LUA_END(0);
                    }
                    else
                        retVal=App::currentWorld->sceneObjects->getLastSelectionHandle();
                }
                else
                    errorString=SIM_ERROR_MODEL_COULD_NOT_BE_READ;
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSaveModel(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.saveModel");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int model=luaToInt(L,1);
        int res=checkOneGeneralInputArgument(L,2,lua_arg_string,0,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
                retVal=simSaveModel_internal(luaToInt(L,1),luaWrap_lua_tostring(L,2));
            else
            { // here we don't save to file, but to buffer:

                if (!App::currentWorld->environment->getSceneLocked())
                {
                    CSceneObject* it=App::currentWorld->sceneObjects->getObjectFromHandle(model);
                    if (it!=nullptr)
                    {
                        if (it->getModelBase())
                        {
                            const std::vector<int>* initSelection=App::currentWorld->sceneObjects->getSelectedObjectHandlesPtr();
                            std::vector<char> buffer;
                            if (CFileOperations::saveModel(model,nullptr,false,false,&buffer))
                            {
                                luaWrap_lua_pushlstring(L,&buffer[0],buffer.size());
                                LUA_END(1);
                            }
                            else
                                errorString=SIM_ERROR_MODEL_COULD_NOT_BE_SAVED;
                            App::currentWorld->sceneObjects->setSelectedObjectHandles(initSelection);
                        }
                        else
                            errorString=SIM_ERROR_OBJECT_NOT_MODEL_BASE;
                    }
                    else
                        errorString=SIM_ERROR_OBJECT_INEXISTANT;
                }
                else
                    errorString=SIM_ERROR_SCENE_LOCKED;
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectSel(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectSel");

    int selSize;
    int* sel=simGetObjectSel_internal(&selSize);
    if (sel!=nullptr)
    {
        pushIntTableOntoStack(L,selSize,sel);
        delete[] sel;
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectSel(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectSel");

    int argCnt=luaWrap_lua_gettop(L);
    if (argCnt>=1)
    {
        if (luaWrap_lua_istable(L,1))
        {
            int objCnt=(int)luaWrap_lua_rawlen(L,1);
            if ( (objCnt==0)||checkInputArguments(L,&errorString,lua_arg_integer,objCnt) )
            {
                std::vector<int> objectHandles;
                objectHandles.resize(objCnt,0);
                getIntsFromTable(L,1,objCnt,&objectHandles[0]);
                simSetObjectSel_internal(&objectHandles[0],objCnt);
            }
        }
        else
            errorString=SIM_ERROR_INVALID_ARGUMENT;
    }
    else
        errorString=SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetRealTimeSimulation(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getIsRealTimeSimulation");

    int retVal=simGetRealTimeSimulation_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simLaunchExecutable(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.launchExecutable");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string file(luaWrap_lua_tostring(L,1));
        int res=checkOneGeneralInputArgument(L,2,lua_arg_string,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            std::string args;
            if (res==2)
                args=luaWrap_lua_tostring(L,2);
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                int showStatus=1;
                if (res==2)
                    showStatus=luaToInt(L,3);
                int sh=VVARIOUS_SHOWNORMAL;
                if (showStatus==0)
                    sh=VVARIOUS_HIDE;
                CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
                std::string what(it->getDescriptiveName());
                what+=" (via sim.launchExecutable)";
                if (1==simCheckExecAuthorization_internal(what.c_str(),(file+" "+args).c_str(),it->getScriptHandle()))
                {
                    if (VVarious::executeExternalApplication(file.c_str(),args.c_str(),App::folders->getExecutablePath().c_str(),sh)) // executable directory needed because otherwise the shellExecute command might switch directories!
                        retVal=1;
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetExtensionString(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getExtensionString");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int objHandle=luaToInt(L,1);
        int index=luaToInt(L,2);
        int res=checkOneGeneralInputArgument(L,3,lua_arg_string,0,true,true,&errorString);
        if (res>=0)
        {
            std::string key;
            if (res==2)
                key=luaWrap_lua_tostring(L,3);
            char* str=simGetExtensionString_internal(objHandle,index,key.c_str());
            if (str!=nullptr)
            {
                luaWrap_lua_pushstring(L,str);
                simReleaseBuffer_internal(str);
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}


int _simComputeMassAndInertia(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.computeMassAndInertia");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int shapeHandle=luaToInt(L,1);
        double density=luaToDouble(L,2);
        retVal=simComputeMassAndInertia_internal(shapeHandle,density);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simTest(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.test");
    if (checkInputArguments(L,nullptr,lua_arg_string,0))
    {
        std::string cmd=luaWrap_lua_tostring(L,1);
        if (cmd.compare("sim.cborEvents")==0)
        {
            App::worldContainer->setCborEvents(luaToBool(L,2));
            LUA_END(0);
        }
        if (cmd.compare("sim.mergeEvents")==0)
        {
            App::worldContainer->setMergeEvents(luaToBool(L,2));
            LUA_END(0);
        }
        if (cmd.compare("sim.fetchCreationEvents")==0)
        {
            CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
            App::worldContainer->getGenesisEvents(stack);
            CScriptObject::buildOntoInterpreterStack_lua(L,stack,false);
            int s=stack->getStackSize();
            App::worldContainer->interfaceStackContainer->destroyStack(stack);
            LUA_END(s);
        }
        if (cmd.compare("sim.getGeodesicInfo")==0)
        {
            C3Vector pt1,pt2;
            getDoublesFromTable(L,2,3,pt1.data);
            getDoublesFromTable(L,3,3,pt2.data);
            int vl=3*((int)luaWrap_lua_rawlen(L,4)/3);
            std::vector<double> vertices;
            vertices.resize(vl);
            getDoublesFromTable(L,4,vl,vertices.data());
            std::vector<double> path;
            double dist=CMeshRoutines::getGeodesicDistanceOnConvexMesh(pt1,pt2,vertices,&path,luaToDouble(L,5));
            luaWrap_lua_pushnumber(L,dist);
            pushDoubleTableOntoStack(L,path.size(),path.data());
            LUA_END(2);
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simTextEditorOpen(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.textEditorOpen");
    int retVal=-1;

#ifdef SIM_WITH_GUI
    if (CPluginContainer::isCodeEditorPluginAvailable()&&(App::mainWindow!=nullptr))
    {
        if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_string,0))
        {
            const char* arg1=luaWrap_lua_tostring(L,1);
            const char* arg2=luaWrap_lua_tostring(L,2);
            retVal=App::mainWindow->codeEditorContainer->open(arg1,arg2,CScriptObject::getScriptHandleFromInterpreterState_lua(L));
        }
    }
    else
        errorString="code Editor plugin was not found.";
#endif

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simTextEditorClose(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.textEditorClose");

#ifdef SIM_WITH_GUI
    if (CPluginContainer::isCodeEditorPluginAvailable()&&(App::mainWindow!=nullptr))
    {
        if (checkInputArguments(L,&errorString,lua_arg_number,0))
        {
            int handle=luaToInt(L,1);
            int posAndSize[4];
            std::string txt;
            if (App::mainWindow->codeEditorContainer->close(handle,posAndSize,&txt,nullptr))
            {
                luaWrap_lua_pushstring(L,txt.c_str());
                pushIntTableOntoStack(L,2,posAndSize+0);
                pushIntTableOntoStack(L,2,posAndSize+2);
                LUA_END(3);
            }
        }
    }
    else
        errorString="code Editor plugin was not found.";
#endif

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simTextEditorShow(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.textEditorShow");
    int retVal=-1;

#ifdef SIM_WITH_GUI
    if (CPluginContainer::isCodeEditorPluginAvailable()&&(App::mainWindow!=nullptr))
    {
        if (checkInputArguments(L,&errorString,lua_arg_number,0))
        {
            int handle=luaToInt(L,1);
            bool showState=luaToBool(L,2);
            retVal=App::mainWindow->codeEditorContainer->showOrHide(handle,showState);
            luaWrap_lua_pushinteger(L,retVal);
            LUA_END(1);
        }
    }
    else
        errorString="code Editor plugin was not found.";
#endif

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simTextEditorGetInfo(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.textEditorGetInfo");

#ifdef SIM_WITH_GUI
    if (CPluginContainer::isCodeEditorPluginAvailable()&&(App::mainWindow!=nullptr))
    {
        if (checkInputArguments(L,&errorString,lua_arg_number,0))
        {
            int handle=luaToInt(L,1);
            int state=App::mainWindow->codeEditorContainer->getShowState(handle);
            if (state>=0)
            {
                int posAndSize[4];
                std::string txt=App::mainWindow->codeEditorContainer->getText(handle,posAndSize);
                luaWrap_lua_pushstring(L,txt.c_str());
                pushIntTableOntoStack(L,2,posAndSize+0);
                pushIntTableOntoStack(L,2,posAndSize+2);
                luaWrap_lua_pushboolean(L,state!=0);
                LUA_END(4);
            }
            LUA_END(0);
        }
    }
    else
        errorString="code Editor plugin was not found.";
#endif

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetJointDependency(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setJointDependency");
    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int jointHandle=luaToInt(L,1);
        int masterJointHandle=luaToInt(L,2);
        double off=luaToDouble(L,3);
        double coeff=luaToDouble(L,4);
        retVal=simSetJointDependency_internal(jointHandle,masterJointHandle,off,coeff);
        if (retVal>=0)
        {
            luaWrap_lua_pushinteger(L,retVal);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetJointDependency(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getJointDependency");
    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int jointHandle=luaToInt(L,1);
        int masterJointHandle;
        double off;
        double coeff;
        retVal=simGetJointDependency_internal(jointHandle,&masterJointHandle,&off,&coeff);
        if (retVal>=0)
        {
            luaWrap_lua_pushinteger(L,masterJointHandle);
            luaWrap_lua_pushnumber(L,off);
            luaWrap_lua_pushnumber(L,coeff);
            LUA_END(3);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetStackTraceback(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getLastStackTraceback");
    std::string retVal;
    int scriptHandle=-1;
    if (luaWrap_lua_gettop(L)!=0)
    {
        if (checkInputArguments(L,&errorString,lua_arg_number,0))
            scriptHandle=luaToInt(L,1);
    }
    else
        scriptHandle=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(scriptHandle);
    if (it!=nullptr)
        retVal=it->getAndClearLastStackTraceback();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushstring(L,retVal.c_str());
    LUA_END(1);
}

int _simSetNamedStringParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setNamedStringParam");
    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_string,0))
    {
        std::string paramName(luaWrap_lua_tostring(L,1));
        size_t l;
        const char* data=((char*)luaWrap_lua_tolstring(L,2,&l));
        retVal=simSetNamedStringParam_internal(paramName.c_str(),data,int(l));
        if (retVal>=0)
        {
            luaWrap_lua_pushinteger(L,retVal);
            LUA_END(1);
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetNamedStringParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getNamedStringParam");
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string paramName(luaWrap_lua_tostring(L,1));
        int l;
        char* stringParam=simGetNamedStringParam_internal(paramName.c_str(),&l);
        if (stringParam!=nullptr)
        {
            luaWrap_lua_pushlstring(L,stringParam,l);
            delete[] stringParam;
            LUA_END(1);
        }
        LUA_END(0);
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetNavigationMode(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setNavigationMode");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simSetNavigationMode_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetNavigationMode(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getNavigationMode");

    int retVal=-1; //error
    retVal=simGetNavigationMode_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetPage(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setPage");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simSetPage_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetPage(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getPage");
    int retVal=-1; //error

    retVal=simGetPage_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCopyPasteObjects(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.copyPasteObjects");

    if (checkInputArguments(L,&errorString,lua_arg_integer,1))
    {
        int objCnt=(int)luaWrap_lua_rawlen(L,1);
        int options=0;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_integer,0,true,false,&errorString);
        if (res>=0)
        {
            if (res==2)
                options=luaToInt(L,2);
            std::vector<int> objectHandles;
            objectHandles.resize(objCnt,0);
            getIntsFromTable(L,1,objCnt,&objectHandles[0]);
            if (simCopyPasteObjects_internal(&objectHandles[0],objCnt,options)>0)
            {
                pushIntTableOntoStack(L,objCnt,&objectHandles[0]);
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simScaleObjects(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.scaleObjects");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_number,1,lua_arg_number,0,lua_arg_bool,0))
    {
        int objCnt=(int)luaWrap_lua_rawlen(L,1);
        if (checkInputArguments(L,&errorString,lua_arg_number,objCnt,lua_arg_number,0,lua_arg_bool,0))
        {
            std::vector<int> objectHandles;
            objectHandles.resize(objCnt,0);
            getIntsFromTable(L,1,objCnt,&objectHandles[0]);
            retVal=simScaleObjects_internal(&objectHandles[0],objCnt,luaToDouble(L,2),luaToBool(L,3));
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetThreadSwitchTiming(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setThreadSwitchTiming");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int timeInMs=luaToInt(L,1);
        int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
        if (it!=nullptr)
            it->setDelayForAutoYielding(timeInMs);

        CThreadPool_old::setThreadSwitchTiming(timeInMs);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetThreadSwitchTiming(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getThreadSwitchTiming");

    int timeInMs=0;
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (it!=nullptr)
        timeInMs=it->getDelayForAutoYielding();

    luaWrap_lua_pushinteger(L,timeInMs);
    LUA_END(1);
}

int _simSetThreadAutomaticSwitch(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setThreadAutomaticSwitch");

    if (luaWrap_lua_gettop(L)>0)
    {
        int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
        if (it!=nullptr)
        {
            int retVal;
            if (luaWrap_lua_isnumber(L,1))
            {
                int a=luaToInt(L,1);
                retVal=it->changeAutoYieldingForbidLevel(a,true);
            }
            else
            {
                bool allow=luaToBool(L,1);
                if (allow)
                    retVal=it->changeAutoYieldingForbidLevel(-1,false);
                else
                    retVal=it->changeAutoYieldingForbidLevel(1,false);
            }
            CThreadPool_old::setThreadAutomaticSwitchForbidLevel(it->getAutoYieldingForbidLevel());
            luaWrap_lua_pushinteger(L,retVal);
            LUA_END(1);
        }
    }
    else
        errorString=SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetThreadAutomaticSwitch(luaWrap_lua_State* L)
{ // doesn't generate an error
    TRACE_LUA_API;
    LUA_START("sim.getThreadAutomaticSwitch");
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    bool retVal=false;
    if (it!=nullptr)
        retVal=(it->getAutoYieldingForbidLevel()==0);

    luaWrap_lua_pushboolean(L,retVal); //CThreadPool_old::getThreadAutomaticSwitch());
    LUA_END(1);
}

int _simGetThreadSwitchAllowed(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getThreadSwitchAllowed");
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    bool canYield=true;
    if (it!=nullptr)
        canYield=it->canManualYield();
    luaWrap_lua_pushboolean(L,canYield);
    LUA_END(1);
}

int _simSetThreadSwitchAllowed(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setThreadSwitchAllowed");

    if (luaWrap_lua_gettop(L)>0)
    {
        int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
        if (it!=nullptr)
        {
            int retVal;
            if (luaWrap_lua_isnumber(L,1))
            {
                int a=luaToInt(L,1);
                retVal=it->changeOverallYieldingForbidLevel(a,true);
            }
            else
            {
                bool allow=luaToBool(L,1);
                int a=1;
                if (allow)
                    a=-1;
                retVal=it->changeOverallYieldingForbidLevel(a,false);
            }
            luaWrap_lua_pushinteger(L,retVal);
            LUA_END(1);
        }
    }
    else
        errorString=SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSaveImage(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.saveImage");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,2,lua_arg_number,0,lua_arg_string,0,lua_arg_number,0))
    {
        size_t dataLength;
        char* data=((char*)luaWrap_lua_tolstring(L,1,&dataLength));
        std::string img(data,dataLength);
        int res[2];
        getIntsFromTable(L,2,2,res);
        int options=luaToInt(L,3);
        std::string filename(luaWrap_lua_tostring(L,4));
        int quality=luaToInt(L,5);
        int channels=3;
        if ((options&3)==1)
            channels=4;
        if ((options&3)==2)
            channels=1;
        if (int(dataLength)>=res[0]*res[1]*channels)
        {
            if ((res[0]>0)&&(res[1]>0))
            {
                if (filename.size()>0)
                {
                    if (filename[0]!='.')
                        retVal=simSaveImage_internal((unsigned char*)&img[0],res,options,filename.c_str(),quality,nullptr);
                    else
                    { // we save to memory:
                        std::string buffer;
                        retVal=CImageLoaderSaver::save((unsigned char*)&img[0],res,options,filename.c_str(),quality,&buffer);
                        if (retVal)
                        {
                            luaWrap_lua_pushlstring(L,&buffer[0],buffer.size());
                            LUA_END(1);
                        }
                        LUA_END(0);
                    }
                }
                else
                    errorString=SIM_ERROR_INVALID_ARGUMENT;
            }
            else
                errorString=SIM_ERROR_INVALID_RESOLUTION;
        }
        else
            errorString=SIM_ERROR_INCORRECT_BUFFER_SIZE;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simLoadImage(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.loadImage");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
    {
        int options=luaToInt(L,1);
        size_t dataLength;
        char* data=((char*)luaWrap_lua_tolstring(L,2,&dataLength));
        int resol[2];
        unsigned char* img=nullptr;
        if ( (dataLength>4)&&(data[0]=='@')&&(data[1]=='m')&&(data[2]=='e')&&(data[3]=='m') )
        {
            int reserved[1]={(int)dataLength};
            img=simLoadImage_internal(resol,options,data+4,reserved);
        }
        else
            img=simLoadImage_internal(resol,options,data,nullptr);
        if (img!=nullptr)
        {
            int s=resol[0]*resol[1]*3;
            if (options&1)
                s=resol[0]*resol[1]*4;
            luaWrap_lua_pushlstring(L,(const char*)img,s);
            delete[] ((char*)img);
            pushIntTableOntoStack(L,2,resol);
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetScaledImage(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getScaledImage");

    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,2,lua_arg_number,2,lua_arg_number,0))
    {
        size_t dataLength;
        char* data=((char*)luaWrap_lua_tolstring(L,1,&dataLength));
        std::string imgIn(data,dataLength);
        int resIn[2];
        getIntsFromTable(L,2,2,resIn);
        int resOut[2];
        getIntsFromTable(L,3,2,resOut);
        int options=luaToInt(L,4);
        int channelsIn=3;
        if (options&1)
            channelsIn=4;
        if (int(dataLength)>=resIn[0]*resIn[1]*channelsIn)
        {
            if ((resIn[0]>0)&&(resIn[1]>0)&&(resOut[0]>0)&&(resOut[1]>0))
            {
                unsigned char* imgOut=simGetScaledImage_internal((unsigned char*)&imgIn[0],resIn,resOut,options,nullptr);
                if (imgOut!=nullptr)
                {
                    int s=resOut[0]*resOut[1]*3;
                    if (options&2)
                        s=resOut[0]*resOut[1]*4;
                    luaWrap_lua_pushlstring(L,(const char*)imgOut,s);
                    delete[] ((char*)imgOut);
                    pushIntTableOntoStack(L,2,resOut);
                    LUA_END(2);
                }
            }
            else
                errorString=SIM_ERROR_INVALID_RESOLUTION;
        }
        else
            errorString=SIM_ERROR_INCORRECT_BUFFER_SIZE;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simTransformImage(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.transformImage");
    int retVal=-1;

    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,2,lua_arg_number,0))
    {
        size_t dataLength;
        char* data=((char*)luaWrap_lua_tolstring(L,1,&dataLength));
        int resol[2];
        getIntsFromTable(L,2,2,resol);
        int options=luaToInt(L,3);
        int channels=3;
        if (options&1)
            channels=4;
        if (int(dataLength)>=resol[0]*resol[1]*channels)
        {
            if ((resol[0]>0)&&(resol[1]>0))
                retVal=simTransformImage_internal((unsigned char*)data,resol,options,nullptr,nullptr,nullptr);
            else
                errorString=SIM_ERROR_INVALID_RESOLUTION;
        }
        else
            errorString=SIM_ERROR_INCORRECT_BUFFER_SIZE;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetQHull(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getQHull");

    if (checkInputArguments(L,&errorString,lua_arg_number,9))
    {
        int vl=(int)luaWrap_lua_rawlen(L,1);
        if (checkInputArguments(L,&errorString,lua_arg_number,vl))
        {
            double* vertices=new double[vl];
            getDoublesFromTable(L,1,vl,vertices);
            double* vertOut;
            int vertOutL;
            int* indOut;
            int indOutL;
            if (simGetQHull_internal(vertices,vl,&vertOut,&vertOutL,&indOut,&indOutL,0,nullptr))
            {
                pushDoubleTableOntoStack(L,vertOutL,vertOut);
                pushIntTableOntoStack(L,indOutL,indOut);
                delete[] vertOut;
                delete[] indOut;
                LUA_END(2);
            }
            delete[] vertices;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetDecimatedMesh(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getDecimatedMesh");

    if (checkInputArguments(L,&errorString,lua_arg_number,9,lua_arg_number,6,lua_arg_number,0))
    {
        int vl=(int)luaWrap_lua_rawlen(L,1);
        int il=(int)luaWrap_lua_rawlen(L,2);
        double percentage=luaToDouble(L,3);
        if (checkInputArguments(L,&errorString,lua_arg_number,vl,lua_arg_number,il,lua_arg_number,0))
        {
            double* vertices=new double[vl];
            getDoublesFromTable(L,1,vl,vertices);
            int* indices=new int[il];
            getIntsFromTable(L,2,il,indices);
            double* vertOut;
            int vertOutL;
            int* indOut;
            int indOutL;
            if (simGetDecimatedMesh_internal(vertices,vl,indices,il,&vertOut,&vertOutL,&indOut,&indOutL,percentage,0,nullptr))
            {
                pushDoubleTableOntoStack(L,vertOutL,vertOut);
                pushIntTableOntoStack(L,indOutL,indOut);
                delete[] vertOut;
                delete[] indOut;
                LUA_END(2);
            }
            delete[] vertices;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simPackInt32Table(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.packInt32Table");

    if (luaWrap_lua_gettop(L)>0)
    {
        if (luaWrap_lua_istable(L,1))
        {
            int startIndex=0;
            int count=0;
            int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    startIndex=luaToInt(L,2);

                res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        count=luaToInt(L,3);

                    int tableSize=int(luaWrap_lua_rawlen(L,1));

                    if (count==0)
                        count=tableSize-startIndex;
                    if (count>tableSize-startIndex)
                        count=tableSize-startIndex;
                    if (count>0)
                    {
                        char* data=new char[sizeof(int)*count];
                        for (int i=0;i<count;i++)
                        {
                            luaWrap_lua_rawgeti(L,1,i+1+startIndex);
                            int v=luaToInt(L,-1);
                            data[4*i+0]=((char*)&v)[0];
                            data[4*i+1]=((char*)&v)[1];
                            data[4*i+2]=((char*)&v)[2];
                            data[4*i+3]=((char*)&v)[3];
                            luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                        }
                        luaWrap_lua_pushlstring(L,(const char*)data,count*sizeof(int));
                        delete[] data;
                        LUA_END(1);
                    }
                }
            }
        }
        else
            errorString=SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG;
    }
    else
        errorString=SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simPackUInt32Table(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.packUInt32Table");

    if (luaWrap_lua_gettop(L)>0)
    {
        if (luaWrap_lua_istable(L,1))
        {
            int startIndex=0;
            int count=0;
            int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    startIndex=luaToInt(L,2);

                res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        count=luaToInt(L,3);

                    int tableSize=int(luaWrap_lua_rawlen(L,1));

                    if (count==0)
                        count=tableSize-startIndex;
                    if (count>tableSize-startIndex)
                        count=tableSize-startIndex;
                    if (count>0)
                    {
                        char* data=new char[sizeof(unsigned int)*count];
                        for (int i=0;i<count;i++)
                        {
                            luaWrap_lua_rawgeti(L,1,i+1+startIndex);
                            unsigned int v=(unsigned int)luaToInt(L,-1);
                            data[4*i+0]=((char*)&v)[0];
                            data[4*i+1]=((char*)&v)[1];
                            data[4*i+2]=((char*)&v)[2];
                            data[4*i+3]=((char*)&v)[3];
                            luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                        }
                        luaWrap_lua_pushlstring(L,(const char*)data,count*sizeof(unsigned int));
                        delete[] data;
                        LUA_END(1);
                    }
                }
            }
        }
        else
            errorString=SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG;
    }
    else
        errorString=SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simPackFloatTable(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.packFloatTable");

    if (luaWrap_lua_gettop(L)>0)
    {
        if (luaWrap_lua_istable(L,1))
        {
            int startIndex=0;
            int count=0;
            int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    startIndex=luaToInt(L,2);

                res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        count=luaToInt(L,3);

                    int tableSize=int(luaWrap_lua_rawlen(L,1));

                    if (count==0)
                        count=tableSize-startIndex;
                    if (count>tableSize-startIndex)
                        count=tableSize-startIndex;
                    if (count>0)
                    {
                        char* data=new char[sizeof(float)*count];
                        for (int i=0;i<count;i++)
                        {
                            luaWrap_lua_rawgeti(L,1,i+1+startIndex);
                            float v=luaToDouble(L,-1);
                            data[4*i+0]=((char*)&v)[0];
                            data[4*i+1]=((char*)&v)[1];
                            data[4*i+2]=((char*)&v)[2];
                            data[4*i+3]=((char*)&v)[3];
                            luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                        }
                        luaWrap_lua_pushlstring(L,(const char*)data,count*sizeof(float));
                        delete[] data;
                        LUA_END(1);
                    }
                }
            }
        }
        else
            errorString=SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG;
    }
    else
        errorString=SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simPackDoubleTable(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.packDoubleTable");

    if (luaWrap_lua_gettop(L)>0)
    {
        if (luaWrap_lua_istable(L,1))
        {
            int startIndex=0;
            int count=0;
            int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    startIndex=luaToInt(L,2);

                res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        count=luaToInt(L,3);

                    int tableSize=int(luaWrap_lua_rawlen(L,1));

                    if (count==0)
                        count=tableSize-startIndex;
                    if (count>tableSize-startIndex)
                        count=tableSize-startIndex;
                    if (count>0)
                    {
                        char* data=new char[sizeof(double)*count];
                        for (int i=0;i<count;i++)
                        {
                            luaWrap_lua_rawgeti(L,1,i+1+startIndex);
                            double v=luaToDouble(L,-1);
                            data[sizeof(double)*i+0]=((char*)&v)[0];
                            data[sizeof(double)*i+1]=((char*)&v)[1];
                            data[sizeof(double)*i+2]=((char*)&v)[2];
                            data[sizeof(double)*i+3]=((char*)&v)[3];
                            data[sizeof(double)*i+4]=((char*)&v)[4];
                            data[sizeof(double)*i+5]=((char*)&v)[5];
                            data[sizeof(double)*i+6]=((char*)&v)[6];
                            data[sizeof(double)*i+7]=((char*)&v)[7];
                            luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                        }
                        luaWrap_lua_pushlstring(L,(const char*)data,count*sizeof(double));
                        delete[] data;
                        LUA_END(1);
                    }
                }
            }
        }
        else
            errorString=SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG;
    }
    else
        errorString=SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simPackUInt8Table(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.packUInt8Table");

    if (luaWrap_lua_gettop(L)>0)
    {
        if (luaWrap_lua_istable(L,1))
        {
            int startIndex=0;
            int count=0;
            int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    startIndex=luaToInt(L,2);

                res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        count=luaToInt(L,3);

                    int tableSize=int(luaWrap_lua_rawlen(L,1));

                    if (count==0)
                        count=tableSize-startIndex;
                    if (count>tableSize-startIndex)
                        count=tableSize-startIndex;
                    if (count>0)
                    {
                        char* data=new char[sizeof(char)*count];
                        for (int i=0;i<count;i++)
                        {
                            luaWrap_lua_rawgeti(L,1,i+1+startIndex);
                            unsigned char v=(unsigned char)luaToInt(L,-1);
                            data[i]=((char*)&v)[0];
                            luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                        }
                        luaWrap_lua_pushlstring(L,(const char*)data,count*sizeof(char));
                        delete[] data;
                        LUA_END(1);
                    }
                }
            }
        }
        else
            errorString=SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG;
    }
    else
        errorString=SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simPackUInt16Table(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.packUInt16Table");

    if (luaWrap_lua_gettop(L)>0)
    {
        if (luaWrap_lua_istable(L,1))
        {
            int startIndex=0;
            int count=0;
            int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    startIndex=luaToInt(L,2);

                res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        count=luaToInt(L,3);

                    int tableSize=int(luaWrap_lua_rawlen(L,1));

                    if (count==0)
                        count=tableSize-startIndex;
                    if (count>tableSize-startIndex)
                        count=tableSize-startIndex;
                    if (count>0)
                    {
                        char* data=new char[sizeof(short)*count];
                        for (int i=0;i<count;i++)
                        {
                            luaWrap_lua_rawgeti(L,1,i+1+startIndex);
                            unsigned short v=(unsigned short)luaToInt(L,-1);
                            data[2*i+0]=((char*)&v)[0];
                            data[2*i+1]=((char*)&v)[1];
                            luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                        }
                        luaWrap_lua_pushlstring(L,(const char*)data,count*sizeof(short));
                        delete[] data;
                        LUA_END(1);
                    }
                }
            }
        }
        else
            errorString=SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG;
    }
    else
        errorString=SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simUnpackInt32Table(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.unpackInt32Table");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        int startIndex=0;
        int count=0;
        int additionalCharOffset=0;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                startIndex=luaToInt(L,2);

            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    count=luaToInt(L,3);

                res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        additionalCharOffset=luaToInt(L,4);

                    size_t dataLength;
                    char* data=((char*)luaWrap_lua_tolstring(L,1,&dataLength))+additionalCharOffset;
                    dataLength=sizeof(int)*((dataLength-additionalCharOffset)/sizeof(int));
                    int packetCount=int(dataLength/sizeof(int));
                    if (count==0)
                        count=int(1999999999);

                    if ((startIndex>=0)&&(startIndex<packetCount))
                    {
                        if (startIndex+count>packetCount)
                            count=packetCount-startIndex;

                        int* theInts=new int[count];
                        for (int i=0;i<int(count);i++)
                        {
                            int v;
                            ((char*)&v)[0]=data[sizeof(int)*(i+startIndex)+0];
                            ((char*)&v)[1]=data[sizeof(int)*(i+startIndex)+1];
                            ((char*)&v)[2]=data[sizeof(int)*(i+startIndex)+2];
                            ((char*)&v)[3]=data[sizeof(int)*(i+startIndex)+3];
                            theInts[i]=v;
                        }
                        pushIntTableOntoStack(L,count,theInts);
                        delete[] theInts;
                        LUA_END(1);
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simUnpackUInt32Table(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.unpackUInt32Table");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        int startIndex=0;
        int count=0;
        int additionalCharOffset=0;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                startIndex=luaToInt(L,2);

            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    count=luaToInt(L,3);

                res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        additionalCharOffset=luaToInt(L,4);

                    size_t dataLength;
                    char* data=((char*)luaWrap_lua_tolstring(L,1,&dataLength))+additionalCharOffset;
                    dataLength=sizeof(unsigned int)*((dataLength-additionalCharOffset)/sizeof(unsigned int));
                    int packetCount=int(dataLength/sizeof(unsigned int));
                    if (count==0)
                        count=int(1999999999);

                    if ((startIndex>=0)&&(startIndex<packetCount))
                    {
                        if (startIndex+count>packetCount)
                            count=packetCount-startIndex;

                        unsigned int* theInts=new unsigned int[count];
                        for (int i=0;i<int(count);i++)
                        {
                            unsigned int v;
                            ((char*)&v)[0]=data[sizeof(unsigned int)*(i+startIndex)+0];
                            ((char*)&v)[1]=data[sizeof(unsigned int)*(i+startIndex)+1];
                            ((char*)&v)[2]=data[sizeof(unsigned int)*(i+startIndex)+2];
                            ((char*)&v)[3]=data[sizeof(unsigned int)*(i+startIndex)+3];
                            theInts[i]=v;
                        }
                        pushUIntTableOntoStack(L,count,theInts);
                        delete[] theInts;
                        LUA_END(1);
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simUnpackFloatTable(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.unpackFloatTable");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        int startIndex=0;
        int count=0;
        int additionalCharOffset=0;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                startIndex=luaToInt(L,2);

            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    count=luaToInt(L,3);

                res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        additionalCharOffset=luaToInt(L,4);

                    size_t dataLength;
                    char* data=((char*)luaWrap_lua_tolstring(L,1,&dataLength))+additionalCharOffset;
                    dataLength=sizeof(float)*((dataLength-additionalCharOffset)/sizeof(float));
                    int packetCount=int(dataLength/sizeof(float));
                    if (count==0)
                        count=int(1999999999);

                    if ((startIndex>=0)&&(startIndex<packetCount))
                    {
                        if (startIndex+count>packetCount)
                            count=packetCount-startIndex;

                        float* theFloats=new float[count];
                        for (int i=0;i<int(count);i++)
                        {
                            float v;
                            ((char*)&v)[0]=data[sizeof(float)*(i+startIndex)+0];
                            ((char*)&v)[1]=data[sizeof(float)*(i+startIndex)+1];
                            ((char*)&v)[2]=data[sizeof(float)*(i+startIndex)+2];
                            ((char*)&v)[3]=data[sizeof(float)*(i+startIndex)+3];
                            theFloats[i]=v;
                        }
                        pushFloatTableOntoStack(L,count,theFloats);
                        delete[] theFloats;
                        LUA_END(1);
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simUnpackDoubleTable(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.unpackDoubleTable");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        int startIndex=0;
        int count=0;
        int additionalCharOffset=0;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                startIndex=luaToInt(L,2);

            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    count=luaToInt(L,3);

                res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        additionalCharOffset=luaToInt(L,4);

                    size_t dataLength;
                    char* data=((char*)luaWrap_lua_tolstring(L,1,&dataLength))+additionalCharOffset;
                    dataLength=sizeof(double)*((dataLength-additionalCharOffset)/sizeof(double));
                    int packetCount=int(dataLength/sizeof(double));
                    if (count==0)
                        count=int(1999999999);

                    if ((startIndex>=0)&&(startIndex<packetCount))
                    {
                        if (startIndex+count>packetCount)
                            count=packetCount-startIndex;

                        double* theDoubles=new double[count];
                        for (int i=0;i<int(count);i++)
                        {
                            double v;
                            ((char*)&v)[0]=data[sizeof(double)*(i+startIndex)+0];
                            ((char*)&v)[1]=data[sizeof(double)*(i+startIndex)+1];
                            ((char*)&v)[2]=data[sizeof(double)*(i+startIndex)+2];
                            ((char*)&v)[3]=data[sizeof(double)*(i+startIndex)+3];
                            ((char*)&v)[4]=data[sizeof(double)*(i+startIndex)+4];
                            ((char*)&v)[5]=data[sizeof(double)*(i+startIndex)+5];
                            ((char*)&v)[6]=data[sizeof(double)*(i+startIndex)+6];
                            ((char*)&v)[7]=data[sizeof(double)*(i+startIndex)+7];
                            theDoubles[i]=v;
                        }
                        pushDoubleTableOntoStack(L,count,theDoubles);
                        delete[] theDoubles;
                        LUA_END(1);
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simUnpackUInt8Table(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.unpackUInt8Table");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        int startIndex=0;
        int count=0;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                startIndex=luaToInt(L,2);

            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    count=luaToInt(L,3);

                size_t dataLength;
                const char* data=(char*)luaWrap_lua_tolstring(L,1,&dataLength);
                int packetCount=(int)dataLength;
                if (count==0)
                    count=int(1999999999);

                if ((startIndex>=0)&&(startIndex<packetCount))
                {
                    if (startIndex+count>packetCount)
                        count=packetCount-startIndex;

                    int* theBytes=new int[count];
                    for (int i=0;i<count;i++)
                    {
                        unsigned char v;
                        ((char*)&v)[0]=data[i+startIndex];
                        theBytes[i]=v;
                    }
                    pushIntTableOntoStack(L,count,theBytes);
                    delete[] theBytes;
                    LUA_END(1);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simUnpackUInt16Table(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.unpackUInt16Table");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        int startIndex=0;
        int count=0;
        int additionalCharOffset=0;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                startIndex=luaToInt(L,2);

            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    count=luaToInt(L,3);

                res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        additionalCharOffset=luaToInt(L,4);

                    size_t dataLength;
                    char* data=((char*)luaWrap_lua_tolstring(L,1,&dataLength))+additionalCharOffset;
                    dataLength=2*((dataLength-additionalCharOffset)/2);
                    int packetCount=(int)dataLength/2;
                    if (count==0)
                        count=int(1999999999);

                    if ((startIndex>=0)&&(startIndex<packetCount))
                    {
                        if (startIndex+count>packetCount)
                            count=packetCount-startIndex;

                        int* theWords=new int[count];
                        for (int i=0;i<int(count);i++)
                        {
                            unsigned short v;
                            ((char*)&v)[0]=data[2*(i+startIndex)+0];
                            ((char*)&v)[1]=data[2*(i+startIndex)+1];
                            theWords[i]=v;
                        }
                        pushIntTableOntoStack(L,count,theWords);
                        delete[] theWords;
                        LUA_END(1);
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simTransformBuffer(luaWrap_lua_State* L)
{ // string inBuffer,number inFormat,number multiplier,number offset,number outFormat
    TRACE_LUA_API;
    LUA_START("sim.transformBuffer");

    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        size_t dataLength;
        int inFormat=luaToInt(L,2);
        int outFormat=luaToInt(L,5);
        bool clamp=(outFormat&sim_buffer_clamp)!=0;
        if (clamp)
            outFormat-=sim_buffer_clamp;
        bool something=false;
        if (inFormat==sim_buffer_float)
        {
            double mult=luaToDouble(L,3);
            double off=luaToDouble(L,4);
            bool noScalingNorOffset=( (mult==1.0)&&(off==0.0) );
            const float* data=(const float*)luaWrap_lua_tolstring(L,1,&dataLength);
            something=true;
            dataLength-=(dataLength % sizeof(float));
            dataLength/=sizeof(float);
            if (dataLength!=0)
            {
                if (outFormat==sim_buffer_uint8)
                {
                   unsigned char* dat=new unsigned char[dataLength];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               float v=data[i];
                               dat[i]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               float v=data[i]*mult+off;
                               dat[i]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(unsigned char)(data[i]);
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(unsigned char)(data[i]*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_uint8rgb)
                {
                   unsigned char* dat=new unsigned char[dataLength*3];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               float v=data[i];
                               dat[3*i+0]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               dat[3*i+1]=dat[3*i+0];
                               dat[3*i+2]=dat[3*i+0];
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               float v=data[i]*mult+off;
                               dat[3*i+0]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               dat[3*i+1]=dat[3*i+0];
                               dat[3*i+2]=dat[3*i+0];
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               dat[3*i+0]=(unsigned char)data[i];
                               dat[3*i+1]=dat[3*i+0];
                               dat[3*i+2]=dat[3*i+0];
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               dat[3*i+0]=(unsigned char)(data[i]*mult+off);
                               dat[3*i+1]=dat[3*i+0];
                               dat[3*i+2]=dat[3*i+0];
                           }
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*3);
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_int8)
                {
                   char* dat=new char[dataLength];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               float v=data[i];
                               dat[i]=(v<-128.499)?(-128):((v>127.499)?(127):((char)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               float v=data[i]*mult+off;
                               dat[i]=(v<-128.499)?(-128):((v>127.499)?(127):((char)v));
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(char)data[i];
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(char)(data[i]*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_uint16)
                {
                    uint16_t* dat=new uint16_t[dataLength];
                    if (clamp)
                    {
                        if (noScalingNorOffset)
                        {
                            for (size_t i=0;i<dataLength;i++)
                            {
                                float v=data[i];
                                dat[i]=(v<0.0)?(0):((v>65535.499)?(65535):((uint16_t)v));
                            }
                        }
                        else
                        {
                            for (size_t i=0;i<dataLength;i++)
                            {
                                float v=data[i]*mult+off;
                                dat[i]=(v<0.0)?(0):((v>65535.499)?(65535):((uint16_t)v));
                            }
                        }
                    }
                    else
                    {
                        if (noScalingNorOffset)
                        {
                            for (size_t i=0;i<dataLength;i++)
                                dat[i]=(uint16_t)data[i];
                        }
                        else
                        {
                            for (size_t i=0;i<dataLength;i++)
                                dat[i]=(uint16_t)(data[i]*mult+off);
                        }
                    }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*sizeof(uint16_t));
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_int16)
                {
                   int16_t* dat=new int16_t[dataLength];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               float v=data[i];
                               dat[i]=(v<-32768.499)?(-32768):((v>32767.499)?(32767):((int16_t)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               float v=data[i]*mult+off;
                               dat[i]=(v<-32768.499)?(-32768):((v>32767.499)?(32767):((int16_t)v));
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(int16_t)data[i];
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(int16_t)(data[i]*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*sizeof(int16_t));
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_uint32)
                {
                   uint32_t* dat=new uint32_t[dataLength];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               float v=data[i];
                               dat[i]=(v<0.0)?(0):((v>4294967295.499)?(4294967295):((uint32_t)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               float v=data[i]*mult+off;
                               dat[i]=(v<0.0)?(0):((v>4294967295.499)?(4294967295):((uint32_t)v));
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(uint32_t)data[i];
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(uint32_t)(data[i]*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*sizeof(uint32_t));
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_int32)
                {
                   int32_t* dat=new int32_t[dataLength];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               float v=data[i];
                               dat[i]=(v<-2147483648.499)?(-2147483647):((v>2147483647.499)?(2147483647):((int32_t)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               float v=data[i]*mult+off;
                               dat[i]=(v<-2147483648.499)?(-2147483647):((v>2147483647.499)?(2147483647):((int32_t)v));
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(int32_t)data[i];
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(int32_t)(data[i]*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*sizeof(int32_t));
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_float)
                {
                   float* dat=new float[dataLength];
                   if (noScalingNorOffset)
                   {
                       for (size_t i=0;i<dataLength;i++)
                           dat[i]=data[i];
                   }
                   else
                   {
                       for (size_t i=0;i<dataLength;i++)
                           dat[i]=data[i]*mult+off;
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*sizeof(float));
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_double)
                {
                   double* dat=new double[dataLength];
                   if (noScalingNorOffset)
                   {
                       for (size_t i=0;i<dataLength;i++)
                           dat[i]=(double)data[i];
                   }
                   else
                   {
                       for (size_t i=0;i<dataLength;i++)
                           dat[i]=(double)(data[i]*mult+off);
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*sizeof(double));
                   delete[] dat;
                   LUA_END(1);
                }
                errorString=SIM_ERROR_INVALID_FORMAT;
            }
            else
                errorString=SIM_ERROR_INVALID_DATA;
        }
        if (inFormat==sim_buffer_double)
        {
            double mult=luaToDouble(L,3);
            double off=luaToDouble(L,4);
            bool noScalingNorOffset=( (mult==1.0)&&(off==0.0) );
            const double* data=(const double*)luaWrap_lua_tolstring(L,1,&dataLength);
            something=true;
            dataLength-=(dataLength % sizeof(double));
            dataLength/=sizeof(double);
            if (dataLength!=0)
            {
                if (outFormat==sim_buffer_uint8)
                {
                   unsigned char* dat=new unsigned char[dataLength];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i];
                               dat[i]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i]*mult+off;
                               dat[i]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(unsigned char)data[i];
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(unsigned char)(data[i]*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_uint8rgb)
                {
                   unsigned char* dat=new unsigned char[dataLength*3];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i];
                               dat[3*i+0]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               dat[3*i+1]=dat[3*i+0];
                               dat[3*i+2]=dat[3*i+0];
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i]*mult+off;
                               dat[3*i+0]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               dat[3*i+1]=dat[3*i+0];
                               dat[3*i+2]=dat[3*i+0];
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               dat[3*i+0]=(unsigned char)data[i];
                               dat[3*i+1]=dat[3*i+0];
                               dat[3*i+2]=dat[3*i+0];
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               dat[3*i+0]=(unsigned char)(data[i]*mult+off);
                               dat[3*i+1]=dat[3*i+0];
                               dat[3*i+2]=dat[3*i+0];
                           }
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*3);
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_int8)
                {
                   char* dat=new char[dataLength];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i];
                               dat[i]=(v<-128.499)?(-128):((v>127.499)?(127):((char)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i]*mult+off;
                               dat[i]=(v<-128.499)?(-128):((v>127.499)?(127):((char)v));
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(char)data[i];
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(char)(data[i]*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_uint16)
                {
                   uint16_t* dat=new uint16_t[dataLength];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i];
                               dat[i]=(v<0.0)?(0):((v>65535.499)?(65535):((uint16_t)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i]*mult+off;
                               dat[i]=(v<0.0)?(0):((v>65535.499)?(65535):((uint16_t)v));
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(uint16_t)data[i];
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(uint16_t)(data[i]*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*sizeof(uint16_t));
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_int16)
                {
                   int16_t* dat=new int16_t[dataLength];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i];
                               dat[i]=(v<-32768.499)?(-32768):((v>32767.499)?(32767):((int16_t)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i]*mult+off;
                               dat[i]=(v<-32768.499)?(-32768):((v>32767.499)?(32767):((int16_t)v));
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(int16_t)data[i];
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(int16_t)(data[i]*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*sizeof(int16_t));
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_uint32)
                {
                   uint32_t* dat=new uint32_t[dataLength];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i];
                               dat[i]=(v<0.0)?(0):((v>4294967295.499)?(4294967295):((uint32_t)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i]*mult+off;
                               dat[i]=(v<0.0)?(0):((v>4294967295.499)?(4294967295):((uint32_t)v));
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(uint32_t)data[i];
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(uint32_t)(data[i]*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*sizeof(uint32_t));
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_int32)
                {
                   int32_t* dat=new int32_t[dataLength];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i];
                               dat[i]=(v<-2147483648.499)?(-2147483647):((v>2147483647.499)?(2147483647):((int32_t)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i]*mult+off;
                               dat[i]=(v<-2147483648.499)?(-2147483647):((v>2147483647.499)?(2147483647):((int32_t)v));
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(int32_t)data[i];
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(int32_t)(data[i]*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*sizeof(int32_t));
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_float)
                {
                   float* dat=new float[dataLength];
                   if (clamp)
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i];
                               dat[i]=(v<-FLOATFLOAT_MAX)?(-FLOATFLOAT_MAX):((v>FLOATFLOAT_MAX)?(FLOATFLOAT_MAX):((float)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=data[i]*mult+off;
                               dat[i]=(v<-FLOATFLOAT_MAX)?(-FLOATFLOAT_MAX):((v>FLOATFLOAT_MAX)?(FLOATFLOAT_MAX):((float)v));
                           }
                       }
                   }
                   else
                   {
                       if (noScalingNorOffset)
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(float)data[i];
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(float)(data[i]*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*sizeof(float));
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_double)
                {
                   double* dat=new double[dataLength];
                   if (noScalingNorOffset)
                   {
                       for (size_t i=0;i<dataLength;i++)
                           dat[i]=data[i];
                   }
                   else
                   {
                       for (size_t i=0;i<dataLength;i++)
                           dat[i]=data[i]*mult+off;
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength*sizeof(double));
                   delete[] dat;
                   LUA_END(1);
                }
                errorString=SIM_ERROR_INVALID_FORMAT;
            }
            else
                errorString=SIM_ERROR_INVALID_DATA;
        }
        if ( (inFormat==sim_buffer_uint8rgb)||(inFormat==sim_buffer_uint8bgr) )
        {
            double mult=luaToDouble(L,3);
            double off=luaToDouble(L,4);
            bool noScalingNorOffset=( (mult==1.0)&&(off==0.0) );
            const unsigned char* data=(const unsigned char*)luaWrap_lua_tolstring(L,1,&dataLength);
            something=true;
            dataLength-=(dataLength % 3);
            dataLength/=3;
            if (dataLength!=0)
            {
                if ( ( (inFormat==sim_buffer_uint8rgb)&&(outFormat==sim_buffer_uint8bgr) )||( (inFormat==sim_buffer_uint8bgr)&&(outFormat==sim_buffer_uint8rgb) ) )
                {
                   unsigned char* dat=new unsigned char[3*dataLength];
                   if (noScalingNorOffset)
                   {
                       for (size_t i=0;i<dataLength;i++)
                       {
                           dat[3*i+0]=data[3*i+2];
                           dat[3*i+1]=data[3*i+1];
                           dat[3*i+2]=data[3*i+0];
                       }
                   }
                   else
                   {
                       if (clamp)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=double(data[3*i+2])*mult+off;
                               dat[3*i+0]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[3*i+1])*mult+off;
                               dat[3*i+1]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[3*i+0])*mult+off;
                               dat[3*i+2]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                                dat[3*i+0]=(unsigned char)(double(data[3*i+2])*mult+off);
                                dat[3*i+1]=(unsigned char)(double(data[3*i+1])*mult+off);
                                dat[3*i+2]=(unsigned char)(double(data[3*i+0])*mult+off);
                           }
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,3*dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                if (inFormat==outFormat)
                {
                   unsigned char* dat=new unsigned char[3*dataLength];
                   if (noScalingNorOffset)
                   {
                       for (size_t i=0;i<dataLength;i++)
                       {
                           dat[3*i+0]=data[3*i+0];
                           dat[3*i+1]=data[3*i+1];
                           dat[3*i+2]=data[3*i+2];
                       }
                   }
                   else
                   {
                       if (clamp)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=double(data[3*i+0])*mult+off;
                               dat[3*i+0]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[3*i+1])*mult+off;
                               dat[3*i+1]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[3*i+2])*mult+off;
                               dat[3*i+2]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               dat[3*i+0]=(unsigned char)(double(data[3*i+0])*mult+off);
                               dat[3*i+1]=(unsigned char)(double(data[3*i+1])*mult+off);
                               dat[3*i+2]=(unsigned char)(double(data[3*i+2])*mult+off);
                           }
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,3*dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                errorString=SIM_ERROR_INVALID_FORMAT;
            }
            else
                errorString=SIM_ERROR_INVALID_DATA;
        }
        if (inFormat==sim_buffer_uint8rgba)
        {
            double mult=luaToDouble(L,3);
            double off=luaToDouble(L,4);
            bool noScalingNorOffset=( (mult==1.0)&&(off==0.0) );
            const unsigned char* data=(const unsigned char*)luaWrap_lua_tolstring(L,1,&dataLength);
            something=true;
            dataLength-=(dataLength % 4);
            dataLength/=4;
            if (dataLength!=0)
            {
                if (outFormat==sim_buffer_uint8rgb)
                {
                   unsigned char* dat=new unsigned char[3*dataLength];
                   if (noScalingNorOffset)
                   {
                       for (size_t i=0;i<dataLength;i++)
                       {
                           dat[3*i+0]=data[4*i+0];
                           dat[3*i+1]=data[4*i+1];
                           dat[3*i+2]=data[4*i+2];
                       }
                   }
                   else
                   {
                       if (clamp)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=double(data[4*i+0])*mult+off;
                               dat[3*i+0]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[4*i+1])*mult+off;
                               dat[3*i+1]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[4*i+2])*mult+off;
                               dat[3*i+2]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               dat[3*i+0]=(unsigned char)(double(data[4*i+0])*mult+off);
                               dat[3*i+1]=(unsigned char)(double(data[4*i+1])*mult+off);
                               dat[3*i+2]=(unsigned char)(double(data[4*i+2])*mult+off);
                           }
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,3*dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==inFormat)
                {
                   unsigned char* dat=new unsigned char[4*dataLength];
                   if (noScalingNorOffset)
                   {
                       for (size_t i=0;i<dataLength;i++)
                       {
                           dat[4*i+0]=data[4*i+0];
                           dat[4*i+1]=data[4*i+1];
                           dat[4*i+2]=data[4*i+2];
                           dat[4*i+3]=data[4*i+3];
                       }
                   }
                   else
                   {
                       if (clamp)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=double(data[4*i+0])*mult+off;
                               dat[4*i+0]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[4*i+1])*mult+off;
                               dat[4*i+1]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[4*i+2])*mult+off;
                               dat[4*i+2]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[4*i+3])*mult+off;
                               dat[4*i+3]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               dat[4*i+0]=(unsigned char)(double(data[4*i+0])*mult+off);
                               dat[4*i+1]=(unsigned char)(double(data[4*i+1])*mult+off);
                               dat[4*i+2]=(unsigned char)(double(data[4*i+2])*mult+off);
                               dat[4*i+3]=(unsigned char)(double(data[4*i+3])*mult+off);
                           }
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,4*dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                errorString=SIM_ERROR_INVALID_FORMAT;
            }
            else
                errorString=SIM_ERROR_INVALID_DATA;
        }
        if (inFormat==sim_buffer_uint8argb)
        {
            double mult=luaToDouble(L,3);
            double off=luaToDouble(L,4);
            bool noScalingNorOffset=( (mult==1.0)&&(off==0.0) );
            const unsigned char* data=(const unsigned char*)luaWrap_lua_tolstring(L,1,&dataLength);
            something=true;
            dataLength-=(dataLength % 4);
            dataLength/=4;
            if (dataLength!=0)
            {
                if (outFormat==sim_buffer_uint8rgb)
                {
                   unsigned char* dat=new unsigned char[3*dataLength];
                   if (noScalingNorOffset)
                   {
                       for (size_t i=0;i<dataLength;i++)
                       {
                           dat[3*i+0]=data[4*i+1];
                           dat[3*i+1]=data[4*i+2];
                           dat[3*i+2]=data[4*i+3];
                       }
                   }
                   else
                   {
                       if (clamp)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=double(data[4*i+1])*mult+off;
                               dat[3*i+0]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[4*i+2])*mult+off;
                               dat[3*i+1]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[4*i+3])*mult+off;
                               dat[3*i+2]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               dat[3*i+0]=(unsigned char)(double(data[4*i+1])*mult+off);
                               dat[3*i+1]=(unsigned char)(double(data[4*i+2])*mult+off);
                               dat[3*i+2]=(unsigned char)(double(data[4*i+3])*mult+off);
                           }
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,3*dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==inFormat)
                {
                   unsigned char* dat=new unsigned char[4*dataLength];
                   if (noScalingNorOffset)
                   {
                       for (size_t i=0;i<dataLength;i++)
                       {
                           dat[4*i+0]=data[4*i+0];
                           dat[4*i+1]=data[4*i+1];
                           dat[4*i+2]=data[4*i+2];
                           dat[4*i+3]=data[4*i+3];
                       }
                   }
                   else
                   {
                       if (clamp)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=double(data[4*i+0])*mult+off;
                               dat[4*i+0]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[4*i+1])*mult+off;
                               dat[4*i+1]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[4*i+2])*mult+off;
                               dat[4*i+2]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               v=double(data[4*i+3])*mult+off;
                               dat[4*i+3]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               dat[4*i+0]=(unsigned char)(double(data[4*i+0])*mult+off);
                               dat[4*i+1]=(unsigned char)(double(data[4*i+1])*mult+off);
                               dat[4*i+2]=(unsigned char)(double(data[4*i+2])*mult+off);
                               dat[4*i+3]=(unsigned char)(double(data[4*i+3])*mult+off);
                           }
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,4*dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                errorString=SIM_ERROR_INVALID_FORMAT;
            }
            else
                errorString=SIM_ERROR_INVALID_DATA;
        }
        if (inFormat==sim_buffer_uint8)
        {
            double mult=luaToDouble(L,3);
            double off=luaToDouble(L,4);
            bool noScalingNorOffset=( (mult==1.0)&&(off==0.0) );
            const unsigned char* data=(const unsigned char*)luaWrap_lua_tolstring(L,1,&dataLength);
            something=true;
            if (dataLength!=0)
            {
                if (outFormat==sim_buffer_split)
                {
                    size_t splitSize=size_t(luaToInt(L,4));
                    if (splitSize>0)
                    {
                        std::vector<std::string> stringTable;
                        size_t of=0;
                        while (of<dataLength)
                        {
                            size_t of2=of+splitSize;
                            if (of2>dataLength)
                                of2=dataLength;
                            std::string astr;
                            astr.assign(data+of,data+of2);
                            stringTable.push_back(astr);
                            of+=splitSize;
                        }
                        pushLStringTableOntoStack(L,stringTable);
                        LUA_END(1);
                    }
                }
                if (outFormat==sim_buffer_base64)
                {
                    std::string inDat(data,data+dataLength);
                    std::string outDat(CTTUtil::encode64(inDat));
                    luaWrap_lua_pushlstring(L,outDat.c_str(),outDat.length());
                    LUA_END(1);
                }
                if (outFormat==sim_buffer_uint8)
                {
                   unsigned char* dat=new unsigned char[dataLength];
                   if (noScalingNorOffset)
                   {
                       for (size_t i=0;i<dataLength;i++)
                           dat[i]=data[i];
                   }
                   else
                   {
                       if (clamp)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=double(data[i])*mult+off;
                               dat[i]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                               dat[i]=(unsigned char)(double(data[i])*mult+off);
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                if (outFormat==sim_buffer_uint8rgb)
                {
                   unsigned char* dat=new unsigned char[3*dataLength];
                   if (noScalingNorOffset)
                   {
                       for (size_t i=0;i<dataLength;i++)
                       {
                           dat[3*i+0]=data[i];
                           dat[3*i+1]=data[i];
                           dat[3*i+2]=data[i];
                       }
                   }
                   else
                   {
                       if (clamp)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=double(data[i])*mult+off;
                               dat[3*i+0]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                               dat[3*i+1]=dat[3*i+0];
                               dat[3*i+2]=dat[3*i+0];
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               dat[3*i+0]=(unsigned char)(double(data[i])*mult+off);
                               dat[3*i+1]=dat[3*i+0];
                               dat[3*i+2]=dat[3*i+0];
                           }
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,3*dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                errorString=SIM_ERROR_INVALID_FORMAT;
            }
            else
                errorString=SIM_ERROR_INVALID_DATA;
        }
        if ( (inFormat==sim_buffer_uint8rgb)||(inFormat==sim_buffer_uint8bgr) )
        {
            double mult=luaToDouble(L,3);
            double off=luaToDouble(L,4);
            bool noScalingNorOffset=( (mult==1.0)&&(off==0.0) );
            const unsigned char* data=(const unsigned char*)luaWrap_lua_tolstring(L,1,&dataLength);
            something=true;
            dataLength-=(dataLength % 3);
            dataLength/=3;
            if (dataLength!=0)
            {
                if (outFormat==sim_buffer_uint8)
                {
                   unsigned char* dat=new unsigned char[dataLength];
                   if (noScalingNorOffset)
                   {
                       for (size_t i=0;i<dataLength;i++)
                           dat[i]=(int(data[3*i+0])+int(data[3*i+1])+int(data[3*i+2]))/3;
                   }
                   else
                   {
                       if (clamp)
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=off+(double(data[3*i+0])+double(data[3*i+1])+double(data[3*i+2]))*mult/3.0;
                               dat[i]=(v<0.0)?(0):((v>255.499)?(255):((unsigned char)v));
                           }
                       }
                       else
                       {
                           for (size_t i=0;i<dataLength;i++)
                           {
                               double v=off+(double(data[3*i+0])+double(data[3*i+1])+double(data[3*i+2]))*mult/3.0;
                               dat[i]=(unsigned char)v;
                           }
                       }
                   }
                   luaWrap_lua_pushlstring(L,(const char*)dat,dataLength);
                   delete[] dat;
                   LUA_END(1);
                }
                errorString=SIM_ERROR_INVALID_FORMAT;
            }
            else
                errorString=SIM_ERROR_INVALID_DATA;
        }
        if (inFormat==sim_buffer_base64)
        {
            const unsigned char* data=(const unsigned char*)luaWrap_lua_tolstring(L,1,&dataLength);
            something=true;
            if (dataLength!=0)
            {
                if (outFormat==sim_buffer_uint8)
                {
                    std::string inDat(data,data+dataLength);
                    std::string outDat(CTTUtil::decode64(inDat));
                    luaWrap_lua_pushlstring(L,outDat.c_str(),outDat.length());
                    LUA_END(1);
                }
                errorString=SIM_ERROR_INVALID_FORMAT;
            }
            else
                errorString=SIM_ERROR_INVALID_DATA;
        }

        if (!something)
            errorString=SIM_ERROR_INVALID_FORMAT;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simCombineRgbImages(luaWrap_lua_State* L)
{   TRACE_LUA_API;
    LUA_START("sim.combineRgbImages");

    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,2,lua_arg_string,0,lua_arg_number,2,lua_arg_number,0))
    {
        size_t img1Length,img2Length;
        char* img1=(char*)luaWrap_lua_tolstring(L,1,&img1Length);
        char* img2=(char*)luaWrap_lua_tolstring(L,3,&img2Length);
        int res1[2];
        int res2[2];
        getIntsFromTable(L,2,2,res1);
        getIntsFromTable(L,4,2,res2);
        int op=luaToInt(L,5);
        if ( (img1Length>=size_t(res1[0]*res1[1]*3))&&(img2Length>=size_t(res2[0]*res2[1]*3)) )
        {
            if (op==sim_imgcomb_vertical)
            {
               if (res1[0]==res2[0])
               {
                   unsigned char* dat=new unsigned char[(res1[0]*res1[1]+res2[0]*res2[1])*3];
                   size_t l=size_t(res1[0]*res1[1]*3);
                   for (size_t i=0;i<l;i++)
                       dat[i]=img1[i];
                   for (size_t i=0;i<size_t(res2[0]*res2[1]*3);i++)
                       dat[l+i]=img2[i];
                   luaWrap_lua_pushlstring(L,(const char*)dat,(res1[0]*res1[1]+res2[0]*res2[1])*3);
                   delete[] dat;
                   LUA_END(1);
               }
               else
                   errorString=SIM_ERROR_INVALID_RESOLUTION;
            }
            if (op==sim_imgcomb_horizontal)
            {
                if (res1[1]==res2[1])
                {
                    unsigned char* dat=new unsigned char[(res1[0]*res1[1]+res2[0]*res2[1])*3];
                    for (size_t y=0;y<size_t(res1[1]);y++)
                    {
                        int off1=(int)y*res1[0]*3;
                        int off2=(int)y*res2[0]*3;
                        int off3=off1+off2;
                        for (size_t i=0;i<size_t(res1[0]*3);i++)
                            dat[off3+i]=img1[off1+i];
                        off3+=res1[0]*3;
                        for (size_t i=0;i<size_t(res2[0]*3);i++)
                            dat[off3+i]=img2[off2+i];
                    }
                    luaWrap_lua_pushlstring(L,(const char*)dat,(res1[0]*res1[1]+res2[0]*res2[1])*3);
                    delete[] dat;
                    LUA_END(1);
                }
                else
                    errorString=SIM_ERROR_INVALID_RESOLUTION;
            }
        }
        else
            errorString=SIM_ERROR_INVALID_DATA;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simAddDrawingObject(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.addDrawingObject");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int objType=luaToInt(L,1);
        double size=luaToDouble(L,2);
        double duplicateTolerance=luaToDouble(L,3);
        int parentID=luaToInt(L,4);
        int maxItemCount=luaToInt(L,5);
        float* ambient=nullptr;
        float* specular=nullptr;
        float* emission=nullptr;
        int res=checkOneGeneralInputArgument(L,6,lua_arg_number,3,true,true,&errorString);
        int okToGo=(res!=-1);
        if (okToGo)
        {
            float ambientC[3];
            float specularC[3];
            float emissionC[6];
            if (res>0)
            {
                if (res==2)
                {
                    getFloatsFromTable(L,6,3,ambientC);
                    ambient=ambientC;
                }
                res=checkOneGeneralInputArgument(L,7,lua_arg_number,3,true,true,&errorString);
                okToGo=(res!=-1);
                if (okToGo)
                {
                    if (res>0)
                    {
                        res=checkOneGeneralInputArgument(L,8,lua_arg_number,3,true,true,&errorString);
                        okToGo=(res!=-1);
                        if (okToGo)
                        {
                            if (res>0)
                            {
                                if (res==2)
                                {
                                    getFloatsFromTable(L,8,3,specularC);
                                    specular=specularC;
                                }
                                res=checkOneGeneralInputArgument(L,9,lua_arg_number,3,true,true,&errorString);
                                okToGo=(res!=-1);
                                if (okToGo)
                                {
                                    if (res>0)
                                    {
                                        if (res==2)
                                        {
                                            // following 3 are default aux colors:
                                            emissionC[3]=0.5;
                                            emissionC[4]=0.0;
                                            emissionC[5]=0.0;
                                            if (int(luaWrap_lua_rawlen(L,9))<6)
                                                getFloatsFromTable(L,9,3,emissionC);
                                            else
                                            {
                                                objType|=sim_drawing_auxchannelcolor1;
                                                getFloatsFromTable(L,9,6,emissionC);
                                            }
                                            emission=emissionC;
                                        }

                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (okToGo)
            {
                setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
                retVal=simAddDrawingObject_internal(objType,size,duplicateTolerance,parentID,maxItemCount,ambient,nullptr,specular,emission);
                setCurrentScriptInfo_cSide(-1,-1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simRemoveDrawingObject(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.removeDrawingObject");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int objectHandle=luaToInt(L,1);
        if (objectHandle==sim_handle_all)
        { // following condition added here on 2011/01/06 so as not to remove objects created from a c/c++ call or from add-on:
            int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
            CScriptObject* itScrObj=App::worldContainer->getScriptFromHandle(currentScriptID);
            App::currentWorld->drawingCont->eraseAllObjects();
            retVal=1;
        }
        else
            retVal=simRemoveDrawingObject_internal(objectHandle);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAddDrawingObjectItem(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.addDrawingObjectItem");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int h=luaToInt(L,1);
        int handleFlags=h&0xff00000;
        h=h&0xfffff;

        CDrawingObject* it=App::currentWorld->drawingCont->getObject(h);
        size_t d=3;
        if (it!=nullptr)
            d=size_t(it->getExpectedFloatsPerItem());
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,int(d),true,true,&errorString);
        if (res==2)
        {
            std::vector<double> vertices;
            if ( (((handleFlags&sim_handleflag_setmultiple)!=0)||((handleFlags&sim_handleflag_addmultiple)!=0))&&(it!=nullptr) )
            {
                size_t itemCnt=luaWrap_lua_rawlen(L,2)/d;
                vertices.resize(itemCnt*d);
                getDoublesFromTable(L,2,int(itemCnt*d),&vertices[0]);
                if ((handleFlags&sim_handleflag_setmultiple)!=0)
                    it->setItems(&vertices[0],itemCnt);
                else
                    it->addItems(&vertices[0],itemCnt);
                retVal=1;
            }
            else
            {
                vertices.resize(d);
                getDoublesFromTable(L,2,int(d),&vertices[0]);
                retVal=simAddDrawingObjectItem_internal(h,&vertices[0]);
            }
        }
        else
        {
            if (res>=0)
                retVal=simAddDrawingObjectItem_internal(h,nullptr);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAddParticleObject(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.addParticleObject");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        // The 4th argument can be nil or a table. Check for that:
        if (luaWrap_lua_gettop(L)<4)
            errorString=SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS;
        else
        {
            if ( (!luaWrap_lua_isnil(L,4))&&((!luaWrap_lua_istable(L,4))||(int(luaWrap_lua_rawlen(L,4))<3)) )
                errorString=SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG;
            else
            {
                if ( (checkOneGeneralInputArgument(L,5,lua_arg_number,0,false,false,&errorString)==2)&&
                    (checkOneGeneralInputArgument(L,6,lua_arg_number,0,false,false,&errorString)==2) )
                {
                    int objType=luaToInt(L,1);
                    double size=luaToDouble(L,2);
                    double massOverVolume=luaToDouble(L,3);
                    int paramLen=int(luaWrap_lua_rawlen(L,4));
                    paramLen=(paramLen-1)/2;
                    paramLen=paramLen*2+1;
                    void* params=nullptr;
                    if (!luaWrap_lua_isnil(L,4))
                    {
                        int intParams[30];
                        double floatParams[30];
                        getIntsFromTable(L,4,paramLen,intParams);
                        getDoublesFromTable(L,4,paramLen,floatParams);
                        params=new char[paramLen*sizeof(int)];
                        ((int*)params)[0]=std::min<int>(intParams[0],(paramLen-1)/2);
                        for (int i=0;i<(paramLen-1)/2;i++)
                        {
                            ((int*)params)[1+2*i]=intParams[1+2*i+0];
                            ((float*)params)[1+2*i+1]=(float)(floatParams[1+2*i+1]);
                        }
                    }
                    double lifeTime=luaToDouble(L,5);
                    int maxItemCount=luaToInt(L,6);
                    float* ambient=nullptr;
                    float* specular=nullptr;
                    float* emission=nullptr;
                    int res=checkOneGeneralInputArgument(L,7,lua_arg_number,3,true,true,&errorString);
                    int okToGo=(res!=-1);
                    if (okToGo)
                    {
                        float ambientC[3];
                        float specularC[3];
                        float emissionC[3];
                        if (res>0)
                        {
                            if (res==2)
                            {
                                getFloatsFromTable(L,7,3,ambientC);
                                ambient=ambientC;
                            }
                            res=checkOneGeneralInputArgument(L,8,lua_arg_number,3,true,true,&errorString);
                            okToGo=(res!=-1);
                            if (okToGo)
                            {
                                if (res>0)
                                {
                                    res=checkOneGeneralInputArgument(L,9,lua_arg_number,3,true,true,&errorString);
                                    okToGo=(res!=-1);
                                    if (okToGo)
                                    {
                                        if (res>0)
                                        {
                                            if (res==2)
                                            {
                                                getFloatsFromTable(L,9,3,specularC);
                                                specular=specularC;
                                            }
                                            res=checkOneGeneralInputArgument(L,10,lua_arg_number,3,true,true,&errorString);
                                            okToGo=(res!=-1);
                                            if (okToGo)
                                            {
                                                if (res>0)
                                                {
                                                    if (res==2)
                                                    {
                                                        getFloatsFromTable(L,10,3,emissionC);
                                                        emission=emissionC;
                                                    }

                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        if (okToGo)
                        {
                            if (!App::currentWorld->simulation->isSimulationRunning())
                                errorString=SIM_ERROR_SIMULATION_NOT_RUNNING;
                            else
                                retVal=CPluginContainer::dyn_addParticleObject(objType,size,massOverVolume,params,lifeTime,maxItemCount,ambient,nullptr,specular,emission);
                        }
                    }
                    delete[] ((char*)params);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simRemoveParticleObject(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.removeParticleObject");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        if (CPluginContainer::dyn_removeParticleObject(luaToInt(L,1))!=0)
            retVal=1;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAddParticleObjectItem(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.addParticleObjectItem");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int particleObjHandle=luaToInt(L,1);
        int d=6+CPluginContainer::dyn_getParticleObjectOtherFloatsPerItem(particleObjHandle);
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,d,true,true,&errorString);
        if (res==2)
        {
            double vertex[20]; // we should have enough here!
            getDoublesFromTable(L,2,d,vertex);

            if (CPluginContainer::dyn_addParticleObjectItem(particleObjHandle,vertex,App::currentWorld->simulation->getSimulationTime())!=0)
                retVal=1;
            else
                errorString=SIM_ERROR_OBJECT_INEXISTANT;
        }
        else
        {
            if (res>=0)
            {
                if (CPluginContainer::dyn_addParticleObjectItem(particleObjHandle,nullptr,App::currentWorld->simulation->getSimulationTime())!=0)
                    retVal=1;
                else
                    errorString=SIM_ERROR_OBJECT_INEXISTANT;
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectSizeFactor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectSizeFactor");

    double retVal=-1.0; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetObjectSizeFactor_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushnumber(L,retVal);
    LUA_END(1);
}

int _simSetInt32Signal(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setInt32Signal");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0))
    {
        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
        retVal=simSetInt32Signal_internal(luaWrap_lua_tostring(L,1),luaToInt(L,2));
        setCurrentScriptInfo_cSide(-1,-1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetInt32Signal(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getInt32Signal");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        int intVal;
        if (simGetInt32Signal_internal(std::string(luaWrap_lua_tostring(L,1)).c_str(),&intVal)==1)
        {
            luaWrap_lua_pushinteger(L,intVal);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simClearInt32Signal(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.clearInt32Signal");

    int retVal=-1; //error
    int res=checkOneGeneralInputArgument(L,1,lua_arg_string,0,true,true,&errorString);
    if (res>=0)
    {
        if (res!=2)
            retVal=simClearInt32Signal_internal(nullptr); // actually deprecated. No need for that
        else
            retVal=simClearInt32Signal_internal(luaWrap_lua_tostring(L,1));
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetFloatSignal(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setFloatSignal");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0))
    {
        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
        retVal=simSetFloatSignal_internal(luaWrap_lua_tostring(L,1),luaToDouble(L,2));
        setCurrentScriptInfo_cSide(-1,-1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetFloatSignal(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getFloatSignal");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        double floatVal;
        if (simGetFloatSignal_internal(std::string(luaWrap_lua_tostring(L,1)).c_str(),&floatVal)==1)
        {
            luaWrap_lua_pushnumber(L,floatVal);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simClearFloatSignal(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.clearFloatSignal");

    int retVal=-1; //error
    int res=checkOneGeneralInputArgument(L,1,lua_arg_string,0,true,true,&errorString);
    if (res>=0)
    {
        if (res!=2)
            retVal=simClearFloatSignal_internal(nullptr); // actually deprecated. No need for that
        else
            retVal=simClearFloatSignal_internal(luaWrap_lua_tostring(L,1));
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetStringSignal(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setStringSignal");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_string,0))
    {
        size_t dataLength;
        const char* data=luaWrap_lua_tolstring(L,2,&dataLength);
        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
        retVal=simSetStringSignal_internal(luaWrap_lua_tostring(L,1),data,int(dataLength));
        setCurrentScriptInfo_cSide(-1,-1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetStringSignal(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getStringSignal");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        int stringLength;
        char* str=simGetStringSignal_internal(std::string(luaWrap_lua_tostring(L,1)).c_str(),&stringLength);
        if (str!=nullptr)
        {
            luaWrap_lua_pushlstring(L,str,stringLength);
            simReleaseBuffer_internal(str);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simClearStringSignal(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.clearStringSignal");

    int retVal=-1; //error
    int res=checkOneGeneralInputArgument(L,1,lua_arg_string,0,true,true,&errorString);
    if (res>=0)
    {
        if (res!=2)
            retVal=simClearStringSignal_internal(nullptr);
        else
            retVal=simClearStringSignal_internal(luaWrap_lua_tostring(L,1));
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetSignalName(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getSignalName");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        char* str=simGetSignalName_internal(luaToInt(L,1),luaToInt(L,2));
        if (str!=nullptr)
        {
            luaWrap_lua_pushstring(L,str);
            simReleaseBuffer_internal(str);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetObjectProperty(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectProperty");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetObjectProperty_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetObjectProperty(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectProperty");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetObjectProperty_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectSpecialProperty(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectSpecialProperty");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetObjectSpecialProperty_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetObjectSpecialProperty(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectSpecialProperty");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetObjectSpecialProperty_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetModelProperty(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getModelProperty");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetModelProperty_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetModelProperty(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setModelProperty");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetModelProperty_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simReadForceSensor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.readForceSensor");

    int retVal=-1;
    double force[3]={0.0,0.0,0.0};
    double torque[3]={0.0,0.0,0.0};
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simReadForceSensor_internal(luaToInt(L,1),force,torque);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    pushDoubleTableOntoStack(L,3,force);
    pushDoubleTableOntoStack(L,3,torque);
    LUA_END(3);
}

int _simGetLightParameters(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getLightParameters");

    int retVal=-1;
    double ambientOld[3]={0.0,0.0,0.0};
    double diffuse[3]={0.0,0.0,0.0};
    double specular[3]={0.0,0.0,0.0};
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetLightParameters_internal(luaToInt(L,1),nullptr,diffuse,specular);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    pushDoubleTableOntoStack(L,3,ambientOld);
    pushDoubleTableOntoStack(L,3,diffuse);
    pushDoubleTableOntoStack(L,3,specular);
    LUA_END(4);
}

int _simSetLightParameters(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setLightParameters");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int objHandle=luaToInt(L,1);
        int state=luaToInt(L,2);
        float* diffuseP=nullptr;
        float* specularP=nullptr;
        float diffuse_[3]={0.0,0.0,0.0};
        float specular_[3]={0.0,0.0,0.0};
        int res=checkOneGeneralInputArgument(L,3,lua_arg_number,3,true,true,&errorString);
        if (res!=-1)
        {
            int res=checkOneGeneralInputArgument(L,4,lua_arg_number,3,true,true,&errorString);
            if (res!=-1)
            {
                if (res==2)
                { // get the data
                    getFloatsFromTable(L,4,3,diffuse_);
                    diffuseP=diffuse_;
                }
                int res=checkOneGeneralInputArgument(L,5,lua_arg_number,3,true,true,&errorString);
                if (res!=-1)
                {
                    if (res==2)
                    { // get the data
                        getFloatsFromTable(L,5,3,specular_);
                        specularP=specular_;
                    }
                    retVal=simSetLightParameters_internal(objHandle,state,nullptr,diffuseP,specularP);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetVelocity(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getVelocity");

    double linVel[3]={0.0,0.0,0.0};
    double angVel[3]={0.0,0.0,0.0};
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        simGetVelocity_internal(luaToInt(L,1),linVel,angVel);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    pushDoubleTableOntoStack(L,3,linVel);
    pushDoubleTableOntoStack(L,3,angVel);
    LUA_END(2);
}

int _simGetObjectVelocity(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectVelocity");

    double linVel[3]={0.0,0.0,0.0};
    double angVel[3]={0.0,0.0,0.0};
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        simGetObjectVelocity_internal(luaToInt(L,1),linVel,angVel);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    pushDoubleTableOntoStack(L,3,linVel);
    pushDoubleTableOntoStack(L,3,angVel);
    LUA_END(2);
}

int _simGetJointVelocity(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getJointVelocity");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double vel;
        int retVal=simGetJointVelocity_internal(luaToInt(L,1),&vel);
        if (retVal!=-1)
        {
            luaWrap_lua_pushnumber(L,vel);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simAddForceAndTorque(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.addForceAndTorque");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double f[3]={0.0,0.0,0.0};
        double t[3]={0.0,0.0,0.0};
        bool err=false;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,3,true,true,&errorString);
        if (res==2)
            getDoublesFromTable(L,2,3,f);
        err=err||(res<0);
        if (!err)
        {
            int res=checkOneGeneralInputArgument(L,3,lua_arg_number,3,true,true,&errorString);
            if (res==2)
                getDoublesFromTable(L,3,3,t);
            err=err||(res<0);
            if (!err)
                retVal=simAddForceAndTorque_internal(luaToInt(L,1),f,t);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAddForce(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.addForce");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,3,lua_arg_number,3))
    {
        double r[3];
        double f[3];
        getDoublesFromTable(L,2,3,r);
        getDoublesFromTable(L,3,3,f);
        retVal=simAddForce_internal(luaToInt(L,1),r,f);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetExplicitHandling(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setExplicitHandling");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetExplicitHandling_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetExplicitHandling(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getExplicitHandling");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetExplicitHandling_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetLinkDummy(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getLinkDummy");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetLinkDummy_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetLinkDummy(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setLinkDummy");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetLinkDummy_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetShapeColor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setShapeColor");

    int retVal=-1; // means error
    int shapeHandle=-1;
    bool ok=false;
    bool correctColors=false;
    if (!checkInputArguments(L,nullptr,lua_arg_number,0))
    { // this section is to guarantee backward compatibility: color values have changed in the release following 3.1.3. So we need to adjust them
        if (checkInputArguments(L,&errorString,lua_arg_string,0))
        {
            std::string txt(luaWrap_lua_tostring(L,1));
            if (txt.compare(0,20,"@backCompatibility1:")==0)
            {
                txt.assign(txt.begin()+20,txt.end());
                if (tt::getValidInt(txt.c_str(),shapeHandle)) // try to extract the original number
                {
                    correctColors=true;
                    ok=true;
                }
                else
                    checkInputArguments(L,&errorString,lua_arg_number,0); // just generate an error
            }
            else
                checkInputArguments(L,&errorString,lua_arg_number,0); // just generate an error
        }
    }
    else
    {
        ok=true;
        shapeHandle=luaToInt(L,1);
    }

    if (ok)
    { // First arg ok
        std::string strTmp;
        char* str=nullptr;
        int colorComponent=0;
        bool err=false;
        bool transformColor=false;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_string,0,false,true,&errorString);
        if (res==2)
        {
            strTmp=luaWrap_lua_tostring(L,2);
            str=(char*)strTmp.c_str();
            transformColor=((strTmp.length()>1)&&(strTmp[0]=='@'));
        }
        err=err||(res<1);
        if (!err)
        {
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,false,false,&errorString);
            if (res==2)
                colorComponent=luaToInt(L,3);
            err=err||(res<2);
            if (!err)
            {
                int floatsInTableExpected=3;
                if (colorComponent==4)
                    floatsInTableExpected=1;
                res=checkOneGeneralInputArgument(L,4,lua_arg_number,floatsInTableExpected,false,false,&errorString);
                if (res==2)
                {
                    if (strTmp.compare("@compound")==0)
                    {
                        CShape* shape=App::currentWorld->sceneObjects->getShapeFromHandle(shapeHandle);
                        if (shape!=nullptr)
                            floatsInTableExpected*=shape->getComponentCount();
                    }
                    std::vector<float> rgbData;
                    rgbData.resize(floatsInTableExpected);
                    getFloatsFromTable(L,4,floatsInTableExpected,&rgbData[0]);
                    if (correctColors&&(colorComponent==0)&&(!transformColor) )
                    {
                        rgbData[0]=(rgbData[0]+0.25)/0.85;
                        rgbData[1]=(rgbData[1]+0.25)/0.85;
                        rgbData[2]=(rgbData[2]+0.25)/0.85;
                        float mx=std::max<float>(std::max<float>(rgbData[0],rgbData[1]),rgbData[2]);
                        if (mx>1.0)
                        {
                            rgbData[0]/=mx;
                            rgbData[1]/=mx;
                            rgbData[2]/=mx;
                        }
                    }
                    retVal=simSetShapeColor_internal(shapeHandle,str,colorComponent,&rgbData[0]);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetShapeColor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getShapeColor");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    { // First arg ok
        int shapeHandle=luaToInt(L,1);
        std::string strTmp;
        char* str=nullptr;
        int colorComponent=0;
        bool err=false;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_string,0,false,true,&errorString);
        if (res==2)
        {
            strTmp=luaWrap_lua_tostring(L,2);
            str=(char*)strTmp.c_str();
        }
        err=err||(res<1);
        if (!err)
        {
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,false,false,&errorString);
            if (res==2)
                colorComponent=luaToInt(L,3);
            err=err||(res<2);
            if (!err)
            {
                int res=1;
                if (strTmp.compare("@compound")==0)
                {
                    CShape* shape=App::currentWorld->sceneObjects->getShapeFromHandle(shapeHandle);
                    if (shape!=nullptr)
                        res=shape->getComponentCount();
                }
                if (colorComponent!=4)
                    res*=3;
                std::vector<float> rgbData;
                rgbData.resize(res);
                retVal=simGetShapeColor_internal(shapeHandle,str,colorComponent,&rgbData[0]);
                if (retVal>0)
                {
                    luaWrap_lua_pushinteger(L,retVal);
                    pushFloatTableOntoStack(L,res,&rgbData[0]);
                    LUA_END(2);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    double dummy[3]={0.0,0.0,0.0};
    pushDoubleTableOntoStack(L,3,dummy);
    LUA_END(2);
}

int _simSetObjectColor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectColor");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,3))
    {
        float col[3];
        getFloatsFromTable(L,4,3,col);
        int res=simSetObjectColor_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3),col);
        if (res>=0)
        {
            luaWrap_lua_pushboolean(L,res!=0);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetObjectColor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectColor");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        float col[3];
        int res=simGetObjectColor_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3),col);
        if (res>0)
        {
            pushFloatTableOntoStack(L,3,col);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simResetDynamicObject(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.resetDynamicObject");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simResetDynamicObject_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetJointMode(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setJointMode");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetJointMode_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetJointMode(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getJointMode");

    int retVal=-1; // means error
    int options=0;
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetJointMode_internal(luaToInt(L,1),&options);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    luaWrap_lua_pushinteger(L,options);
    LUA_END(2);
}

int _simSerialOpen(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim._serialOpen");

    int retVal=-1; // means error
#ifdef SIM_WITH_SERIAL
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0))
    {
        size_t dataLength;
        const char* portName=luaWrap_lua_tolstring(L,1,&dataLength);
        int baudrate=luaToInt(L,2);
        retVal=App::worldContainer->serialPortContainer->serialPortOpen(true,portName,baudrate);
    }
#endif

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSerialClose(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim._serialClose");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        simSerialClose_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSerialSend(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.serialSend");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
    {
        size_t dataLength;
        char* data=(char*)luaWrap_lua_tolstring(L,2,&dataLength);
        retVal=simSerialSend_internal(luaToInt(L,1),data,(int)dataLength);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSerialRead(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim._serialRead");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        size_t maxLength=(size_t)luaToInt(L,2);
        std::string data;
        data.resize(maxLength);
        int nb=simSerialRead_internal(luaToInt(L,1),(char*)data.c_str(),(int)maxLength);
        if (nb>0)
        {
            luaWrap_lua_pushlstring(L,data.c_str(),nb);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSerialCheck(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.serialCheck");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simSerialCheck_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetContactInfo(luaWrap_lua_State* L)
{ // problematic for remote API and in general: function returns different return value count
    TRACE_LUA_API;
    LUA_START("sim.getContactInfo");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int collidingObjects[2];
        double contactInfo[9];
        int index=luaToInt(L,3);
        if (index<sim_handleflag_extended)
            index+=sim_handleflag_extended;
        int res=simGetContactInfo_internal(luaToInt(L,1),luaToInt(L,2),index,collidingObjects,contactInfo);
        if (res==1)
        {
            pushIntTableOntoStack(L,2,collidingObjects);
            pushDoubleTableOntoStack(L,3,contactInfo);
            pushDoubleTableOntoStack(L,3,contactInfo+3);
            pushDoubleTableOntoStack(L,3,contactInfo+6);
            LUA_END(4);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simAuxiliaryConsoleOpen(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.auxiliaryConsoleOpen");

    int retVal=-1; // Error
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0,lua_arg_number,0))
    {
        int mode=luaToInt(L,3);
        int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        CScriptObject* itScrObj=App::worldContainer->getScriptFromHandle(currentScriptID);
        if ( (itScrObj->getScriptType()==sim_scripttype_mainscript)||(itScrObj->getScriptType()==sim_scripttype_childscript) )
        { // Add-ons and customization scripts do not have this restriction
            mode|=1;
        }
        if ( (itScrObj->getScriptType()!=sim_scripttype_sandboxscript)&&(itScrObj->getScriptType()!=sim_scripttype_addonscript) )
        { // Add-ons and sandbox scripts do not have this restriction
            mode|=16;
            mode-=16;
        }
        int* p=nullptr;
        int* s=nullptr;
        float* tc=nullptr;
        float* bc=nullptr;
        int res=checkOneGeneralInputArgument(L,4,lua_arg_number,2,true,true,&errorString);
        if (res>=0)
        {
            int pp[2]={0,0};
            if (res==2)
            {
                getIntsFromTable(L,4,2,pp);
                p=pp;
            }
            res=checkOneGeneralInputArgument(L,5,lua_arg_number,2,true,true,&errorString);
            if (res>=0)
            {
                int ss[2]={0,0};
                if (res==2)
                {
                    getIntsFromTable(L,5,2,ss);
                    s=ss;
                }
                res=checkOneGeneralInputArgument(L,6,lua_arg_number,3,true,true,&errorString);
                if (res>=0)
                {
                    float tc_[3]={0.0,0.0,0.0};
                    if (res==2)
                    {
                        getFloatsFromTable(L,6,3,tc_);
                        tc=tc_;
                    }
                    res=checkOneGeneralInputArgument(L,7,lua_arg_number,3,true,true,&errorString);
                    if (res>=0)
                    {
                        float bc_[3]={0.0,0.0,0.0};
                        if (res==2)
                        {
                            getFloatsFromTable(L,7,3,bc_);
                            bc=bc_;
                        }
                        retVal=simAuxiliaryConsoleOpen_internal(luaWrap_lua_tostring(L,1),luaToInt(L,2),mode,p,s,tc,bc);
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAuxiliaryConsoleClose(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.auxiliaryConsoleClose");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simAuxiliaryConsoleClose_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAuxiliaryConsoleShow(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.auxiliaryConsoleShow");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_bool,0))
        retVal=simAuxiliaryConsoleShow_internal(luaToInt(L,1),luaToBool(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAuxiliaryConsolePrint(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.auxiliaryConsolePrint");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int res=checkOneGeneralInputArgument(L,2,lua_arg_string,0,false,true,&errorString);
        if (res==1)
            retVal=simAuxiliaryConsolePrint_internal(luaToInt(L,1),nullptr);
        if (res==2)
            retVal=simAuxiliaryConsolePrint_internal(luaToInt(L,1),luaWrap_lua_tostring(L,2));
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simImportShape(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.importShape");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int fileType=luaToInt(L,1);
        std::string pathAndFilename(luaWrap_lua_tostring(L,2));
        int options=luaToInt(L,3);
        double identicalVerticeTolerance=luaToDouble(L,4);
        double scalingFactor=luaToDouble(L,5);
        retVal=simImportShape_internal(fileType,pathAndFilename.c_str(),options,identicalVerticeTolerance,scalingFactor);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simImportMesh(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.importMesh");

    int retValCnt=1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int fileType=luaToInt(L,1);
        std::string pathAndFilename(luaWrap_lua_tostring(L,2));
        int options=luaToInt(L,3);
        double identicalVerticeTolerance=luaToDouble(L,4);
        double scalingFactor=luaToDouble(L,5);
        double** vertices;
        int* verticesSizes;
        int** indices;
        int* indicesSizes;
        int elementCount=simImportMesh_internal(fileType,pathAndFilename.c_str(),options,identicalVerticeTolerance,scalingFactor,&vertices,&verticesSizes,&indices,&indicesSizes,nullptr,nullptr);
        if (elementCount>0)
        {
            // Vertices:
            luaWrap_lua_newtable(L);
            int tablePos=luaWrap_lua_gettop(L);
            for (int i=0;i<elementCount;i++)
            {
                luaWrap_lua_newtable(L);
                int smallTablePos=luaWrap_lua_gettop(L);
                for (int j=0;j<verticesSizes[i];j++)
                {
                    luaWrap_lua_pushnumber(L,vertices[i][j]);
                    luaWrap_lua_rawseti(L,smallTablePos,j+1);
                }
                luaWrap_lua_rawseti(L,tablePos,i+1);
            }
            // Indices:
            luaWrap_lua_newtable(L);
            tablePos=luaWrap_lua_gettop(L);
            for (int i=0;i<elementCount;i++)
            {
                luaWrap_lua_newtable(L);
                int smallTablePos=luaWrap_lua_gettop(L);
                for (int j=0;j<indicesSizes[i];j++)
                {
                    luaWrap_lua_pushinteger(L,indices[i][j]);
                    luaWrap_lua_rawseti(L,smallTablePos,j+1);
                }
                luaWrap_lua_rawseti(L,tablePos,i+1);
            }
            retValCnt=2;

            for (int i=0;i<elementCount;i++)
            {
                delete[] indices[i];
                delete[] vertices[i];
            }
            delete[] indicesSizes;
            delete[] indices;
            delete[] verticesSizes;
            delete[] vertices;
            LUA_END(retValCnt);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simExportMesh(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.exportMesh");

    int retVal=-1; // indicates an error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0,lua_arg_number,0,lua_arg_number,0))
    {
        int fileType=luaToInt(L,1);
        std::string pathAndFilename(luaWrap_lua_tostring(L,2));
        int options=luaToInt(L,3);
        double scalingFactor=luaToDouble(L,4);
        int elementCount=15487;
        if ( (luaWrap_lua_gettop(L)>=6)&&luaWrap_lua_istable(L,5)&&luaWrap_lua_istable(L,6) )
        {
            int ve=(int)luaWrap_lua_rawlen(L,5);
            int ie=(int)luaWrap_lua_rawlen(L,6);
            elementCount=std::min<int>(ve,ie);
        }
        if ( (checkOneGeneralInputArgument(L,5,lua_arg_table,elementCount,false,false,&errorString)==2)&&
                (checkOneGeneralInputArgument(L,6,lua_arg_table,elementCount,false,false,&errorString)==2) )
        {
            double** vertices;
            int* verticesSizes;
            int** indices;
            int* indicesSizes;
            vertices=new double*[elementCount];
            verticesSizes=new int[elementCount];
            indices=new int*[elementCount];
            indicesSizes=new int[elementCount];
            // Following needed if we break before (because one table in a table is not correct (or not a table)):
            for (int i=0;i<elementCount;i++)
            {
                vertices[i]=nullptr;
                verticesSizes[i]=0;
                indices[i]=nullptr;
                indicesSizes[i]=0;
            }

            bool error=false;
            for (int i=0;i<elementCount;i++)
            {
                if (!error)
                {
                    luaWrap_lua_rawgeti(L,5,i+1);
                    if (luaWrap_lua_istable(L,-1))
                    {
                        int vl=(int)luaWrap_lua_rawlen(L,-1);
                        if (checkOneGeneralInputArgument(L,luaWrap_lua_gettop(L),lua_arg_number,vl,false,false,&errorString)==2)
                        {
                            verticesSizes[i]=vl;
                            vertices[i]=new double[vl];
                            getDoublesFromTable(L,luaWrap_lua_gettop(L),vl,vertices[i]);
                        }
                        else
                            error=true;
                    }
                    else
                        error=true;
                    luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                }
                if (!error)
                {
                    luaWrap_lua_rawgeti(L,6,i+1);
                    if (luaWrap_lua_istable(L,-1))
                    {
                        int vl=(int)luaWrap_lua_rawlen(L,-1);
                        if (checkOneGeneralInputArgument(L,luaWrap_lua_gettop(L),lua_arg_number,vl,false,false,&errorString)==2)
                        {
                            indicesSizes[i]=vl;
                            indices[i]=new int[vl];
                            getIntsFromTable(L,luaWrap_lua_gettop(L),vl,indices[i]);
                        }
                        else
                            error=true;
                    }
                    else
                        error=true;
                    luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                }
            }

            if (!error)
                retVal=simExportMesh_internal(fileType,pathAndFilename.c_str(),options,scalingFactor,elementCount,(const double**)vertices,verticesSizes,(const int**)indices,indicesSizes,nullptr,nullptr);

            for (int i=0;i<elementCount;i++)
            {
                delete[] vertices[i];
                delete[] indices[i];
            }
            delete[] indicesSizes;
            delete[] indices;
            delete[] verticesSizes;
            delete[] vertices;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}



int _simCreateMeshShape(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.createMeshShape");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int options=luaToInt(L,1);
        double shadingAngle=luaToDouble(L,2);

        int vl=2;
        int il=2;
        if ( (luaWrap_lua_gettop(L)>=4)&&luaWrap_lua_istable(L,3)&&luaWrap_lua_istable(L,4) )
        {
            vl=(int)luaWrap_lua_rawlen(L,3);
            il=(int)luaWrap_lua_rawlen(L,4);
        }
        int res=checkOneGeneralInputArgument(L,3,lua_arg_number,vl,false,false,&errorString);
        if (res==2)
        {
            res=checkOneGeneralInputArgument(L,4,lua_arg_number,il,false,false,&errorString);
            if (res==2)
            {
                double* vertices=new double[vl];
                int* indices=new int[il];
                getDoublesFromTable(L,3,vl,vertices);
                getIntsFromTable(L,4,il,indices);
                retVal=simCreateMeshShape_internal(options,shadingAngle,vertices,vl,indices,il,nullptr);
                delete[] indices;
                delete[] vertices;
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetShapeMesh(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getShapeMesh");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double* vertices;
        int verticesSize;
        int* indices;
        int indicesSize;
        double* normals;
        int result=simGetShapeMesh_internal(luaToInt(L,1),&vertices,&verticesSize,&indices,&indicesSize,&normals);
        if (result>0)
        {
            pushDoubleTableOntoStack(L,verticesSize,vertices);
            pushIntTableOntoStack(L,indicesSize,indices);
            pushDoubleTableOntoStack(L,indicesSize*3,normals);
            delete[] vertices;
            delete[] indices;
            delete[] normals;
            LUA_END(3);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simCreatePrimitiveShape(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.createPrimitiveShape");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_number,3))
    {
        int primitiveType=luaToInt(L,1);
        double sizes[3];
        getDoublesFromTable(L,2,3,sizes);
        int res=checkOneGeneralInputArgument(L,3,lua_arg_integer,0,true,false,&errorString);
        if (res>=0)
        {
            int options=0;
            if (res==2)
                options=luaToInt(L,3);
            retVal=simCreatePrimitiveShape_internal(primitiveType,sizes,options);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCreateHeightfieldShape(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.createHeightfieldShape");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int options=luaToInt(L,1);
        double shadingAngle=luaToDouble(L,2);
        int xPointCount=luaToInt(L,3);
        int yPointCount=luaToInt(L,4);
        double xSize=luaToDouble(L,5);
        int res=checkOneGeneralInputArgument(L,6,lua_arg_number,xPointCount*yPointCount,false,false,&errorString);
        if (res==2)
        {
            double* heights=new double[xPointCount*yPointCount];
            getDoublesFromTable(L,6,xPointCount*yPointCount,heights);
            retVal=simCreateHeightfieldShape_internal(options,shadingAngle,xPointCount,yPointCount,xSize,heights);
            delete[] heights;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}


int _simCreateJoint(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.createJoint");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int jointType=luaToInt(L,1);
        int jointMode=luaToInt(L,2);
        int options=luaToInt(L,3);
        double* sizes=nullptr;
        double* colorA=nullptr;
        double* colorB=nullptr;
        double s[2];
        double cA[12];
        double cB[12];
        int res=checkOneGeneralInputArgument(L,4,lua_arg_number,2,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getDoublesFromTable(L,4,2,s);
                sizes=s;
            }
            res=checkOneGeneralInputArgument(L,5,lua_arg_number,12,true,true,&errorString);
            if (res>=0)
            {
                if (res==2)
                {
                    getDoublesFromTable(L,5,12,cA);
                    colorA=cA;
                }
                res=checkOneGeneralInputArgument(L,6,lua_arg_number,12,true,true,&errorString);
                if (res>=0)
                {
                    if (res==2)
                    {
                        getDoublesFromTable(L,6,12,cB);
                        colorB=cB;
                    }
                    retVal=simCreateJoint_internal(jointType,jointMode,options,sizes,colorA,colorB);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCreateDummy(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.createDummy");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double size=luaToDouble(L,1);
        float* color=nullptr;
        float c[12]={0.0,0.0,0.0,0.0,0.0,0.0,0.25,0.25,0.25,0.0,0.0,0.0};
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,3,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getFloatsFromTable(L,2,3,c);
                color=c;
            }
            retVal=simCreateDummy_internal(size,color);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCreateProximitySensor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.createProximitySensor");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,8,lua_arg_number,15))
    {
        int sensorType=luaToInt(L,1);
        int subType=luaToInt(L,2);
        int options=luaToInt(L,3);
        int intParams[8];
        double floatParams[15];
        getIntsFromTable(L,4,8,intParams);
        getDoublesFromTable(L,5,15,floatParams);

        double* color=nullptr;
        double c[48];
        int res=checkOneGeneralInputArgument(L,6,lua_arg_number,48,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getDoublesFromTable(L,6,48,c);
                color=c;
            }
            retVal=simCreateProximitySensor_internal(sensorType,subType,options,intParams,floatParams,color);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCreateForceSensor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.createForceSensor");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,5,lua_arg_number,5))
    {
        int options=luaToInt(L,1);
        int intParams[5];
        double floatParams[5];
        getIntsFromTable(L,2,5,intParams);
        getDoublesFromTable(L,3,5,floatParams);

        double* color=nullptr;
        double c[24];
        int res=checkOneGeneralInputArgument(L,4,lua_arg_number,24,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getDoublesFromTable(L,4,24,c);
                color=c;
            }
            retVal=simCreateForceSensor_internal(options,intParams,floatParams,color);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCreateVisionSensor(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.createVisionSensor");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,4,lua_arg_number,11))
    {
        int options=luaToInt(L,1);
        int intParams[4];
        double floatParams[11];
        getIntsFromTable(L,2,4,intParams);
        getDoublesFromTable(L,3,11,floatParams);

        double* color=nullptr;
        double c[48];
        int res=checkOneGeneralInputArgument(L,4,lua_arg_number,48,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getDoublesFromTable(L,4,48,c);
                color=c;
            }
            retVal=simCreateVisionSensor_internal(options,intParams,floatParams,color);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simFloatingViewAdd(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.floatingViewAdd");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
        retVal=simFloatingViewAdd_internal(luaToDouble(L,1),luaToDouble(L,2),luaToDouble(L,3),luaToDouble(L,4),luaToInt(L,5));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simFloatingViewRemove(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.floatingViewRemove");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simFloatingViewRemove_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAdjustView(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.adjustView");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int res=checkOneGeneralInputArgument(L,4,lua_arg_string,0,true,true,&errorString);
        if (res>=0)
        {
            char* txt=nullptr;
            if (res==2)
                txt=(char*)luaWrap_lua_tostring(L,4);
            if ( (txt!=nullptr)&&(strlen(txt)>0) )
                retVal=simAdjustView_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3),txt);
            else
                retVal=simAdjustView_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3),nullptr);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCameraFitToView(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.cameraFitToView");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int* objPtr=nullptr;
        int options=0;
        double scaling=1.0;
        int tableLen=2;
        if (luaWrap_lua_istable(L,2))
        {
            tableLen=int(luaWrap_lua_rawlen(L,2));
            int* buffer=new int[tableLen];
            objPtr=buffer;
            getIntsFromTable(L,2,tableLen,buffer);
        }
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,tableLen,true,true,&errorString);
        if (res>=0)
        {
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if (res>=0)
            {
                if (res==2)
                    options=luaToInt(L,3);
                res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
                if (res>=0)
                {
                    if (res==2)
                        scaling=luaToDouble(L,4);
                    retVal=simCameraFitToView_internal(luaToInt(L,1),tableLen,objPtr,options,scaling);
                }
            }
        }
        delete[] objPtr;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAnnounceSceneContentChange(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.announceSceneContentChange");

    int retVal=simAnnounceSceneContentChange_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectInt32Param(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectInt32Param");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int param;
        int retVal=simGetObjectInt32Param_internal(luaToInt(L,1),luaToInt(L,2),&param);
        if (retVal>0)
        {
            luaWrap_lua_pushinteger(L,param);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectInt32Param(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectInt32Param");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetObjectInt32Param_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}


int _simGetObjectFloatParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectFloatParam");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        double param;
        int retVal=simGetObjectFloatParam_internal(luaToInt(L,1),luaToInt(L,2),&param);
        if (retVal>0)
        {
            luaWrap_lua_pushnumber(L,param);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectFloatParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectFloatParam");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetObjectFloatParam_internal(luaToInt(L,1),luaToInt(L,2),luaToDouble(L,3));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectFloatArrayParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectFloatArrayParam");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int s;
        double* params=simGetObjectFloatArrayParam_internal(luaToInt(L,1),luaToInt(L,2),&s);
        if (params!=nullptr)
        {
            pushDoubleTableOntoStack(L,s,params);
            delete [] params;
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectFloatArrayParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectFloatArrayParam");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,1))
    {
        size_t cnt=luaWrap_lua_rawlen(L,3);
        std::vector<double> arr;
        arr.resize(cnt);
        getDoublesFromTable(L,3,cnt,&arr[0]);
        retVal=simSetObjectFloatArrayParam_internal(luaToInt(L,1),luaToInt(L,2),&arr[0],int(cnt));
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectStringParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectStringParam");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int paramLength;
        char* strBuff=simGetObjectStringParam_internal(luaToInt(L,1),luaToInt(L,2),&paramLength);
        if (strBuff!=nullptr)
        {
            luaWrap_lua_pushlstring(L,strBuff,paramLength);
            delete[] strBuff;
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectStringParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectStringParam");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_string,0))
    {
        size_t dataLength;
        char* data=(char*)luaWrap_lua_tolstring(L,3,&dataLength);
        retVal=simSetObjectStringParam_internal(luaToInt(L,1),luaToInt(L,2),data,(int)dataLength);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetScriptInt32Param(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getScriptInt32Param");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int param;
        int scriptID=luaToInt(L,1);
        if (scriptID==sim_handle_self)
            scriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        int retVal=simGetScriptInt32Param_internal(scriptID,luaToInt(L,2),&param);
        if (retVal>0)
        {
            luaWrap_lua_pushinteger(L,param);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetScriptInt32Param(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setScriptInt32Param");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int scriptID=luaToInt(L,1);
        if (scriptID==sim_handle_self)
            scriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        retVal=simSetScriptInt32Param_internal(scriptID,luaToInt(L,2),luaToInt(L,3));
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetScriptStringParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getScriptStringParam");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int paramLength;
        int scriptID=luaToInt(L,1);
        if (scriptID==sim_handle_self)
            scriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        char* strBuff=simGetScriptStringParam_internal(scriptID,luaToInt(L,2),&paramLength);
        if (strBuff!=nullptr)
        {
            luaWrap_lua_pushlstring(L,strBuff,paramLength);
            delete[] strBuff;
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetScriptStringParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setScriptStringParam");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_string,0))
    {
        int scriptID=luaToInt(L,1);
        if (scriptID==sim_handle_self)
            scriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        size_t dataLength;
        char* data=(char*)luaWrap_lua_tolstring(L,3,&dataLength);
        retVal=simSetScriptStringParam_internal(scriptID,luaToInt(L,2),data,(int)dataLength);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetRotationAxis(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getRotationAxis");

    if (checkInputArguments(L,&errorString,lua_arg_number,7,lua_arg_number,7))
    {
        double inM0[12];
        double inM1[12];

        C4X4Matrix mStart;
        C4X4Matrix mGoal;
        if (luaWrap_lua_rawlen(L,1)>=12)
        { // we have a matrix
            getDoublesFromTable(L,1,12,inM0);
            getDoublesFromTable(L,2,12,inM1);
            mStart.setData(inM0);
            mGoal.setData(inM1);
        }
        else
        { // we have a pose
            getDoublesFromTable(L,1,7,inM0);
            getDoublesFromTable(L,2,7,inM1);
            C7Vector p;
            p.setData(inM0,true);
            mStart=p.getMatrix();
            p.setData(inM1,true);
            mGoal=p.getMatrix();
        }

        // Following few lines taken from the quaternion interpolation part:
        C4Vector AA(mStart.M.getQuaternion());
        C4Vector BB(mGoal.M.getQuaternion());
        if (AA(0)*BB(0)+AA(1)*BB(1)+AA(2)*BB(2)+AA(3)*BB(3)<0.0)
            AA=AA*-1.0;
        C4Vector r((AA.getInverse()*BB).getAngleAndAxis());

        C3Vector v(r(1),r(2),r(3));
        v=AA*v;

        double axis[3];
        axis[0]=v(0);
        axis[1]=v(1);
        axis[2]=v(2);
        double l=sqrt(v(0)*v(0)+v(1)*v(1)+v(2)*v(2));
        if (l!=0.0)
        {
            axis[0]/=l;
            axis[1]/=l;
            axis[2]/=l;
        }

        pushDoubleTableOntoStack(L,3,axis);
        luaWrap_lua_pushnumber(L,r(0));
        LUA_END(2);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simRotateAroundAxis(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.rotateAroundAxis");

    if (checkInputArguments(L,&errorString,lua_arg_number,7,lua_arg_number,3,lua_arg_number,3,lua_arg_number,0))
    {
        double inM[12];
        double axis[3];
        double ppos[3];
        double outM[12];
        getDoublesFromTable(L,2,3,axis);
        getDoublesFromTable(L,3,3,ppos);

        C7Vector tr;
        if (luaWrap_lua_rawlen(L,1)>=12)
        { // we have a matrix
            getDoublesFromTable(L,1,12,inM);
            C4X4Matrix m;
            m.setData(inM);
            tr=m.getTransformation();
        }
        else
        { // we have a pose
            getDoublesFromTable(L,1,7,inM);
            tr.setData(inM,true);
        }
        C3Vector ax(axis);
        C3Vector pos(ppos);

        double alpha=-atan2(ax(1),ax(0));
        double beta=atan2(-sqrt(ax(0)*ax(0)+ax(1)*ax(1)),ax(2));
        tr.X-=pos;
        C7Vector r;
        r.X.clear();
        r.Q.setEulerAngles(0.0,0.0,alpha);
        tr=r*tr;
        r.Q.setEulerAngles(0.0,beta,0.0);
        tr=r*tr;
        r.Q.setEulerAngles(0.0,0.0,luaToDouble(L,4));
        tr=r*tr;
        r.Q.setEulerAngles(0.0,-beta,0.0);
        tr=r*tr;
        r.Q.setEulerAngles(0.0,0.0,-alpha);
        tr=r*tr;
        tr.X+=pos;
        if (luaWrap_lua_rawlen(L,1)>=12)
        { // we have a matrix
            tr.getMatrix().getData(outM);
            pushDoubleTableOntoStack(L,12,outM);
        }
        else
        { // we have a pose
            tr.getData(outM,true);
            pushDoubleTableOntoStack(L,7,outM);
        }
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetJointForce(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getJointForce");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double jointF[1];
        if (simGetJointForce_internal(luaToInt(L,1),jointF)>0)
        {
            luaWrap_lua_pushnumber(L,jointF[0]);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simPersistentDataWrite(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.persistentDataWrite");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_string,0))
    {
        int options=0;
        int res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                options=luaToInt(L,3);
            size_t dataLength;
            char* data=(char*)luaWrap_lua_tolstring(L,2,&dataLength);
            retVal=simPersistentDataWrite_internal(std::string(luaWrap_lua_tostring(L,1)).c_str(),data,(int)dataLength,options);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simPersistentDataRead(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.persistentDataRead");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        int stringLength;
        char* str=simPersistentDataRead_internal(std::string(luaWrap_lua_tostring(L,1)).c_str(),&stringLength);

        if (str!=nullptr)
        {
            luaWrap_lua_pushlstring(L,str,stringLength);
            simReleaseBuffer_internal(str);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simIsHandle(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.isHandle");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int objType=-1;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                objType=luaToInt(L,2);
            int res=simIsHandle_internal(luaToInt(L,1),objType);
            if (res>=0)
            {
                luaWrap_lua_pushboolean(L,res!=0);
                LUA_END(1);
            }
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simRuckigPos(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.ruckigPos");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int dofs=luaToInt(L,1);
        double timeStep=luaToDouble(L,2);
        int flags=luaToInt(L,3);
        int maxVelAccelJerkCnt=dofs*3;
        if ( (flags>=0)&&((flags&sim_ruckig_minvel)!=0) )
            maxVelAccelJerkCnt+=dofs;
        if ( (flags>=0)&&((flags&sim_ruckig_minaccel)!=0) )
            maxVelAccelJerkCnt+=dofs;
        if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,dofs*3,lua_arg_number,maxVelAccelJerkCnt,lua_arg_number,dofs,lua_arg_number,dofs*2))
        {
            std::vector<double> currentPosVelAccel;
            currentPosVelAccel.resize(dofs*3);
            getDoublesFromTable(L,4,dofs*3,&currentPosVelAccel[0]);

            std::vector<double> maxVelAccelJerk;
            maxVelAccelJerk.resize(maxVelAccelJerkCnt);
            getDoublesFromTable(L,5,maxVelAccelJerkCnt,&maxVelAccelJerk[0]);

            std::vector<double> minMaxVel;
            std::vector<double> minMaxAccel;
            std::vector<double> maxJerk;
            for (size_t i=0;i<dofs;i++)
            {
                minMaxVel.push_back(maxVelAccelJerk[i]);
                minMaxAccel.push_back(maxVelAccelJerk[dofs+i]);
                maxJerk.push_back(maxVelAccelJerk[2*dofs+i]);
            }
            size_t off=3*dofs;
            if ( (flags>=0)&&((flags&sim_ruckig_minvel)!=0) )
            {
                for (size_t i=0;i<dofs;i++)
                    minMaxVel.push_back(maxVelAccelJerk[off+i]);
                off+=dofs;
            }
            if ( (flags>=0)&&((flags&sim_ruckig_minaccel)!=0) )
            {
                for (size_t i=0;i<dofs;i++)
                    minMaxAccel.push_back(maxVelAccelJerk[off+i]);
                off+=dofs;
            }

            std::vector<char> selection;
            selection.resize(dofs);
            getCharBoolsFromTable(L,6,dofs,&selection[0]);

            std::vector<double> targetPosVel;
            targetPosVel.resize(dofs*2);
            getDoublesFromTable(L,7,dofs*2,&targetPosVel[0]);

            setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
            int retVal=simRuckigPos_internal(dofs,timeStep,flags,&currentPosVelAccel[0],&currentPosVelAccel[dofs],&currentPosVelAccel[dofs*2],&minMaxVel[0],&minMaxAccel[0],&maxJerk[0],(bool*)selection.data(),&targetPosVel[0],&targetPosVel[dofs],nullptr,nullptr);
            setCurrentScriptInfo_cSide(-1,-1);

            if (retVal>=0)
            {
                luaWrap_lua_pushinteger(L,retVal);
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simRuckigVel(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.ruckigVel");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int dofs=luaToInt(L,1);
        double timeStep=luaToDouble(L,2);
        int flags=luaToInt(L,3);
        int maxAccelJerkCnt=dofs*2;
        if ( (flags>=0)&&((flags&sim_ruckig_minaccel)!=0) )
            maxAccelJerkCnt+=dofs;
        if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,dofs*3,lua_arg_number,maxAccelJerkCnt,lua_arg_number,dofs,lua_arg_number,dofs))
        {
            std::vector<double> currentPosVelAccel;
            currentPosVelAccel.resize(dofs*3);
            getDoublesFromTable(L,4,dofs*3,&currentPosVelAccel[0]);

            std::vector<double> maxAccelJerk;
            maxAccelJerk.resize(maxAccelJerkCnt);
            getDoublesFromTable(L,5,maxAccelJerkCnt,&maxAccelJerk[0]);

            std::vector<double> minMaxAccel;
            std::vector<double> maxJerk;
            for (size_t i=0;i<dofs;i++)
            {
                minMaxAccel.push_back(maxAccelJerk[i]);
                maxJerk.push_back(maxAccelJerk[dofs+i]);
            }
            size_t off=2*dofs;
            if ( (flags>=0)&&((flags&sim_ruckig_minaccel)!=0) )
            {
                for (size_t i=0;i<dofs;i++)
                    minMaxAccel.push_back(maxAccelJerk[off+i]);
                off+=dofs;
            }

            std::vector<char> selection;
            selection.resize(dofs);
            getCharBoolsFromTable(L,6,dofs,&selection[0]);

            std::vector<double> targetVel;
            targetVel.resize(dofs);
            getDoublesFromTable(L,7,dofs,&targetVel[0]);

            setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
            int retVal=simRuckigVel_internal(dofs,timeStep,flags,&currentPosVelAccel[0],&currentPosVelAccel[dofs],&currentPosVelAccel[dofs*2],&minMaxAccel[0],&maxJerk[0],(bool*)selection.data(),&targetVel[0],nullptr,nullptr);
            setCurrentScriptInfo_cSide(-1,-1);

            if (retVal>=0)
            {
                luaWrap_lua_pushinteger(L,retVal);
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simRuckigStep(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.ruckigStep");
    int retVal=-1;
    std::vector<double> newPosVelAccel;
    double syncTime=0.0;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        double timeStep=luaToDouble(L,2);
        int dofs=CPluginContainer::ruckigPlugin_dofs(handle);
        if (dofs<0)
            dofs=1; // will be caught later down
        newPosVelAccel.resize(dofs*3);
        retVal=simRuckigStep_internal(handle,timeStep,&newPosVelAccel[0],&newPosVelAccel[dofs],&newPosVelAccel[dofs*2],&syncTime,nullptr,nullptr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    if (newPosVelAccel.size()>0)
        pushDoubleTableOntoStack(L,newPosVelAccel.size(),&newPosVelAccel[0]);
    else
        pushIntTableOntoStack(L,0,nullptr); // empty table
    luaWrap_lua_pushnumber(L,syncTime);
    LUA_END(3);
}

int _simRuckigRemove(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.ruckigRemove");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        int retVal=simRuckigRemove_internal(handle);
        if (retVal>=0)
        {
            luaWrap_lua_pushinteger(L,retVal);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetObjectQuaternion(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectQuaternion");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        double coord[4];
        if (simGetObjectQuaternion_internal(luaToInt(L,1),luaToInt(L,2),coord)==1)
        {
            pushDoubleTableOntoStack(L,4,coord);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectQuaternion(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setObjectQuaternion");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,4))
    {
        double coord[4];
        getDoublesFromTable(L,3,4,coord);
        retVal=simSetObjectQuaternion_internal(luaToInt(L,1),luaToInt(L,2),coord);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simBuildMatrixQ(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.buildMatrixQ");

    if (checkInputArguments(L,&errorString,lua_arg_number,3,lua_arg_number,4))
    {
        double arr[12];
        double pos[3];
        double quaternion[4];
        getDoublesFromTable(L,1,3,pos);
        getDoublesFromTable(L,2,4,quaternion);
        if (simBuildMatrixQ_internal(pos,quaternion,arr)==1)
        {
            pushDoubleTableOntoStack(L,12,arr);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetQuaternionFromMatrix(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getQuaternionFromMatrix");

    if (checkInputArguments(L,&errorString,lua_arg_number,12))
    {
        double arr[12];
        double quaternion[4];
        getDoublesFromTable(L,1,12,arr);
        if (simGetQuaternionFromMatrix_internal(arr,quaternion)==1)
        {
            pushDoubleTableOntoStack(L,4,quaternion);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simLoadModule(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.loadModule");

    int retVal=-3; // means plugin could not be loaded
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_string,0))
    {
        std::string fileAndPath(luaWrap_lua_tostring(L,1));
        std::string pluginName(luaWrap_lua_tostring(L,2));
        retVal=simLoadModule_internal(fileAndPath.c_str(),pluginName.c_str());
        if (retVal>=0)
        {
            CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
            it->registerNewFunctions_lua(); // otherwise we can only use the custom Lua functions that the plugin registers after this script has re-initialized!
            it->registerPluginFunctions();
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simUnloadModule(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.unloadModule");

    int retVal=0; // error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simUnloadModule_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCallScriptFunction(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.callScriptFunction");

    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0))
    {
        std::string funcAndScriptName(luaWrap_lua_tostring(L,1));
        int scriptHandleOrType=luaToInt(L,2);

        std::string scriptDescription;
        std::string funcName;
        size_t p=funcAndScriptName.find('@');
        if (p!=std::string::npos)
        {
            scriptDescription.assign(funcAndScriptName.begin()+p+1,funcAndScriptName.end());
            funcName.assign(funcAndScriptName.begin(),funcAndScriptName.begin()+p);
        }
        else
            funcName=funcAndScriptName;

        CScriptObject* script=nullptr;
        if (scriptHandleOrType>=SIM_IDSTART_LUASCRIPT)
        { // script is identified by its ID
            script=App::worldContainer->getScriptFromHandle(scriptHandleOrType);
        }
        else
        { // the script is identified by its type sometimes also by its name
            if (scriptHandleOrType==sim_scripttype_mainscript)
                script=App::currentWorld->embeddedScriptContainer->getMainScript();
            if ( (scriptHandleOrType==sim_scripttype_childscript)||(scriptHandleOrType==sim_scripttype_customizationscript) )
            {
                int objId=-1;
                CSceneObject* obj=App::currentWorld->sceneObjects->getObjectFromPath(nullptr,scriptDescription.c_str(),0);
                if (obj!=nullptr)
                    objId=obj->getObjectHandle();
                else
                    objId=App::currentWorld->sceneObjects->getObjectHandleFromName_old(scriptDescription.c_str());
                if (scriptHandleOrType==sim_scripttype_childscript)
                    script=App::currentWorld->embeddedScriptContainer->getScriptFromObjectAttachedTo(sim_scripttype_childscript,objId);
                else
                    script=App::currentWorld->embeddedScriptContainer->getScriptFromObjectAttachedTo(sim_scripttype_customizationscript,objId);
            }
            if (scriptHandleOrType==sim_scripttype_sandboxscript)
                script=App::worldContainer->sandboxScript;
            if (scriptHandleOrType==sim_scripttype_addonscript)
                script=App::worldContainer->addOnScriptContainer->getAddOnFromName(scriptDescription.c_str());
        }

        if (script!=nullptr)
        {
            if (script->hasInterpreterState())
            {
                CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
                CScriptObject::buildFromInterpreterStack_lua(L,stack,3,0); // skip the two first args

                if (script->getThreadedExecutionIsUnderWay_oldThreads())
                { // very special handling here!
                    if (VThread::areThreadIDsSame(script->getThreadedScriptThreadId_old(),VThread::getCurrentThreadId()))
                    {
                        int rr=script->callCustomScriptFunction(funcName.c_str(),stack);
                        if (rr==1)
                        {
                            CScriptObject::buildOntoInterpreterStack_lua(L,stack,false);
                            int ss=stack->getStackSize();
                            App::worldContainer->interfaceStackContainer->destroyStack(stack);
                            if (ss==0)
                            {
                                pushIntTableOntoStack(L,0,nullptr);
                                ss++;
                            }
                            LUA_END(ss);
                        }
                        else
                        {
                            if (rr==-1)
                                errorString=SIM_ERROR_ERROR_IN_SCRIPT_FUNCTION;
                            else // rr==0
                                errorString=SIM_ERROR_FAILED_CALLING_SCRIPT_FUNCTION;
                        }
                    }
                    else
                    { // we have to execute that function via another thread!
                        void* d[4];
                        int callType=1;
                        d[0]=&callType;
                        d[1]=script;
                        d[2]=(void*)funcName.c_str();
                        d[3]=stack;
                        int retVal=CThreadPool_old::callRoutineViaSpecificThread(script->getThreadedScriptThreadId_old(),d);
                        if (retVal==1)
                        {
                            CScriptObject::buildOntoInterpreterStack_lua(L,stack,false);
                            int ss=stack->getStackSize();
                            App::worldContainer->interfaceStackContainer->destroyStack(stack);
                            if (ss==0)
                            {
                                pushIntTableOntoStack(L,0,nullptr);
                                ss++;
                            }
                            LUA_END(ss);
                        }
                        else
                        {
                            if (retVal==-1)
                                errorString=SIM_ERROR_ERROR_IN_SCRIPT_FUNCTION;
                            else // retVal==0
                                errorString=SIM_ERROR_FAILED_CALLING_SCRIPT_FUNCTION;
                        }
                    }
                }
                else
                {
                    if (VThread::isCurrentThreadTheMainSimulationThread())
                    { // For now we don't allow non-main threads to call non-threaded scripts!
                        int rr=script->callCustomScriptFunction(funcName.c_str(),stack);
                        if (rr==1)
                        {
                            CScriptObject::buildOntoInterpreterStack_lua(L,stack,false);
                            int ss=stack->getStackSize();
                            App::worldContainer->interfaceStackContainer->destroyStack(stack);
                            if (ss==0)
                            {
                                pushIntTableOntoStack(L,0,nullptr);
                                ss++;
                            }
                            LUA_END(ss);
                        }
                        else
                        {
                            if (rr==-1)
                                errorString=SIM_ERROR_ERROR_IN_SCRIPT_FUNCTION;
                            else // rr==0
                                errorString=SIM_ERROR_FAILED_CALLING_SCRIPT_FUNCTION;
                        }
                    }
                    else
                        errorString=SIM_ERROR_FAILED_CALLING_SCRIPT_FUNCTION;
                }
                App::worldContainer->interfaceStackContainer->destroyStack(stack);
            }
            else
                errorString=SIM_ERROR_SCRIPT_NOT_INITIALIZED;
        }
        else
            errorString=SIM_ERROR_SCRIPT_INEXISTANT;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetShapeMass(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getShapeMass");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        double mass;
        int result=simGetShapeMass_internal(handle,&mass);
        if (result!=-1)
        {
            luaWrap_lua_pushnumber(L,mass);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetShapeMass(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setShapeMass");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        double mass=luaToDouble(L,2);
        simSetShapeMass_internal(handle,mass);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetShapeInertia(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getShapeInertia");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        double inertiaMatrix[9];
        double transformation[12];
        int result=simGetShapeInertia_internal(handle,inertiaMatrix,transformation);
        if (result!=-1)
        {
            pushDoubleTableOntoStack(L,9,inertiaMatrix);
            pushDoubleTableOntoStack(L,12,transformation);
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetShapeInertia(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setShapeInertia");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,9,lua_arg_number,12))
    {
        int handle=luaToInt(L,1);
        double inertiaMatrix[9];
        getDoublesFromTable(L,2,9,inertiaMatrix);
        double transformation[12];
        getDoublesFromTable(L,3,12,transformation);
        simSetShapeInertia_internal(handle,inertiaMatrix,transformation);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simIsDynamicallyEnabled(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.isDynamicallyEnabled");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        int res=simIsDynamicallyEnabled_internal(handle);
        if (res>=0)
        {
            luaWrap_lua_pushboolean(L,res!=0);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGenerateShapeFromPath(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.generateShapeFromPath");

    if (checkInputArguments(L,&errorString,lua_arg_number,14,lua_arg_number,4))
    {
        std::vector<double> ppath;
        std::vector<double> section;
        ppath.resize(luaWrap_lua_rawlen(L,1));
        section.resize(luaWrap_lua_rawlen(L,2));
        getDoublesFromTable(L,1,luaWrap_lua_rawlen(L,1),&ppath[0]);
        getDoublesFromTable(L,2,luaWrap_lua_rawlen(L,2),&section[0]);
        int options=0;
        double* zvect=nullptr;
        int res=checkOneGeneralInputArgument(L,3,lua_arg_integer,0,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
                options=luaToInt(L,3);
            res=checkOneGeneralInputArgument(L,4,lua_arg_number,3,true,true,&errorString);
            if (res>=0)
            {
                double tmp[3];
                if (res==2)
                {
                    getDoublesFromTable(L,4,3,tmp);
                    zvect=tmp;
                }
                int h=simGenerateShapeFromPath_internal(&ppath[0],int(ppath.size()),&section[0],int(section.size()),options,zvect,0.0);
                if (h>=0)
                {
                    luaWrap_lua_pushinteger(L,h);
                    LUA_END(1);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetClosestPosOnPath(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getClosestPosOnPath");

    if (checkInputArguments(L,&errorString,lua_arg_number,3,lua_arg_number,1,lua_arg_number,3))
    {
        size_t pathS=luaWrap_lua_rawlen(L,1);
        size_t pathLS=luaWrap_lua_rawlen(L,2);
        if (pathLS<=pathS/3)
        {
            std::vector<double> ppath;
            std::vector<double> pathLengths;
            double absPt[3];
            ppath.resize(pathS);
            pathLengths.resize(pathLS);
            getDoublesFromTable(L,1,pathS,&ppath[0]);
            getDoublesFromTable(L,2,pathLS,&pathLengths[0]);
            getDoublesFromTable(L,3,3,absPt);
            double p=simGetClosestPosOnPath_internal(&ppath[0],int(pathLS*3),&pathLengths[0],absPt);
            luaWrap_lua_pushnumber(L,p);
            LUA_END(1);
        }
        else
            errorString=SIM_ERROR_ONE_TABLE_SIZE_IS_WRONG;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simInitScript(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.initScript");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int scriptHandle=luaToInt(L,1);
        int r=simInitScript_internal(scriptHandle);
        if (r>=0)
        {
            luaWrap_lua_pushboolean(L,r==1);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simModuleEntry(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.moduleEntry");

    if (checkInputArguments(L,&errorString,lua_arg_integer,0))
    {
        int itemHandle=luaToInt(L,1);
        const char* label=nullptr;
        std::string _label;
        int state=-1;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_string,0,false,true,&errorString);
        if (res>=1)
        {
            if (res==2)
            {
                _label=luaWrap_lua_tostring(L,2);
                label=_label.c_str();
            }
            res=checkOneGeneralInputArgument(L,3,lua_arg_integer,0,true,true,&errorString);
            if (res>=0)
            {
                if (res==2)
                    state=luaToInt(L,3);
                setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
                int h=simModuleEntry_internal(itemHandle,label,state);
                setCurrentScriptInfo_cSide(-1,-1);
                if (h>=0)
                {
                    luaWrap_lua_pushinteger(L,h);
                    LUA_END(1);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simPushUserEvent(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.pushUserEvent");

    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_integer,0,lua_arg_integer,0))
    {
        std::string eventStr(luaWrap_lua_tostring(L,1));
        int handle=luaToInt(L,2);
        long long int uid=luaWrap_lua_tointeger(L,3);
        if (luaWrap_lua_istable(L,4))
        {
            int options=0; // bit0: mergeable
            int res=checkOneGeneralInputArgument(L,5,lua_arg_integer,0,true,true,&errorString);
            if (res>=0)
            {
                if (res==2)
                    options=luaToInt(L,5);
                if (App::worldContainer->getEventsEnabled())
                {
                    auto [event,data]=App::worldContainer->prepareNakedEvent(eventStr.c_str(),handle,uid,(options&1)!=0);
                    CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
                    CScriptObject::buildFromInterpreterStack_lua(L,stack,4,0); // skip the 3 first args
                    CInterfaceStackTable* t=(CInterfaceStackTable*)stack->detachStackObjectFromIndex(0);
                    App::worldContainer->interfaceStackContainer->destroyStack(stack);
                    event.eventTable->appendMapObject_stringObject("data",t);
                    App::worldContainer->pushEvent(event);
                }
            }
        }
        else
            errorString.assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simBroadcastMsg(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.broadcastMsg");

    if (luaWrap_lua_istable(L,1))
    {
        int options=0;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_integer,0,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
                options=luaToInt(L,2);
            CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
            CScriptObject::buildFromInterpreterStack_lua(L,stack,1,0);
            stack->pushInt32OntoStack(CScriptObject::getScriptHandleFromInterpreterState_lua(L),false);
            App::worldContainer->broadcastMsg(stack,options);
            App::worldContainer->interfaceStackContainer->destroyStack(stack);
        }
    }
    else
        errorString.assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetVisionSensorRes(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getVisionSensorRes");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int resolution[2];
        if (simGetVisionSensorRes_internal(luaToInt(L,1),resolution)==1)
        {
            pushIntTableOntoStack(L,2,resolution);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simHandleJointMotion(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.handleJointMotion");

    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (it!=nullptr)
    {
        if (it->getScriptType()==sim_scripttype_mainscript)
        {
            for (size_t i=0;i<App::currentWorld->sceneObjects->getJointCount();i++)
            {
                CJoint* it=App::currentWorld->sceneObjects->getJointFromIndex(i);
                if (it->getJointMode()==sim_jointmode_kinematic)
                    it->handleMotion();
            }
        }
        else
            errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetGenesisEvents(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getGenesisEvents");

    // no args for now
    CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
    App::worldContainer->getGenesisEvents(stack);
    CScriptObject::buildOntoInterpreterStack_lua(L,stack,false);
    int s=stack->getStackSize();
    App::worldContainer->interfaceStackContainer->destroyStack(stack);
    LUA_END(s);

//    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
//    LUA_END(0);
}

int _simGroupShapes(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.groupShapes");

    int retVal=-1; // error
    if (checkInputArguments(L,&errorString,lua_arg_number,1))
    {
        int res=checkOneGeneralInputArgument(L,2,lua_arg_bool,0,true,true,&errorString);
        if (res>=0)
        {
            bool mergeShapes=false;
            if (res==2)
                mergeShapes=luaToBool(L,2);
            int tableSize=int(luaWrap_lua_rawlen(L,1));
            int* theTable=new int[tableSize];
            getIntsFromTable(L,1,tableSize,theTable);
            if (mergeShapes)
                tableSize=-tableSize;
            retVal=simGroupShapes_internal(theTable,tableSize);
            delete[] theTable;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simUngroupShape(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.ungroupShape");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int shapeHandle=luaToInt(L,1);
        int count;
        int* handles=simUngroupShape_internal(shapeHandle,&count);
        if (handles!=nullptr)
        {
            pushIntTableOntoStack(L,count,handles);
            delete[] handles;
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}


int _simConvexDecompose(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.convexDecompose");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int shapeHandle=luaToInt(L,1);
        int options=luaToInt(L,2);
        int intParams[10];
        double floatParams[10];
        bool goOn=true;
        if ((options&4)==0)
        {
            goOn=false;
            int ipc=4;
            int fpc=3;
            if (options&128)
            {
                ipc=10;
                fpc=10;
            }
            int res=checkOneGeneralInputArgument(L,3,lua_arg_number,ipc,false,false,&errorString);
            if (res==2)
            {
                res=checkOneGeneralInputArgument(L,4,lua_arg_number,fpc,false,false,&errorString);
                if (res==2)
                {
                    getIntsFromTable(L,3,ipc,intParams);
                    getDoublesFromTable(L,4,fpc,floatParams);
                    goOn=true;
                }
            }
        }
        intParams[4]=0;
        floatParams[3]=0.0;
        floatParams[4]=0.0;
        if (goOn)
            retVal=simConvexDecompose_internal(shapeHandle,options,intParams,floatParams);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simQuitSimulator(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.quitSimulator");

    simQuitSimulator_internal(false);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetShapeMaterial(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setShapeMaterial");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int shapeHandle=luaToInt(L,1);
        int materialId=luaToInt(L,2);
        retVal=simSetShapeMaterial_internal(shapeHandle,materialId);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetTextureId(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getTextureId");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string matName(luaWrap_lua_tostring(L,1));
        int resolution[2];
        int retVal=simGetTextureId_internal(matName.c_str(),resolution);
        if (retVal>=0)
        {
            luaWrap_lua_pushinteger(L,retVal);
            pushIntTableOntoStack(L,2,resolution);
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simReadTexture(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.readTexture");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int textureId=luaToInt(L,1);
        int options=luaToInt(L,2);
        int posX=0;
        int posY=0;
        int sizeX=0;
        int sizeY=0;
        // Now check the optional arguments:
        int res;
        res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                posX=luaToInt(L,3);
            res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    posY=luaToInt(L,4);
                res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        sizeX=luaToInt(L,5);
                    res=checkOneGeneralInputArgument(L,6,lua_arg_number,0,true,false,&errorString);
                    if ((res==0)||(res==2))
                    {
                        if (res==2)
                            sizeY=luaToInt(L,6);

                        CTextureObject* to=App::currentWorld->textureContainer->getObject(textureId);
                        if (to!=nullptr)
                        {
                            int tSizeX,tSizeY;
                            to->getTextureSize(tSizeX,tSizeY);
                            if ( (posX>=0)&&(posY>=0)&&(sizeX>=0)&&(sizeY>=0)&&(posX+sizeX<=tSizeX)&&(posY+sizeY<=tSizeY) )
                            {
                                if (sizeX==0)
                                {
                                    posX=0;
                                    sizeX=tSizeX;
                                }
                                if (sizeY==0)
                                {
                                    posY=0;
                                    sizeY=tSizeY;
                                }
                                unsigned char* textureData=simReadTexture_internal(textureId,options,posX,posY,sizeX,sizeY);
                                if (textureData!=nullptr)
                                { // here we return RGB data in a string
                                    luaWrap_lua_pushlstring(L,(char*)textureData,sizeX*sizeY*3);
                                    simReleaseBuffer_internal(textureData);
                                    LUA_END(1);
                                }
                            }
                            else
                                errorString=SIM_ERROR_INVALID_ARGUMENTS;
                        }
                        else
                            errorString=SIM_ERROR_TEXTURE_INEXISTANT;
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simWriteTexture(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.writeTexture");

    int retVal=-1; // error
    if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_integer,0,lua_arg_string,0))
    {
        int textureId=luaToInt(L,1);
        int options=luaToInt(L,2);
        size_t dataLength;
        char* data=(char*)luaWrap_lua_tolstring(L,3,&dataLength);
        if (dataLength>=3)
        {
            int posX=0;
            int posY=0;
            int sizeX=0;
            int sizeY=0;
            double interpol=0.0;
            // Now check the optional arguments:
            int res;
            res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    posX=luaToInt(L,4);
                res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        posY=luaToInt(L,5);
                    res=checkOneGeneralInputArgument(L,6,lua_arg_number,0,true,false,&errorString);
                    if ((res==0)||(res==2))
                    {
                        if (res==2)
                            sizeX=luaToInt(L,6);
                        res=checkOneGeneralInputArgument(L,7,lua_arg_number,0,true,false,&errorString);
                        if ((res==0)||(res==2))
                        {
                            if (res==2)
                                sizeY=luaToInt(L,7);
                            res=checkOneGeneralInputArgument(L,8,lua_arg_number,0,true,false,&errorString);
                            if ((res==0)||(res==2))
                            {
                                if (res==2)
                                    interpol=luaToDouble(L,8);
                                CTextureObject* to=App::currentWorld->textureContainer->getObject(textureId);
                                if (to!=nullptr)
                                {
                                    int tSizeX,tSizeY;
                                    to->getTextureSize(tSizeX,tSizeY);
                                    if ( (sizeX>=0)&&(sizeY>=0) )
                                    {
                                        if (sizeX==0)
                                        {
                                            posX=0;
                                            sizeX=tSizeX;
                                        }
                                        if (sizeY==0)
                                        {
                                            posY=0;
                                            sizeY=tSizeY;
                                        }
                                        if (int(dataLength)<sizeX*sizeY*3)
                                        {
                                            std::vector<char> dat;
                                            dat.resize(sizeX*sizeY*3);
                                            for (size_t i=0;i<sizeX*sizeY;i++)
                                            {
                                                dat[3*i+0]=data[0];
                                                dat[3*i+1]=data[1];
                                                dat[3*i+2]=data[2];
                                            }
                                            retVal=simWriteTexture_internal(textureId,options,dat.data(),posX,posY,sizeX,sizeY,interpol);
                                        }
                                        else
                                            retVal=simWriteTexture_internal(textureId,options,data,posX,posY,sizeX,sizeY,interpol);
                                    }
                                    else
                                        errorString=SIM_ERROR_INVALID_ARGUMENTS;
                                }
                                else
                                    errorString=SIM_ERROR_TEXTURE_INEXISTANT;
                            }
                        }
                    }
                }
            }
        }
        else
            errorString=SIM_ERROR_INVALID_BUFFER_SIZE;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCreateTexture(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.createTexture");

    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0))
    {
        std::string fileName(luaWrap_lua_tostring(L,1));
        int options=luaToInt(L,2);
        double* planeSizesP=nullptr;
        double planeSizes[2];
        double* scalingUVP=nullptr;
        double scalingUV[2];
        double* xy_gP=nullptr;
        double xy_g[3];
        int resolution[2]={0,0}; // means: just any!

        // Now check the optional arguments:
        int res;
        res=checkOneGeneralInputArgument(L,3,lua_arg_number,2,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getDoublesFromTable(L,3,2,planeSizes);
                planeSizesP=planeSizes;
            }

            res=checkOneGeneralInputArgument(L,4,lua_arg_number,2,true,true,&errorString);
            if (res>=0)
            {
                if (res==2)
                {
                    getDoublesFromTable(L,4,2,scalingUV);
                    scalingUVP=scalingUV;
                }

                res=checkOneGeneralInputArgument(L,5,lua_arg_number,3,true,true,&errorString);
                if (res>=0)
                {
                    if (res==2)
                    {
                        getDoublesFromTable(L,5,3,xy_g);
                        xy_gP=xy_g;
                    }

                    res=checkOneGeneralInputArgument(L,6,lua_arg_number,0,true,true,&errorString);
                    if (res>=0)
                    {
                        int maxTextureSize=0; // just the original
                        if (res==2)
                            maxTextureSize=luaToInt(L,6);

                        res=checkOneGeneralInputArgument(L,7,lua_arg_number,2,fileName.length()!=0,fileName.length()!=0,&errorString);
                        if (res>=0)
                        {
                            if (res==2)
                                getIntsFromTable(L,7,2,resolution);

                            if (fileName.length()==0)
                            { // we are not loading a texture, but creating it!
                                resolution[0]=tt::getLimitedInt(1,4096,resolution[0]);
                                resolution[1]=tt::getLimitedInt(1,4096,resolution[1]);
                            }

                            int textureId;
                            int shapeHandle=simCreateTexture_internal(fileName.c_str(),options,planeSizesP,scalingUVP,xy_gP,maxTextureSize,&textureId,resolution,nullptr);
                            if (shapeHandle>=0)
                            {
                                luaWrap_lua_pushinteger(L,shapeHandle);
                                luaWrap_lua_pushinteger(L,textureId);
                                pushIntTableOntoStack(L,2,resolution);
                                LUA_END(3);
                            }
                        }
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}


int _simWriteCustomDataBlock(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.writeCustomDataBlock");

    int retVal=-1; // error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
    {
        int objectHandle=luaToInt(L,1);
        if (objectHandle==sim_handle_self)
            objectHandle=CScriptObject::getScriptHandleFromInterpreterState_lua(L);

        std::string dataName(luaWrap_lua_tostring(L,2));
        int res;
        res=checkOneGeneralInputArgument(L,3,lua_arg_string,0,false,true,&errorString);
        if (res>=1)
        {
            size_t dataLength=0;
            char* data=nullptr;
            if (res==2)
                data=(char*)luaWrap_lua_tolstring(L,3,&dataLength);
            retVal=simWriteCustomDataBlock_internal(objectHandle,dataName.c_str(),data,(int)dataLength);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}


int _simReadCustomDataBlock(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.readCustomDataBlock");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
    {
        int objectHandle=luaToInt(L,1);
        if (objectHandle==sim_handle_self)
            objectHandle=CScriptObject::getScriptHandleFromInterpreterState_lua(L);

        std::string dataName(luaWrap_lua_tostring(L,2));
        int dataLength;
        char* data=simReadCustomDataBlock_internal(objectHandle,dataName.c_str(),&dataLength);
        if (data!=nullptr)
        {
            luaWrap_lua_pushlstring(L,(const char*)data,dataLength);
            simReleaseBuffer_internal(data);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simReadCustomDataBlockTags(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.readCustomDataBlockTags");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int objectHandle=luaToInt(L,1);
        if (objectHandle==sim_handle_self)
            objectHandle=CScriptObject::getScriptHandleFromInterpreterState_lua(L);

        int tagCount;
        char* data=simReadCustomDataBlockTags_internal(objectHandle,&tagCount);
        if (data!=nullptr)
        {
            std::vector<std::string> stringTable;
            size_t off=0;
            for (int i=0;i<tagCount;i++)
            {
                stringTable.push_back(data+off);
                off+=strlen(data+off)+1;
            }
            pushStringTableOntoStack(L,stringTable);
            simReleaseBuffer_internal(data);

            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetShapeGeomInfo(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getShapeGeomInfo");

    int retVal=-1; // means error
    int intData[5]={0,0,0,0,0};
    double floatData[5]={0.0,0.0,0.0,0.0,0.0};
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        retVal=simGetShapeGeomInfo_internal(handle,intData,floatData,nullptr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    luaWrap_lua_pushinteger(L,intData[0]);
    pushDoubleTableOntoStack(L,4,floatData);
    LUA_END(3);
}

int _simGetObjects(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjects");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simGetObjects_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectsInTree(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getObjectsInTree");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        int objType=sim_handle_all;
        int options=0;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
                objType=luaToInt(L,2);
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,true,&errorString);
            if (res>=0)
            {
                if (res==2)
                    options=luaToInt(L,3);
                int objCnt=0;
                int* objHandles=simGetObjectsInTree_internal(handle,objType,options,&objCnt);
                if (objHandles!=nullptr)
                {
                    pushIntTableOntoStack(L,objCnt,objHandles);
                    simReleaseBuffer_internal((char*)objHandles);
                    LUA_END(1);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simScaleObject(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.scaleObject");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        double x=luaToDouble(L,2);
        double y=luaToDouble(L,3);
        double z=luaToDouble(L,4);
        int options=0;
        int res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
                options=luaToInt(L,5);
            retVal=simScaleObject_internal(handle,x,y,z,options);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetShapeTexture(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setShapeTexture");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,2))
    {
        int handle=luaToInt(L,1);
        int textureId=luaToInt(L,2);
        int mapMode=luaToInt(L,3);
        int options=luaToInt(L,4);
        double uvScaling[2];
        getDoublesFromTable(L,5,2,uvScaling);
        double* posP=nullptr;
        double* orP=nullptr;
        double _pos[3];
        double _or[3];
        int res=checkOneGeneralInputArgument(L,6,lua_arg_number,3,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getDoublesFromTable(L,6,3,_pos);
                posP=_pos;
            }
            res=checkOneGeneralInputArgument(L,7,lua_arg_number,3,true,true,&errorString);
            if (res>=0)
            {
                if (res==2)
                {
                    getDoublesFromTable(L,7,3,_or);
                    orP=_or;
                }
                retVal=simSetShapeTexture_internal(handle,textureId,mapMode,options,uvScaling,posP,orP);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetShapeTextureId(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getShapeTextureId");
    int retVal=-1;

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        retVal=simGetShapeTextureId_internal(handle);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCreateCollectionEx(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.createCollectionEx");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int options=luaToInt(L,1);
        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
        int handle=simCreateCollectionEx_internal(options);
        setCurrentScriptInfo_cSide(-1,-1);
        luaWrap_lua_pushinteger(L,handle);
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simAddItemToCollection(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.addItemToCollection");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int collHandle=luaToInt(L,1);
        int what=luaToInt(L,2);
        int objHandle=luaToInt(L,3);
        int options=luaToInt(L,4);
        simAddItemToCollection_internal(collHandle,what,objHandle,options);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simDestroyCollection(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.destroyCollection");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        simDestroyCollection_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetCollectionObjects(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getCollectionObjects");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        int cnt;
        int* objHandles=simGetCollectionObjects_internal(handle,&cnt);
        pushIntTableOntoStack(L,cnt,objHandles);
        delete[] objHandles;
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simHandleAddOnScripts(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.handleAddOnScripts");

    int retVal=-1;
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* itScrObj=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (itScrObj->getScriptType()==sim_scripttype_mainscript)
    {
        if (checkInputArguments(L,&errorString,lua_arg_number,0))
        {
            int callType=luaToInt(L,1);
            retVal=0;
            if (App::getEditModeType()==NO_EDIT_MODE)
                retVal=App::worldContainer->addOnScriptContainer->callScripts(callType,nullptr,nullptr);
        }
    }
    else
        errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simHandleSandboxScript(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.handleSandboxScript");

    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* itScrObj=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (itScrObj->getScriptType()==sim_scripttype_mainscript)
    {
        if (checkInputArguments(L,&errorString,lua_arg_number,0))
        {
            int callType=luaToInt(L,1);
            if ( (App::getEditModeType()==NO_EDIT_MODE)&&(App::worldContainer->sandboxScript!=nullptr) )
                App::worldContainer->sandboxScript->systemCallScript(callType,nullptr,nullptr);
        }
    }
    else
        errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simReorientShapeBoundingBox(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.reorientShapeBoundingBox");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int shapeHandle=luaToInt(L,1);
        int relativeToHandle=luaToInt(L,2);
        retVal=simReorientShapeBoundingBox_internal(shapeHandle,relativeToHandle,0);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetEngineFloatParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getEngineFloatParam");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int paramId=luaToInt(L,1);
        int objectHandle=luaToInt(L,2);
        bool ok;
        double paramVal=simGetEngineFloatParam_internal(paramId,objectHandle,nullptr,&ok);
        if (ok)
        {
            luaWrap_lua_pushnumber(L,paramVal);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetEngineInt32Param(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getEngineInt32Param");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int paramId=luaToInt(L,1);
        int objectHandle=luaToInt(L,2);
        bool ok;
        int paramVal=simGetEngineInt32Param_internal(paramId,objectHandle,nullptr,&ok);
        if (ok)
        {
            luaWrap_lua_pushinteger(L,paramVal);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetEngineBoolParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getEngineBoolParam");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int paramId=luaToInt(L,1);
        int objectHandle=luaToInt(L,2);
        bool ok;
        bool paramVal=simGetEngineBoolParam_internal(paramId,objectHandle,nullptr,&ok);
        if (ok)
        {
            luaWrap_lua_pushboolean(L,paramVal);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetEngineFloatParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setEngineFloatParam");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int paramId=luaToInt(L,1);
        int objectHandle=luaToInt(L,2);
        double paramVal=luaToDouble(L,3);
        retVal=simSetEngineFloatParam_internal(paramId,objectHandle,nullptr,paramVal);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetEngineInt32Param(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setEngineInt32Param");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int paramId=luaToInt(L,1);
        int objectHandle=luaToInt(L,2);
        int paramVal=luaToInt(L,3);
        retVal=simSetEngineInt32Param_internal(paramId,objectHandle,nullptr,paramVal);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetEngineBoolParam(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setEngineBoolParam");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_bool,0))
    {
        int paramId=luaToInt(L,1);
        int objectHandle=luaToInt(L,2);
        bool paramVal=luaToBool(L,3);
        retVal=simSetEngineBoolParam_internal(paramId,objectHandle,nullptr,paramVal);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCreateOctree(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.createOctree");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        double voxelSize=luaToDouble(L,1);
        int options=luaToInt(L,2);
        double pointSize=luaToDouble(L,3);
        retVal=simCreateOctree_internal(voxelSize,options,pointSize,nullptr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCreatePointCloud(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.createPointCloud");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        double maxVoxelSize=luaToDouble(L,1);
        int maxPtCntPerVoxel=luaToInt(L,2);
        int options=luaToInt(L,3);
        double pointSize=luaToDouble(L,4);
        retVal=simCreatePointCloud_internal(maxVoxelSize,maxPtCntPerVoxel,options,pointSize,nullptr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetPointCloudOptions(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setPointCloudOptions");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        double maxVoxelSize=luaToDouble(L,2);
        int maxPtCntPerVoxel=luaToInt(L,3);
        int options=luaToInt(L,4);
        double pointSize=luaToDouble(L,5);
        retVal=simSetPointCloudOptions_internal(handle,maxVoxelSize,maxPtCntPerVoxel,options,pointSize,nullptr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetPointCloudOptions(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getPointCloudOptions");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        double maxVoxelSize;
        int maxPtCntPerVoxel;
        int options;
        double pointSize;
        int retVal=simGetPointCloudOptions_internal(handle,&maxVoxelSize,&maxPtCntPerVoxel,&options,&pointSize,nullptr);
        if (retVal>=0)
        {
            luaWrap_lua_pushnumber(L,maxVoxelSize);
            luaWrap_lua_pushinteger(L,maxPtCntPerVoxel);
            luaWrap_lua_pushinteger(L,options);
            luaWrap_lua_pushnumber(L,pointSize);
            LUA_END(4);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simInsertVoxelsIntoOctree(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.insertVoxelsIntoOctree");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,3))
    {
        int handle=luaToInt(L,1);
        int options=luaToInt(L,2);
        int ptCnt=int(luaWrap_lua_rawlen(L,3))/3;
        std::vector<double> pts;
        pts.resize(ptCnt*3);
        unsigned char* cols=nullptr;
        std::vector<unsigned char> _cols;
        int v=3;
        if (options&2)
            v=ptCnt*3;
        int res=checkOneGeneralInputArgument(L,4,lua_arg_number,v,true,true,&errorString);
        if (res>=0)
        {
            getDoublesFromTable(L,3,ptCnt*3,&pts[0]);
            if (res==2)
            {
                _cols.resize(v);
                getUCharsFromTable(L,4,v,&_cols[0]);
                cols=&_cols[0];
            }
            res=checkOneGeneralInputArgument(L,5,lua_arg_number,v/3,true,true,&errorString);
            if (res>=0)
            {
                if (cols==nullptr)
                    retVal=simInsertVoxelsIntoOctree_internal(handle,options,&pts[0],ptCnt,nullptr,nullptr,nullptr);
                else
                {
                    unsigned int* tags=nullptr;
                    std::vector<unsigned int> _tags;
                    if (res==2)
                    {
                        _tags.resize(v/3);
                        getUIntsFromTable(L,5,v/3,&_tags[0]);
                        tags=&_tags[0];
                    }
                    retVal=simInsertVoxelsIntoOctree_internal(handle,options,&pts[0],ptCnt,cols,tags,nullptr);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simRemoveVoxelsFromOctree(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.removeVoxelsFromOctree");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        int options=luaToInt(L,2);
        int res=checkOneGeneralInputArgument(L,3,lua_arg_number,3,false,true,&errorString);
        if (res>=1)
        {
            if (res==2)
            { // remove some voxels
                int ptCnt=int(luaWrap_lua_rawlen(L,3))/3;
                std::vector<double> pts;
                pts.resize(ptCnt*3);
                getDoublesFromTable(L,3,ptCnt*3,&pts[0]);
                retVal=simRemoveVoxelsFromOctree_internal(handle,options,&pts[0],ptCnt,nullptr);
            }
            else
                retVal=simRemoveVoxelsFromOctree_internal(handle,options,nullptr,0,nullptr); // remove all voxels!
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simInsertPointsIntoPointCloud(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.insertPointsIntoPointCloud");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,3))
    {
        int handle=luaToInt(L,1);
        int options=luaToInt(L,2);
        int ptCnt=int(luaWrap_lua_rawlen(L,3))/3;
        float optionalValues[2];
        ((int*)optionalValues)[0]=1; // duplicate tolerance bit
        optionalValues[1]=0.0; // duplicate tolerance
        std::vector<double> pts;
        pts.resize(ptCnt*3);
        unsigned char* cols=nullptr;
        std::vector<unsigned char> _cols;
        int v=3;
        if (options&2)
            v=ptCnt*3;
        int res=checkOneGeneralInputArgument(L,4,lua_arg_number,v,true,true,&errorString);
        if (res>=0)
        {
            getDoublesFromTable(L,3,ptCnt*3,&pts[0]);
            if (res==2)
            {
                _cols.resize(v);
                getUCharsFromTable(L,4,v,&_cols[0]);
                cols=&_cols[0];
            }
            res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,true,true,&errorString);
            if (res>=0)
            {
                if (res==2)
                {
                    optionalValues[1]=(float)luaToDouble(L,5); // duplicate tolerance
                    retVal=simInsertPointsIntoPointCloud_internal(handle,options,&pts[0],ptCnt,cols,optionalValues);
                }
                else
                    retVal=simInsertPointsIntoPointCloud_internal(handle,options,&pts[0],ptCnt,cols,nullptr);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simRemovePointsFromPointCloud(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.removePointsFromPointCloud");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        int options=luaToInt(L,2);
        int res=checkOneGeneralInputArgument(L,3,lua_arg_number,3,false,true,&errorString);
        if (res>=1)
        {
            int res2=checkOneGeneralInputArgument(L,4,lua_arg_number,0,false,false,&errorString);
            if (res2==2)
            {
                double tolerance=luaToDouble(L,4);
                if (res==2)
                { // remove some points
                    int ptCnt=int(luaWrap_lua_rawlen(L,3))/3;
                    std::vector<double> pts;
                    pts.resize(ptCnt*3);
                    getDoublesFromTable(L,3,ptCnt*3,&pts[0]);
                    retVal=simRemovePointsFromPointCloud_internal(handle,options,&pts[0],ptCnt,tolerance,nullptr);
                }
                else
                    retVal=simRemovePointsFromPointCloud_internal(handle,options,nullptr,0,0.0,nullptr); // remove all points
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simIntersectPointsWithPointCloud(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.intersectPointsWithPointCloud");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        int options=luaToInt(L,2);
        int res=checkOneGeneralInputArgument(L,3,lua_arg_number,3,false,true,&errorString);
        if (res>=1)
        {
            int res2=checkOneGeneralInputArgument(L,4,lua_arg_number,0,false,false,&errorString);
            if (res2==2)
            {
                double tolerance=luaToDouble(L,4);
                if (res==2)
                { // intersect some points
                    int ptCnt=int(luaWrap_lua_rawlen(L,3))/3;
                    std::vector<double> pts;
                    pts.resize(ptCnt*3);
                    getDoublesFromTable(L,3,ptCnt*3,&pts[0]);
                    retVal=simIntersectPointsWithPointCloud_internal(handle,options,&pts[0],ptCnt,tolerance,nullptr);
                }
                else
                    retVal=simRemovePointsFromPointCloud_internal(handle,options,nullptr,0,0.0,nullptr); // remove all points
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetOctreeVoxels(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getOctreeVoxels");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        int ptCnt=-1;
        const double* p=simGetOctreeVoxels_internal(handle,&ptCnt,nullptr);
        if (ptCnt>=0)
        {
            pushDoubleTableOntoStack(L,ptCnt*3,p);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetPointCloudPoints(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getPointCloudPoints");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        int ptCnt=-1;
        const double* p=simGetPointCloudPoints_internal(handle,&ptCnt,nullptr);
        if (ptCnt>=0)
        {
            pushDoubleTableOntoStack(L,ptCnt*3,p);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simInsertObjectIntoOctree(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.insertObjectIntoOctree");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int handle1=luaToInt(L,1);
        int handle2=luaToInt(L,2);
        int options=luaToInt(L,3);
        unsigned char col[3];
        unsigned char* c=nullptr;
        int tag=0;
        int res=checkOneGeneralInputArgument(L,4,lua_arg_number,3,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getUCharsFromTable(L,4,3,col);
                c=col;
            }
            res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,true,true,&errorString);
            if (res==2)
                tag=(unsigned int)luaToInt(L,5);
            retVal=simInsertObjectIntoOctree_internal(handle1,handle2,options,c,tag,nullptr);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSubtractObjectFromOctree(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.subtractObjectFromOctree");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int handle1=luaToInt(L,1);
        int handle2=luaToInt(L,2);
        int options=luaToInt(L,3);
        retVal=simSubtractObjectFromOctree_internal(handle1,handle2,options,nullptr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simInsertObjectIntoPointCloud(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.insertObjectIntoPointCloud");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int handle1=luaToInt(L,1);
        int handle2=luaToInt(L,2);
        int options=luaToInt(L,3);
        double gridSize=luaToDouble(L,4);
        float optionalValues[2];
        ((int*)optionalValues)[0]=1; // duplicate tolerance bit
        optionalValues[1]=0.0; // duplicate tolerance
        unsigned char col[3];
        unsigned char* c=nullptr;
        int res=checkOneGeneralInputArgument(L,5,lua_arg_number,3,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getUCharsFromTable(L,5,3,col);
                c=col;
            }
            res=checkOneGeneralInputArgument(L,6,lua_arg_number,0,true,true,&errorString);
            if (res>=0)
            {
                if (res==2)
                {
                    optionalValues[1]=(float)luaToDouble(L,6); // duplicate tolerance
                    retVal=simInsertObjectIntoPointCloud_internal(handle1,handle2,options,gridSize,c,optionalValues);
                }
                else
                    retVal=simInsertObjectIntoPointCloud_internal(handle1,handle2,options,gridSize,c,nullptr);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSubtractObjectFromPointCloud(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.subtractObjectFromPointCloud");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int handle1=luaToInt(L,1);
        int handle2=luaToInt(L,2);
        int options=luaToInt(L,3);
        double tolerance=luaToDouble(L,4);
        retVal=simSubtractObjectFromPointCloud_internal(handle1,handle2,options,tolerance,nullptr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCheckOctreePointOccupancy(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.checkOctreePointOccupancy");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,3))
    {
        int handle=luaToInt(L,1);
        int options=luaToInt(L,2);
        int ptCnt=int(luaWrap_lua_rawlen(L,3))/3;
        std::vector<double> points;
        points.resize(ptCnt*3);
        getDoublesFromTable(L,3,ptCnt*3,&points[0]);
        unsigned int tag=0;
        unsigned long long int location=0;
        unsigned int locLow=location&0xffffffff;
        unsigned int locHigh=(location>>32)&0xffffffff;
        retVal=simCheckOctreePointOccupancy_internal(handle,options,&points[0],ptCnt,&tag,&location,nullptr);
        luaWrap_lua_pushinteger(L,retVal);
        luaWrap_lua_pushinteger(L,tag);
        luaWrap_lua_pushinteger(L,locLow);
        luaWrap_lua_pushinteger(L,locHigh);
        LUA_END(4);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simPackTable(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.packTable");

    if (luaWrap_lua_gettop(L)>=1)
    {
        if (luaWrap_lua_istable(L,1))
        {
            int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,true,&errorString);
            if (res>=0)
            {
                int scheme=0;
                if (res==2)
                    scheme=luaToInt(L,2);
                CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
                CScriptObject::buildFromInterpreterStack_lua(L,stack,1,1);
                std::string s;
                if (scheme==0)
                    s=stack->getBufferFromTable();
                if (scheme==1)
                    s=stack->getCborEncodedBufferFromTable(1);
                if (scheme==2)
                    s=stack->getCborEncodedBufferFromTable(0); // doubles coded as double
                luaWrap_lua_pushlstring(L,s.c_str(),s.length());
                App::worldContainer->interfaceStackContainer->destroyStack(stack);
                LUA_END(1);
            }
        }
        else
            errorString.assign(SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG);
    }
    else
        errorString.assign(SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simUnpackTable(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.unpackTable");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        size_t l;
        const char* s=luaWrap_lua_tolstring(L,1,&l);
        CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
        if (stack->pushTableFromBuffer(s,(unsigned int)l))
        {
            CScriptObject::buildOntoInterpreterStack_lua(L,stack,true);
            App::worldContainer->interfaceStackContainer->destroyStack(stack);
            LUA_END(1);
        }
        App::worldContainer->interfaceStackContainer->destroyStack(stack);
        errorString.assign(SIM_ERROR_INVALID_DATA);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simHandleSimulationStart(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.handleSimulationStart");

    int retVal=-1;
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* itScrObj=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (itScrObj->getScriptType()==sim_scripttype_mainscript)
    {
        // Following is for velocity measurement (initial):
        for (size_t i=0;i<App::currentWorld->sceneObjects->getJointCount();i++)
            App::currentWorld->sceneObjects->getJointFromIndex(i)->measureJointVelocity(0.0);
        for (size_t i=0;i<App::currentWorld->sceneObjects->getObjectCount();i++)
            App::currentWorld->sceneObjects->getObjectFromIndex(i)->measureVelocity(0.0);
    }
    else
        errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simHandleSensingStart(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.handleSensingStart");

    int retVal=-1;
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* itScrObj=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (itScrObj->getScriptType()==sim_scripttype_mainscript)
    {
        // Following is for camera tracking!
        for (size_t i=0;i<App::currentWorld->sceneObjects->getCameraCount();i++)
        {
            CCamera*  it=App::currentWorld->sceneObjects->getCameraFromIndex(i);
            it->handleCameraTracking();
        }

        // Following is for velocity measurement:
        double dt=App::currentWorld->simulation->getTimeStep();
        double t=dt+App::currentWorld->simulation->getSimulationTime();
        for (size_t i=0;i<App::currentWorld->sceneObjects->getJointCount();i++)
            App::currentWorld->sceneObjects->getJointFromIndex(i)->measureJointVelocity(t);
        for (size_t i=0;i<App::currentWorld->sceneObjects->getObjectCount();i++)
            App::currentWorld->sceneObjects->getObjectFromIndex(i)->measureVelocity(dt); // adapt that func!
    }
    else
        errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAuxFunc(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.auxFunc");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string cmd(luaWrap_lua_tostring(L,1));
        if (cmd.compare("frexp")==0)
        {
            if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0))
            {
                int e;
                luaWrap_lua_pushnumber(L,frexp(luaWrap_lua_tonumber(L,2),&e));
                luaWrap_lua_pushinteger(L,e);
                LUA_END(2);
            }
            LUA_END(0);
        }
        if (cmd.compare("createMirror")==0)
        {
            if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,2))
            {
                double s[2];
                getDoublesFromTable(L,2,2,s);
                CMirror* m=new CMirror();
                m->setMirrorWidth(s[0]);
                m->setMirrorHeight(s[1]);
                App::currentWorld->sceneObjects->addObjectToScene(m,false,true);
                int h=m->getObjectHandle();
                luaWrap_lua_pushinteger(L,h);
                LUA_END(1);
            }
            LUA_END(0);
        }
        if (cmd.compare("activateMainWindow")==0)
        {
#ifdef SIM_WITH_GUI
            if (App::mainWindow!=nullptr)
                App::mainWindow->activateMainWindow();
#endif
            LUA_END(0);
        }
        if (cmd.compare("sleep")==0)
        {
            int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,false,false,&errorString);
            if (res==2)
            {
                int tInMs=int(luaToDouble(L,2)*1000.0);
                VThread::sleep(tInMs);
            }
            LUA_END(0);
        }
        if (cmd.compare("enableRendering")==0)
        {
            App::simThread->setRenderingAllowed(true);
            LUA_END(0);
        }
        if (cmd.compare("disableRendering")==0)
        {
            App::simThread->setRenderingAllowed(false);
            LUA_END(0);
        }
        if (cmd.compare("curveToClipboard")==0)
        {
            if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0,lua_arg_number,0,lua_arg_string,0))
            {
                int graphHandle=luaToInt(L,2);
                int curveType=luaToInt(L,3);
                std::string curveName(luaWrap_lua_tostring(L,4));
                CGraph* it=App::currentWorld->sceneObjects->getGraphFromHandle(graphHandle);
                if (it!=nullptr)
                {
                    it->curveToClipboard(curveType,curveName.c_str());
                    LUA_END(0);
                }
                else
                    errorString=SIM_ERROR_INVALID_HANDLE;
            }
        }
        if (cmd.compare("curveToStatic")==0)
        {
            if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0,lua_arg_number,0,lua_arg_string,0))
            {
                int graphHandle=luaToInt(L,2);
                int curveType=luaToInt(L,3);
                std::string curveName(luaWrap_lua_tostring(L,4));
                CGraph* it=App::currentWorld->sceneObjects->getGraphFromHandle(graphHandle);
                if (it!=nullptr)
                {
                    it->curveToStatic(curveType,curveName.c_str());
                    LUA_END(0);
                }
                else
                    errorString=SIM_ERROR_INVALID_HANDLE;
            }
        }
        if (cmd.compare("removeStaticCurve")==0)
        {
            if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0,lua_arg_number,0,lua_arg_string,0))
            {
                int graphHandle=luaToInt(L,2);
                int curveType=luaToInt(L,3);
                std::string curveName(luaWrap_lua_tostring(L,4));
                CGraph* it=App::currentWorld->sceneObjects->getGraphFromHandle(graphHandle);
                if (it!=nullptr)
                {
                    it->removeStaticCurve(curveType,curveName.c_str());
                    LUA_END(0);
                }
                else
                    errorString=SIM_ERROR_INVALID_HANDLE;
            }
        }
        if (cmd.compare("setAssemblyMatchValues")==0)
        {
            if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0,lua_arg_bool,0,lua_arg_string,0))
            {
                int objHandle=luaToInt(L,2);
                bool childAttr=luaToBool(L,3);
                std::string matchValues(luaWrap_lua_tostring(L,4));
                CSceneObject* it=App::currentWorld->sceneObjects->getObjectFromHandle(objHandle);
                if (it!=nullptr)
                {
                    it->setAssemblyMatchValues(childAttr,matchValues.c_str());
                    LUA_END(0);
                }
                else
                    errorString=SIM_ERROR_INVALID_HANDLE;
            }
        }
        if (cmd.compare("getAssemblyMatchValues")==0)
        {
            if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0,lua_arg_bool,0))
            {
                int objHandle=luaToInt(L,2);
                bool childAttr=luaToBool(L,3);
                CSceneObject* it=App::currentWorld->sceneObjects->getObjectFromHandle(objHandle);
                if (it!=nullptr)
                {
                    std::string str(it->getAssemblyMatchValues(childAttr));
                    luaWrap_lua_pushstring(L,str.c_str());
                    LUA_END(1);
                }
                else
                    errorString=SIM_ERROR_INVALID_HANDLE;
            }
        }
#ifdef SIM_WITH_GUI
        if (cmd.compare("drawImageLines")==0)
        {
            if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_string,0,lua_arg_number,2,lua_arg_number,4,lua_arg_number,3,lua_arg_number,0))
            {
                int res[2];
                getIntsFromTable(L,3,2,res);
                size_t imgLen;
                const char* img=luaWrap_lua_tolstring(L,2,&imgLen);
                if (imgLen==size_t(3*res[0]*res[1]))
                {
                    std::vector<int> lines;
                    int vc=int(luaWrap_lua_rawlen(L,4));
                    vc=2*(vc/2);
                    lines.resize(vc);
                    getIntsFromTable(L,4,vc,&lines[0]);
                    int col[3];
                    getIntsFromTable(L,5,3,col);
                    int lineWidth=luaToInt(L,6);
                    QImage qimg((const unsigned char*)img,res[0],res[1],res[0]*3,QImage::Format_RGB888);
                    QPixmap pix(QPixmap::fromImage(qimg));
                    QPainter paint(&pix);
                    QColor qcol(col[0],col[1],col[2],255);
                    QPen pen(qcol);
                    pen.setWidth(lineWidth);
                    paint.setPen(pen);
                    for (size_t i=0;i<(lines.size()/2)-1;i++)
                        paint.drawLine(lines[2*i+0],lines[2*i+1],lines[2*i+2],lines[2*i+3]);
                    qimg=pix.toImage();
                    std::vector<unsigned char> img2;
                    img2.resize(res[0]*res[1]*3);
                    for (int y=0;y<res[1];y++)
                    {
                        for (int x=0;x<res[0];x++)
                        {
                            QRgb rgb=qimg.pixel(x,y);
                            img2[y*res[0]*3+3*x+0]=qRed(rgb);
                            img2[y*res[0]*3+3*x+1]=qGreen(rgb);
                            img2[y*res[0]*3+3*x+2]=qBlue(rgb);
                        }
                    }
                    luaWrap_lua_pushlstring(L,(const char*)&img2[0],res[0]*res[1]*3);
                    LUA_END(1);
                }
            }
        }
        if (cmd.compare("drawImageSquares")==0)
        {
            if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_string,0,lua_arg_number,2,lua_arg_number,2,lua_arg_number,3,lua_arg_number,0))
            {
                int res[2];
                getIntsFromTable(L,3,2,res);
                size_t imgLen;
                const char* img=luaWrap_lua_tolstring(L,2,&imgLen);
                if (imgLen==size_t(3*res[0]*res[1]))
                {
                    std::vector<int> rects;
                    int vc=int(luaWrap_lua_rawlen(L,4));
                    vc=4*(vc/4);
                    rects.resize(vc);
                    getIntsFromTable(L,4,vc,&rects[0]);
                    int col[3];
                    getIntsFromTable(L,5,3,col);
                    int lineWidth=luaToInt(L,6);
                    QImage qimg((const unsigned char*)img,res[0],res[1],res[0]*3,QImage::Format_RGB888);
                    QPixmap pix(QPixmap::fromImage(qimg));
                    QPainter paint(&pix);
                    QColor qcol(col[0],col[1],col[2],255);
                    QPen pen(qcol);
                    pen.setWidth(lineWidth);
                    paint.setPen(pen);
                    for (size_t i=0;i<rects.size()/4;i++)
                        paint.drawRect(rects[4*i+0],rects[4*i+1],rects[4*i+2],rects[4*i+3]);
                    qimg=pix.toImage();
                    std::vector<unsigned char> img2;
                    img2.resize(res[0]*res[1]*3);
                    for (int y=0;y<res[1];y++)
                    {
                        for (int x=0;x<res[0];x++)
                        {
                            QRgb rgb=qimg.pixel(x,y);
                            img2[y*res[0]*3+3*x+0]=qRed(rgb);
                            img2[y*res[0]*3+3*x+1]=qGreen(rgb);
                            img2[y*res[0]*3+3*x+2]=qBlue(rgb);
                        }
                    }
                    luaWrap_lua_pushlstring(L,(const char*)&img2[0],res[0]*res[1]*3);
                    LUA_END(1);
                }
            }
        }
#endif
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetReferencedHandles(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setReferencedHandles");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int objHandle=luaToInt(L,1);
        if (luaWrap_lua_istable(L,2))
        {
            int cnt=(int)luaWrap_lua_rawlen(L,2);
            if (cnt>0)
            {
                std::vector<int> handles;
                handles.resize(cnt);
                getIntsFromTable(L,2,cnt,&handles[0]);
                retVal=simSetReferencedHandles_internal(objHandle,cnt,&handles[0],nullptr,nullptr);
            }
            else
                retVal=simSetReferencedHandles_internal(objHandle,0,nullptr,nullptr,nullptr);
        }
        else
            errorString=SIM_ERROR_INVALID_ARGUMENT;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetReferencedHandles(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getReferencedHandles");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int objHandle=luaToInt(L,1);
        int* handles;
        int cnt=simGetReferencedHandles_internal(objHandle,&handles,nullptr,nullptr);
        if (cnt>=0)
        {
            pushIntTableOntoStack(L,cnt,handles);
            delete[] handles;
            LUA_END(1);
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetGraphCurve(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getGraphCurve");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int graphHandle=luaToInt(L,1);
        int graphType=luaToInt(L,2);
        int index=luaToInt(L,3);
        CGraph* graph=App::currentWorld->sceneObjects->getGraphFromHandle(graphHandle);
        if (graph!=nullptr)
        {
            std::vector<double> xVals;
            std::vector<double> yVals;
            std::string label;
            int curveType;
            int curveId;
            int curveWidth;
            float col[3];
            double minMax[6];
            if (graph->getGraphCurveData(graphType,index,label,xVals,yVals,curveType,col,minMax,curveId,curveWidth))
            {
                luaWrap_lua_pushstring(L,label.c_str());
                luaWrap_lua_pushinteger(L,curveType);
                pushFloatTableOntoStack(L,3,col);
                if (xVals.size()>0)
                    pushDoubleTableOntoStack(L,(int)xVals.size(),&xVals[0]);
                else
                    pushIntTableOntoStack(L,0,nullptr); // empty table
                if (yVals.size()>0)
                    pushDoubleTableOntoStack(L,(int)yVals.size(),&yVals[0]);
                else
                    pushIntTableOntoStack(L,0,nullptr); // empty table
                pushDoubleTableOntoStack(L,6,minMax);
                luaWrap_lua_pushinteger(L,curveId);
                luaWrap_lua_pushinteger(L,curveWidth);
                LUA_END(8);
            }
        }
        else
            errorString=SIM_ERROR_OBJECT_NOT_GRAPH;
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetGraphInfo(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getGraphInfo");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int graphHandle=luaToInt(L,1);
        CGraph* graph=App::currentWorld->sceneObjects->getGraphFromHandle(graphHandle);
        if (graph!=nullptr)
        {
            int bitCoded=0;
            luaWrap_lua_pushinteger(L,bitCoded);
            pushFloatTableOntoStack(L,3,graph->backgroundColor);
            pushFloatTableOntoStack(L,3,graph->textColor);
            LUA_END(3);
        }
        else
            errorString=SIM_ERROR_OBJECT_NOT_GRAPH;
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetShapeViz(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getShapeViz");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int shapeHandle=luaToInt(L,1);
        int index=luaToInt(L,2);
        SShapeVizInfof info;
        int ret=simGetShapeVizf_internal(shapeHandle+sim_handleflag_extended,index,&info);
        if (ret>0)
        {
            CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
            stack->pushTableOntoStack();

            stack->insertKeyFloatArrayIntoStackTable("vertices",info.vertices,size_t(info.verticesSize));
            stack->insertKeyInt32ArrayIntoStackTable("indices",info.indices,size_t(info.indicesSize));
            stack->insertKeyFloatArrayIntoStackTable("normals",info.normals,size_t(info.indicesSize*3));
            stack->insertKeyFloatArrayIntoStackTable("colors",info.colors,9);
            stack->insertKeyFloatIntoStackTable("shadingAngle",info.shadingAngle);
            stack->insertKeyFloatIntoStackTable("transparency",info.transparency);
            stack->insertKeyInt32IntoStackTable("options",info.options);

            delete[] info.vertices;
            delete[] info.indices;
            delete[] info.normals;
            if (ret>1)
            {
                stack->pushStringOntoStack("texture",0);
                stack->pushTableOntoStack();

                stack->insertKeyStringIntoStackTable("texture",info.texture,4*info.textureRes[0]*info.textureRes[1]);
                stack->insertKeyInt32ArrayIntoStackTable("resolution",info.textureRes,2);
                stack->insertKeyFloatArrayIntoStackTable("coordinates",info.textureCoords,size_t(info.indicesSize*2));
                stack->insertKeyInt32IntoStackTable("applyMode",info.textureApplyMode);
                stack->insertKeyInt32IntoStackTable("options",info.textureOptions);
                stack->insertKeyInt32IntoStackTable("id",info.textureId);

                stack->insertDataIntoStackTable();
                delete[] info.texture;
                delete[] info.textureCoords;
            }
            CScriptObject::buildOntoInterpreterStack_lua(L,stack,true);
            App::worldContainer->interfaceStackContainer->destroyStack(stack);
            LUA_END(1);
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simExecuteScriptString(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.executeScriptString");

    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0))
    {
        std::string strAndScriptName(luaWrap_lua_tostring(L,1));
        int scriptHandleOrType=luaToInt(L,2);
        CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
        int retVal=simExecuteScriptString_internal(scriptHandleOrType,strAndScriptName.c_str(),stack->getId());
        if (retVal>=0)
        {
            luaWrap_lua_pushinteger(L,retVal);
            int s=1;
            if (stack->getStackSize()>0)
            {
                CScriptObject::buildOntoInterpreterStack_lua(L,stack,false);//true);
                s+=stack->getStackSize();
            }
            App::worldContainer->interfaceStackContainer->destroyStack(stack);
            if (s==1)
            {
                pushIntTableOntoStack(L,0,nullptr);
                s++;
            }
            LUA_END(s);
        }
        else
            App::worldContainer->interfaceStackContainer->destroyStack(stack);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}


int _simGetApiFunc(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getApiFunc");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
    {
        int scriptHandleOrType=luaToInt(L,1);
        std::string apiWord(luaWrap_lua_tostring(L,2));
        char* str=simGetApiFunc_internal(scriptHandleOrType,apiWord.c_str());
        std::vector<std::string> strTable;
        if (str!=nullptr)
        {
            std::string sstr(str);
            simReleaseBuffer_internal(str);
            size_t prevPos=0;
            size_t spacePos=sstr.find(' ',prevPos);
            while (spacePos!=std::string::npos)
            {
                strTable.push_back(std::string(sstr.begin()+prevPos,sstr.begin()+spacePos));
                prevPos=spacePos+1;
                spacePos=sstr.find(' ',prevPos);
            }
            strTable.push_back(std::string(sstr.begin()+prevPos,sstr.end()));
        }
        pushStringTableOntoStack(L,strTable);
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetApiInfo(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getApiInfo");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
    {
        int scriptHandleOrType=luaToInt(L,1);
        std::string apiWord(luaWrap_lua_tostring(L,2));
        char* str=simGetApiInfo_internal(scriptHandleOrType,apiWord.c_str());
        if (str!=nullptr)
        {
            luaWrap_lua_pushstring(L,str);
            simReleaseBuffer_internal(str);
            LUA_END(1);
        }
        LUA_END(0);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetModuleInfo(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getModuleInfo");

    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0))
    {
        char* stringInfo;
        int intInfo;
        std::string moduleName(luaWrap_lua_tostring(L,1));
        int infoType=luaToInt(L,2);
        int res=simGetModuleInfo_internal(moduleName.c_str(),infoType,&stringInfo,&intInfo);
        if (res>=0)
        {
            if ( (infoType==sim_moduleinfo_extversionstr)||(infoType==sim_moduleinfo_builddatestr) )
            {
                luaWrap_lua_pushstring(L,stringInfo);
                delete[] stringInfo;
                LUA_END(1);
            }
            if ( (infoType==sim_moduleinfo_extversionint)||(infoType==sim_moduleinfo_verbosity)||(infoType==sim_moduleinfo_statusbarverbosity) )
            {
                luaWrap_lua_pushinteger(L,intInfo);
                LUA_END(1);
            }
        }
        LUA_END(0);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetModuleInfo(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.setModuleInfo");

    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0))
    {
        std::string moduleName(luaWrap_lua_tostring(L,1));
        int infoType=luaToInt(L,2);
        if ( (infoType==sim_moduleinfo_verbosity)||(infoType==sim_moduleinfo_statusbarverbosity) )
        {
            int res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,false,false,&errorString);
            if (res==2)
            {
                int verbosity=luaToInt(L,3);
                simSetModuleInfo_internal(moduleName.c_str(),infoType,nullptr,verbosity);
            }
            else
                errorString=SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG;
        }
        else
            errorString=SIM_ERROR_INVALID_MODULE_INFO_TYPE;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simRegisterScriptFunction(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.registerScriptFunction");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_string,0))
    {
        std::string funcNameAtPluginName(luaWrap_lua_tostring(L,1));
        std::string ct(luaWrap_lua_tostring(L,2));
        ct="####"+ct;
        retVal=simRegisterScriptCallbackFunction_internal(funcNameAtPluginName.c_str(),ct.c_str(),nullptr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simRegisterScriptVariable(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.registerScriptVariable");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        const char* varNameAtPluginName=luaWrap_lua_tostring(L,1);
        if (checkOneGeneralInputArgument(L,2,lua_arg_string,0,true,false,&errorString)==2)
            retVal=simRegisterScriptVariable_internal(varNameAtPluginName,luaWrap_lua_tostring(L,2),-1);
        else
            retVal=simRegisterScriptVariable_internal(varNameAtPluginName,nullptr,0);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simRegisterScriptFuncHook(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.registerScriptFuncHook");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_string,0,lua_arg_bool,0))
    {
        const char* systemFunc=luaWrap_lua_tostring(L,1);
        const char* userFunc=luaWrap_lua_tostring(L,2);
        bool execBefore=luaToBool(L,3);
        retVal=simRegisterScriptFuncHook_internal(CScriptObject::getScriptHandleFromInterpreterState_lua(L),systemFunc,userFunc,execBefore,0);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simIsDeprecated(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.isDeprecated");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
        retVal=simIsDeprecated_internal(luaWrap_lua_tostring(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetPersistentDataTags(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getPersistentDataTags");

    int tagCount;
    char* data=simGetPersistentDataTags_internal(&tagCount);
    if (data!=nullptr)
    {
        std::vector<std::string> stringTable;
        size_t off=0;
        for (int i=0;i<tagCount;i++)
        {
            stringTable.push_back(data+off);
            off+=strlen(data+off)+1;
        }
        pushStringTableOntoStack(L,stringTable);
        simReleaseBuffer_internal(data);
        LUA_END(1);
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetRandom(luaWrap_lua_State* L)
{
    TRACE_LUA_API;
    LUA_START("sim.getRandom");

    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (it!=nullptr)
    {
        int res=checkOneGeneralInputArgument(L,1,lua_arg_number,0,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                unsigned int s=abs(luaToInt(L,1));
                it->setRandomSeed(s);
            }
            luaWrap_lua_pushnumber(L,it->getRandomDouble());
            LUA_END(1);
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

//****************************************************
//****************************************************
//****************************************************
//****************************************************
//****************************************************
//****************************************************

const SLuaCommands simLuaCommandsOldApi[]=
{ // Following for backward compatibility (has now a new notation (see 'simLuaCommands'))
    {"simGetScriptName",_simGetScriptName,                      "Deprecated. Use sim.getObjectAlias instead",false},
    {"simGetObjectAssociatedWithScript",_simGetObjectAssociatedWithScript,"Deprecated. Use sim.getObjectHandle('.') instead",false},
    {"simGetScriptAssociatedWithObject",_simGetScriptAssociatedWithObject,"Deprecated. Use sim.getScriptHandle instead",false},
    {"simGetCustomizationScriptAssociatedWithObject",_simGetCustomizationScriptAssociatedWithObject,"Deprecated. Use sim.getScriptHandle instead",false},
    {"simOpenModule",_simOpenModule,                            "Deprecated",false},
    {"simCloseModule",_simCloseModule,                          "Deprecated",false},
    {"simHandleModule",_simHandleModule,                        "Deprecated",false},
    {"simHandleDynamics",_simHandleDynamics,                    "Use the newer sim.handleDynamics notation",false},
    {"simHandleProximitySensor",_simHandleProximitySensor,      "Use the newer sim.handleProximitySensor notation",false},
    {"simReadProximitySensor",_simReadProximitySensor,          "Use the newer sim.readProximitySensor notation",false},
    {"simResetProximitySensor",_simResetProximitySensor,        "Use the newer sim.resetProximitySensor notation",false},
    {"simCheckProximitySensor",_simCheckProximitySensor,        "Use the newer sim.checkProximitySensor notation",false},
    {"simCheckProximitySensorEx",_simCheckProximitySensorEx,    "Use the newer sim.checkProximitySensorEx notation",false},
    {"simCheckProximitySensorEx2",_simCheckProximitySensorEx2,  "Use the newer sim.checkProximitySensorEx2 notation",false},
    {"simGetObjectHandle",_sim_getObjectHandle,                 "Use the newer sim.getObjectHandle notation",false},
    {"simAddScript",_simAddScript,                              "Use the newer sim.addScript notation",false},
    {"simAssociateScriptWithObject",_simAssociateScriptWithObject,"Use the newer sim.associateScriptWithObject notation",false},
    {"simSetScriptText",_simSetScriptText,                      "Deprecated. Use sim.setScriptStringParam instead",false},
    {"simGetScriptHandle",_simGetScriptHandle,                  "Deprecated. Use sim.getScript instead",false},
    {"simGetObjectPosition",_simGetObjectPosition,              "Use the newer sim.getObjectPosition notation",false},
    {"simGetObjectOrientation",_simGetObjectOrientation,        "Use the newer sim.getObjectOrientation notation",false},
    {"simSetObjectPosition",_simSetObjectPosition,              "Use the newer sim.setObjectPosition notation",false},
    {"simSetObjectOrientation",_simSetObjectOrientation,        "Use the newer sim.setObjectOrientation notation",false},
    {"simGetJointPosition",_simGetJointPosition,                "Use the newer sim.getJointPosition notation",false},
    {"simSetJointPosition",_simSetJointPosition,                "Use the newer sim.setJointPosition notation",false},
    {"simSetJointTargetPosition",_simSetJointTargetPosition,    "Use the newer sim.setJointTargetPosition notation",false},
    {"simGetJointTargetPosition",_simGetJointTargetPosition,    "Use the newer sim.getJointTargetPosition notation",false},
    {"simSetJointForce",_simSetJointMaxForce,                   "Deprecated. Use sim.setJointTargetForce instead",false},
    {"simSetJointTargetVelocity",_simSetJointTargetVelocity,    "Use the newer sim.setJointTargetVelocity notation",false},
    {"simGetJointTargetVelocity",_simGetJointTargetVelocity,    "Use the newer sim.getJointTargetVelocity notation",false},
    {"simGetObjectName",_simGetObjectName,                      "Deprecated. Use sim.getObjectAlias instead",false},
    {"simSetObjectName",_simSetObjectName,                      "Deprecated. Use sim.setObjectAlias instead",false},
    {"simRemoveObject",_simRemoveObject,                        "Use the newer sim.removeObject notation",false},
    {"simRemoveModel",_simRemoveModel,                          "Use the newer sim.removeModel notation",false},
    {"simGetSimulationTime",_simGetSimulationTime,              "Use the newer sim.getSimulationTime notation",false},
    {"simGetSimulationState",_simGetSimulationState,            "Use the newer sim.getSimulationState notation",false},
    {"simGetSystemTime",_simGetSystemTime,                      "Use the newer sim.getSystemTime notation",false},
    {"simGetSystemTimeInMs",_simGetSystemTimeInMs,              "Deprecated. Use sim.getSystemTime instead",false},
    {"simCheckCollision",_simCheckCollision,                    "Use the newer sim.checkCollision notation",false},
    {"simCheckCollisionEx",_simCheckCollisionEx,                "Use the newer sim.checkCollisionEx notation",false},
    {"simCheckDistance",_simCheckDistance,                      "Use the newer sim.checkDistance notation",false},
    {"simGetObjectConfiguration",_simGetObjectConfiguration,    "Deprecated",false},
    {"simSetObjectConfiguration",_simSetObjectConfiguration,    "Deprecated",false},
    {"simGetConfigurationTree",_simGetConfigurationTree,        "Deprecated",false},
    {"simSetConfigurationTree",_simSetConfigurationTree,        "Deprecated",false},
    {"simGetSimulationTimeStep",_simGetSimulationTimeStep,      "Use the newer sim.getSimulationTimeStep notation",false},
    {"simGetSimulatorMessage",_simGetSimulatorMessage,          "Use the newer sim.getSimulatorMessage notation",false},
    {"simResetGraph",_simResetGraph,                            "Use the newer sim.resetGraph notation",false},
    {"simHandleGraph",_simHandleGraph,                          "Use the newer sim.handleGraph notation",false},
    {"simGetLastError",_simGetLastError,                        "Deprecated",false},
    {"simGetObjects",_simGetObjects,                            "Use the newer sim.getObjects notation",false},
    {"simRefreshDialogs",_simRefreshDialogs,                    "Use the newer sim.refreshDialogs notation",false},
    {"simGetModuleName",_simGetModuleName,                      "Use the newer sim.getModuleName notation",false},
    {"simRemoveScript",_simRemoveScript,                        "Use the newer sim.removeScript notation",false},
    {"simStopSimulation",_simStopSimulation,                    "Use the newer sim.stopSimulation notation",false},
    {"simPauseSimulation",_simPauseSimulation,                  "Use the newer sim.pauseSimulation notation",false},
    {"simStartSimulation",_simStartSimulation,                  "Use the newer sim.startSimulation notation",false},
    {"simGetObjectMatrix",_simGetObjectMatrix,                  "Use the newer sim.getObjectMatrix notation",false},
    {"simSetObjectMatrix",_simSetObjectMatrix,                  "Use the newer sim.setObjectMatrix notation",false},
    {"simGetJointMatrix",_simGetJointMatrix,                    "Deprecated. Use sim.getObjectChildPose instead",false},
    {"simSetSphericalJointMatrix",_simSetSphericalJointMatrix,  "Deprecated. Use sim.setObjectChildPose instead",false},
    {"simBuildIdentityMatrix",_simBuildIdentityMatrix,          "Use the newer sim.buildIdentityMatrix notation",false},
    {"simBuildMatrix",_simBuildMatrix,                          "Use the newer sim.buildMatrix notation",false},
    {"simGetEulerAnglesFromMatrix",_simGetEulerAnglesFromMatrix,"Use the newer sim.getEulerAnglesFromMatrix notation",false},
    {"simInvertMatrix",_simInvertMatrix,                        "Use the newer sim.invertMatrix notation",false},
    {"simMultiplyMatrices",_simMultiplyMatrices,                "Use the newer sim.multiplyMatrices notation",false},
    {"simInterpolateMatrices",_simInterpolateMatrices,          "Use the newer sim.interpolateMatrices notation",false},
    {"simMultiplyVector",_simMultiplyVector,                    "Use the newer sim.multiplyVector notation",false},
    {"simGetObjectChild",_simGetObjectChild,                    "Use the newer sim.getObjectChild notation",false},
    {"simGetObjectParent",_simGetObjectParent,                  "Use the newer sim.getObjectParent notation",false},
    {"simSetObjectParent",_simSetObjectParent,                  "Use the newer sim.setObjectParent notation",false},
    {"simGetObjectType",_simGetObjectType,                      "Use the newer sim.getObjectType notation",false},
    {"simGetJointType",_simGetJointType,                        "Use the newer sim.getJointType notation",false},
    {"simSetBoolParameter",_simSetBoolParam,                    "Deprecated. Use sim.setBoolParam instead",false},
    {"simGetBoolParameter",_simGetBoolParam,                    "Deprecated. Use sim.getBoolParam instead",false},
    {"simSetInt32Parameter",_simSetInt32Param,                  "Deprecated. Use sim.setInt32Param instead",false},
    {"simGetInt32Parameter",_simGetInt32Param,                  "Deprecated. Use sim.getInt32Param instead",false},
    {"simSetFloatParameter",_simSetFloatParam,                  "Deprecated. Use sim.setFloatParam instead",false},
    {"simGetFloatParameter",_simGetFloatParam,                  "Deprecated. Use sim.getFloatParam instead",false},
    {"simSetStringParameter",_simSetStringParam,                "Deprecated. Use sim.setStringParam instead",false},
    {"simGetStringParameter",_simGetStringParam,                "Deprecated. Use sim.getStringParam instead",false},
    {"simSetArrayParameter",_simSetArrayParam,                  "Deprecated. Use sim.setArrayParam instead",false},
    {"simGetArrayParameter",_simGetArrayParam,                  "Deprecated. Use sim.getArrayParam instead",false},
    {"simGetJointInterval",_simGetJointInterval,                "Use the newer sim.getJointInterval notation",false},
    {"simSetJointInterval",_simSetJointInterval,                "Use the newer sim.setJointInterval notation",false},
    {"simLoadScene",_simLoadScene,                              "Use the newer sim.loadScene notation",false},
    {"simSaveScene",_simSaveScene,                              "Use the newer sim.saveScene notation",false},
    {"simLoadModel",_simLoadModel,                              "Use the newer sim.loadModel notation",false},
    {"simSaveModel",_simSaveModel,                              "Use the newer sim.saveModel notation",false},
    {"simIsObjectInSelection",_simIsObjectInSelection,          "Deprecated. Use sim.getObjectSel instead",false},
    {"simAddObjectToSelection",_simAddObjectToSelection,        "Deprecated. Use sim.setObjectSel instead",false},
    {"simRemoveObjectFromSelection",_simRemoveObjectFromSelection,"Deprecated. Use sim.setObjectSelection instead",false},
    {"simGetObjectSelection",_simGetObjectSel,                  "Deprecated. Use sim.getObjectSel instead",false},
    {"simGetRealTimeSimulation",_simGetRealTimeSimulation,      "Use the newer sim.getIsRealTimeSimulation notation",false},
    {"simSetNavigationMode",_simSetNavigationMode,              "Use the newer sim.setNavigationMode notation",false},
    {"simGetNavigationMode",_simGetNavigationMode,              "Use the newer sim.getNavigationMode notation",false},
    {"simSetPage",_simSetPage,                                  "Use the newer sim.setPage notation",false},
    {"simGetPage",_simGetPage,                                  "Use the newer sim.getPage notation",false},
    {"simCopyPasteObjects",_simCopyPasteObjects,                "Use the newer sim.copyPasteObjects notation",false},
    {"simScaleObjects",_simScaleObjects,                        "Use the newer sim.scaleObjects notation",false},
    {"simGetObjectUniqueIdentifier",_simGetObjectUniqueIdentifier,"Use the newer sim.getObjectUniqueIdentifier notation",false},
    {"simSetThreadAutomaticSwitch",_simSetThreadAutomaticSwitch,"Use the newer sim.setThreadAutomaticSwitch notation",false},
    {"simGetThreadAutomaticSwitch",_simGetThreadAutomaticSwitch,"Use the newer sim.getThreadAutomaticSwitch notation",false},
    {"simSetThreadSwitchTiming",_simSetThreadSwitchTiming,      "Use the newer sim.setThreadSwitchTiming notation",false},
    {"simSwitchThread",_simSwitchThread,                        "Deprecated",false},
    {"simSaveImage",_simSaveImage,                              "Use the newer sim.saveImage notation",false},
    {"simLoadImage",_simLoadImage,                              "Use the newer sim.loadImage notation",false},
    {"simGetScaledImage",_simGetScaledImage,                    "Use the newer sim.getScaledImage notation",false},
    {"simTransformImage",_simTransformImage,                    "Use the newer sim.transformImage notation",false},
    {"simGetQHull",_simGetQHull,                                "Use the newer sim.getQHull notation",false},
    {"simGetDecimatedMesh",_simGetDecimatedMesh,                "Use the newer sim.getDecimatedMesh notation",false},
    {"simPackInt32Table",_simPackInt32Table,                    "Use the newer sim.packInt32Table notation",false},
    {"simPackUInt32Table",_simPackUInt32Table,                  "Use the newer sim.packUInt32Table notation",false},
    {"simPackFloatTable",_simPackFloatTable,                    "Use the newer sim.packFloatTable notation",false},
    {"simPackDoubleTable",_simPackDoubleTable,                  "Use the newer sim.packDoubleTable notation",false},
    {"simPackUInt8Table",_simPackUInt8Table,                    "Use the newer sim.packUInt8Table notation",false},
    {"simPackUInt16Table",_simPackUInt16Table,                  "Use the newer sim.packUInt16Table notation",false},
    {"simUnpackInt32Table",_simUnpackInt32Table,                "Use the newer sim.unpackInt32Table notation",false},
    {"simUnpackUInt32Table",_simUnpackUInt32Table,              "Use the newer sim.unpackUInt32Table notation",false},
    {"simUnpackFloatTable",_simUnpackFloatTable,                "Use the newer sim.unpackFloatTable notation",false},
    {"simUnpackDoubleTable",_simUnpackDoubleTable,              "Use the newer sim.unpackDoubleTable notation",false},
    {"simUnpackUInt8Table",_simUnpackUInt8Table,                "Use the newer sim.unpackUInt8Table notation",false},
    {"simUnpackUInt16Table",_simUnpackUInt16Table,              "Use the newer sim.unpackUInt16Table notation",false},
    {"simPackTable",_simPackTable,                              "Use the newer sim.packTable notation",false},
    {"simUnpackTable",_simUnpackTable,                          "Use the newer sim.unpackTable notation",false},
    {"simTransformBuffer",_simTransformBuffer,                  "Use the newer sim.transformBuffer notation",false},
    {"simCombineRgbImages",_simCombineRgbImages,                "Use the newer sim.combineRgbImages notation",false},
    {"simGetVelocity",_simGetVelocity,                          "Use the newer sim.getVelocity notation",false},
    {"simGetObjectVelocity",_simGetObjectVelocity,              "Use the newer sim.getObjectVelocity notation",false},
    {"simAddForceAndTorque",_simAddForceAndTorque,              "Use the newer sim.addForceAndTorque notation",false},
    {"simAddForce",_simAddForce,                                "Use the newer sim.addForce notation",false},
    {"simSetExplicitHandling",_simSetExplicitHandling,          "Use the newer sim.setExplicitHandling notation",false},
    {"simGetExplicitHandling",_simGetExplicitHandling,          "Use the newer sim.getExplicitHandling notation",false},
    {"simAddDrawingObject",_simAddDrawingObject,                "Use the newer sim.addDrawingObject notation",false},
    {"simRemoveDrawingObject",_simRemoveDrawingObject,          "Use the newer sim.removeDrawingObject notation",false},
    {"simAddDrawingObjectItem",_simAddDrawingObjectItem,        "Use the newer sim.addDrawingObjectItem notation",false},
    {"simAddParticleObject",_simAddParticleObject,              "Use the newer sim.addParticleObject notation",false},
    {"simRemoveParticleObject",_simRemoveParticleObject,        "Use the newer sim.removeParticleObject notation",false},
    {"simAddParticleObjectItem",_simAddParticleObjectItem,      "Use the newer sim.addParticleObjectItem notation",false},
    {"simGetObjectSizeFactor",_simGetObjectSizeFactor,          "Use the newer sim.getObjectSizeFactor notation",false},
    {"simSetIntegerSignal",_simSetInt32Signal,                  "Deprecated. Use sim.setInt32Signal instead",false},
    {"simGetIntegerSignal",_simGetInt32Signal,                  "Deprecated. Use sim.getInt32Signal instead",false},
    {"simClearIntegerSignal",_simClearInt32Signal,              "Deprecated. Use sim.clearInt32Signal instead",false},
    {"simSetFloatSignal",_simSetFloatSignal,                    "Use the newer sim.setFloatSignal notation",false},
    {"simGetFloatSignal",_simGetFloatSignal,                    "Use the newer sim.getFloatSignal notation",false},
    {"simClearFloatSignal",_simClearFloatSignal,                "Use the newer sim.clearFloatSignal notation",false},
    {"simSetStringSignal",_simSetStringSignal,                  "Use the newer sim.setStringSignal notation",false},
    {"simGetStringSignal",_simGetStringSignal,                  "Use the newer sim.getStringSignal notation",false},
    {"simClearStringSignal",_simClearStringSignal,              "Use the newer sim.clearStringSignal notation",false},
    {"simGetSignalName",_simGetSignalName,                      "Use the newer sim.getSignalName notation",false},
    {"simPersistentDataWrite",_simPersistentDataWrite,          "Use the newer sim.persistentDataWrite notation",false},
    {"simPersistentDataRead",_simPersistentDataRead,            "Use the newer sim.persistentDataRead notation",false},
    {"simSetObjectProperty",_simSetObjectProperty,              "Use the newer sim.setObjectProperty notation",false},
    {"simGetObjectProperty",_simGetObjectProperty,              "Use the newer sim.getObjectProperty notation",false},
    {"simSetObjectSpecialProperty",_simSetObjectSpecialProperty,"Use the newer sim.setObjectSpecialProperty notation",false},
    {"simGetObjectSpecialProperty",_simGetObjectSpecialProperty,"Use the newer sim.getObjectSpecialProperty notation",false},
    {"simSetModelProperty",_simSetModelProperty,                "Use the newer sim.setModelProperty notation",false},
    {"simGetModelProperty",_simGetModelProperty,                "Use the newer sim.getModelProperty notation",false},
    {"simReadForceSensor",_simReadForceSensor,                  "Use the newer sim.readForceSensor notation",false},
    {"simBreakForceSensor",_simBreakForceSensor,                "Deprecated. Use sim.setObjectParent instead",false},
    {"simGetLightParameters",_simGetLightParameters,            "Use the newer sim.getLightParameters notation",false},
    {"simSetLightParameters",_simSetLightParameters,            "Use the newer sim.setLightParameters notation",false},
    {"simGetLinkDummy",_simGetLinkDummy,                        "Use the newer sim.getLinkDummy notation",false},
    {"simSetLinkDummy",_simSetLinkDummy,                        "Use the newer sim.setLinkDummy notation",false},
    {"simSetShapeColor",_simSetShapeColor,                      "Use the newer sim.setShapeColor notation",false},
    {"simGetShapeColor",_simGetShapeColor,                      "Use the newer sim.getShapeColor notation",false},
    {"simResetDynamicObject",_simResetDynamicObject,            "Use the newer sim.resetDynamicObject notation",false},
    {"simSetJointMode",_simSetJointMode,                        "Use the newer sim.setJointMode notation",false},
    {"simGetJointMode",_simGetJointMode,                        "Use the newer sim.getJointMode notation",false},
    {"simSerialSend",_simSerialSend,                            "Use the newer sim.serialSend notation",false},
    {"simSerialCheck",_simSerialCheck,                          "Use the newer sim.serialCheck notation",false},
    {"simGetContactInfo",_simGetContactInfo,                    "Use the newer sim.getContactInfo notation",false},
    {"simAuxiliaryConsoleOpen",_simAuxiliaryConsoleOpen,        "Use the newer sim.auxiliaryConsoleOpen notation",false},
    {"simAuxiliaryConsoleClose",_simAuxiliaryConsoleClose,      "Use the newer sim.auxiliaryConsoleClose notation",false},
    {"simAuxiliaryConsolePrint",_simAuxiliaryConsolePrint,      "Use the newer sim.auxiliaryConsolePrint notation",false},
    {"simAuxiliaryConsoleShow",_simAuxiliaryConsoleShow,        "Use the newer sim.auxiliaryConsoleShow notation",false},
    {"simImportShape",_simImportShape,                          "Use the newer sim.importShape notation",false},
    {"simImportMesh",_simImportMesh,                            "Use the newer sim.importMesh notation",false},
    {"simExportMesh",_simExportMesh,                            "Use the newer sim.exportMesh notation",false},
    {"simCreateMeshShape",_simCreateMeshShape,                  "Use the newer sim.createMeshShape notation",false},
    {"simGetShapeMesh",_simGetShapeMesh,                        "Use the newer sim.getShapeMesh notation",false},
    {"simCreatePureShape",_simCreatePureShape,                  "Use the newer sim.createPureShape notation",false},
    {"simCreateHeightfieldShape",_simCreateHeightfieldShape,    "Use the newer sim.createHeightfieldShape notation",false},
    {"simCreateJoint",_simCreateJoint,                          "Use the newer sim.createJoint notation",false},
    {"simCreateDummy",_simCreateDummy,                          "Use the newer sim.createDummy notation",false},
    {"simCreateProximitySensor",_simCreateProximitySensor,      "Use the newer sim.createProximitySensor notation",false},
    {"simCreateForceSensor",_simCreateForceSensor,              "Use the newer sim.createForceSensor notation",false},
    {"simCreateVisionSensor",_simCreateVisionSensor,            "Use the newer sim.createVisionSensor notation",false},
    {"simFloatingViewAdd",_simFloatingViewAdd,                  "Use the newer sim.floatingViewAdd notation",false},
    {"simFloatingViewRemove",_simFloatingViewRemove,            "Use the newer sim.floatingViewRemove notation",false},
    {"simAdjustView",_simAdjustView,                            "Use the newer sim.adjustView notation",false},
    {"simCameraFitToView",_simCameraFitToView,                  "Use the newer sim.cameraFitToView notation",false},
    {"simAnnounceSceneContentChange",_simAnnounceSceneContentChange,"Use the newer sim.announceSceneContentChange notation",false},
    {"simGetObjectInt32Parameter",_simGetObjectInt32Parameter,  "Deprecated. Use sim.getObjectInt32Param instead",false},
    {"simSetObjectInt32Parameter",_simSetObjectInt32Param,      "Deprecated. Use sim.setObjectInt32Param instead",false},
    {"simGetObjectFloatParameter",_simGetObjectFloatParameter,  "Deprecated. Use sim.getObjectFloatParam instead",false},
    {"simSetObjectFloatParameter",_simSetObjectFloatParam,      "Deprecated. Use sim.setObjectFloatParam instead",false},
    {"simGetObjectStringParameter",_simGetObjectStringParam,    "Deprecated. Use sim.getObjectStringParam instead",false},
    {"simSetObjectStringParameter",_simSetObjectStringParam,    "Deprecated. Use sim.setObjectStringParam instead",false},
    {"simGetRotationAxis",_simGetRotationAxis,                  "Use the newer sim.getRotationAxis notation",false},
    {"simRotateAroundAxis",_simRotateAroundAxis,                "Use the newer sim.rotateAroundAxis notation",false},
    {"simLaunchExecutable",_simLaunchExecutable,                "Use the newer sim.launchExecutable notation",false},
    {"simGetJointForce",_simGetJointForce,                      "Use the newer sim.getJointForce notation",false},
    {"simIsHandleValid",_simIsHandleValid,                      "Deprecated. Use sim.isHandle instead",false},
    {"simGetObjectQuaternion",_simGetObjectQuaternion,          "Use the newer sim.getObjectQuaternion notation",false},
    {"simSetObjectQuaternion",_simSetObjectQuaternion,          "Use the newer sim.setObjectQuaternion notation",false},
    {"simSetShapeMassAndInertia",_simSetShapeMassAndInertia,    "Deprecated. Use sim.setShapeMass and/or sim.setShapeInertia instead",false},
    {"simGetShapeMassAndInertia",_simGetShapeMassAndInertia,    "Deprecated. Use sim.getShapeMass and/or sim.getShapeInertia instead",false},
    {"simGroupShapes",_simGroupShapes,                          "Use the newer sim.groupShapes notation",false},
    {"simUngroupShape",_simUngroupShape,                        "Use the newer sim.ungroupShape notation",false},
    {"simQuitSimulator",_simQuitSimulator,                      "Use the newer sim.quitSimulator notation",false},
    {"simGetThreadId",_simGetThreadId,                          "Use the newer sim.getThreadId notation",false},
    {"simSetShapeMaterial",_simSetShapeMaterial,                "Use the newer sim.setShapeMaterial notation",false},
    {"simGetTextureId",_simGetTextureId,                        "Use the newer sim.getTextureId notation",false},
    {"simReadTexture",_simReadTexture,                          "Use the newer sim.readTexture notation",false},
    {"simWriteTexture",_simWriteTexture,                        "Use the newer sim.writeTexture notation",false},
    {"simCreateTexture",_simCreateTexture,                      "Use the newer sim.createTexture notation",false},
    {"simWriteCustomDataBlock",_simWriteCustomDataBlock,        "Use the newer sim.writeCustomDataBlock notation",false},
    {"simReadCustomDataBlock",_simReadCustomDataBlock,          "Use the newer sim.readCustomDataBlock notation",false},
    {"simReadCustomDataBlockTags",_simReadCustomDataBlockTags,  "Use the newer sim.readCustomDataBlockTags notation",false},
    {"simGetShapeGeomInfo",_simGetShapeGeomInfo,                "Use the newer sim.getShapeGeomInfo notation",false},
    {"simGetObjectsInTree",_simGetObjectsInTree,                "Use the newer sim.getObjectsInTree notation",false},
    {"simSetObjectSizeValues",_simSetObjectSizeValues,          "Deprecated.",false},
    {"simGetObjectSizeValues",_simGetObjectSizeValues,          "Deprecated.",false},
    {"simScaleObject",_simScaleObject,                          "Use the newer sim.scaleObject notation",false},
    {"simSetShapeTexture",_simSetShapeTexture,                  "Use the newer sim.setShapeTexture notation",false},
    {"simGetShapeTextureId",_simGetShapeTextureId,              "Use the newer sim.getShapeTextureId notation",false},
    {"simGetCollectionObjects",_simGetCollectionObjects,        "Use the newer sim.getCollectionObjects notation",false},
    {"simHandleCustomizationScripts",_simHandleCustomizationScripts,"Use the newer sim.handleCustomizationScripts notation",false},
    {"simSetScriptAttribute",_simSetScriptAttribute,            "Deprecated. use sim.setScriptXXXParam instead",false},
    {"simGetScriptAttribute",_simGetScriptAttribute,            "Deprecated. use sim.getScriptXXXParam instead",false},
    {"simHandleChildScripts",_simHandleChildScripts,            "Use the newer sim.handleChildScripts notation",false},
    {"simReorientShapeBoundingBox",_simReorientShapeBoundingBox,"Use the newer sim.reorientShapeBoundingBox notation",false},
    {"simHandleVisionSensor",_simHandleVisionSensor,            "Use the newer sim.handleVisionSensor notation",false},
    {"simReadVisionSensor",_simReadVisionSensor,                "Use the newer sim.readVisionSensor notation",false},
    {"simResetVisionSensor",_simResetVisionSensor,              "Use the newer sim.resetVisionSensor notation",false},
    {"simGetVisionSensorResolution",_simGetVisionSensorResolution,"Use the newer sim.getVisionSensorResolution notation",false},
    {"simGetVisionSensorImage",_simGetVisionSensorImage,        "Use the newer sim.getVisionSensorImage notation",false},
    {"simSetVisionSensorImage",_simSetVisionSensorImage,        "Use the newer sim.setVisionSensorImage notation",false},
    {"simGetVisionSensorCharImage",_simGetVisionSensorCharImage,"Use the newer sim.getVisionSensorCharImage notation",false},
    {"simSetVisionSensorCharImage",_simSetVisionSensorCharImage,"Use the newer sim.setVisionSensorCharImage notation",false},
    {"simGetVisionSensorDepthBuffer",_simGetVisionSensorDepthBuffer,"Use the newer sim.getVisionSensorDepthBuffer notation",false},
    {"simCheckVisionSensor",_simCheckVisionSensor,              "Use the newer sim.checkVisionSensor notation",false},
    {"simCheckVisionSensorEx",_simCheckVisionSensorEx,          "Use the newer sim.checkVisionSensorEx notation",false},
    {"simRMLPos",_simRuckigPos,                                 "Deprecated. Use sim.ruckigPos instead",false},
    {"simRMLVel",_simRuckigVel,                                 "Deprecated. Use sim.ruckigVel instead",false},
    {"simRMLStep",_simRuckigStep,                               "Deprecated. Use sim.ruckigStep instead",false},
    {"simRMLRemove",_simRuckigRemove,                           "Deprecated. Use sim.ruckigRemove instead",false},
    {"simBuildMatrixQ",_simBuildMatrixQ,                        "Use the newer sim.buildMatrixQ notation",false},
    {"simGetQuaternionFromMatrix",_simGetQuaternionFromMatrix,  "Use the newer sim.getQuaternionFromMatrix notation",false},
    {"simFileDialog",_simFileDialog,                            "Deprecated. Use simUI.fielDialog instead",false},
    {"simMsgBox",_simMsgBox,                                    "Deprecated. Use simUI.msgBox instead",false},
    {"simLoadModule",_simLoadModule,                            "Use the newer sim.loadModule notation",false},
    {"simUnloadModule",_simUnloadModule,                        "Use the newer sim.unloadModule notation",false},
    {"simCallScriptFunction",_simCallScriptFunction,            "Use the newer sim.callScriptFunction notation",false},
    {"simGetExtensionString",_simGetExtensionString,            "Use the newer sim.getExtensionString notation",false},
    {"simComputeMassAndInertia",_simComputeMassAndInertia,      "Use the newer sim.computeMassAndInertia notation",false},
    {"simSetScriptVariable",_simSetScriptVariable,              "Deprecated. Use sim.executeScriptString instead",false},
    {"simGetEngineFloatParameter",_simGetEngineFloatParam,      "Deprecated. Use sim.getEngineFloatParam instead",false},
    {"simGetEngineInt32Parameter",_simGetEngineInt32Param,      "Deprecated. Use sim.getEngineInt32Param instead",false},
    {"simGetEngineBoolParameter",_simGetEngineBoolParam,        "Deprecated. Use sim.getEngineBoolParam instead",false},
    {"simSetEngineFloatParameter",_simSetEngineFloatParam,      "Deprecated. Use sim.setEngineFloatParam instead",false},
    {"simSetEngineInt32Parameter",_simSetEngineInt32Param,      "Deprecated. Use sim.setEngineInt32Param instead",false},
    {"simSetEngineBoolParameter",_simSetEngineBoolParam,        "Deprecated. Use sim.setEngineBoolParam instead",false},
    {"simCreateOctree",_simCreateOctree,                        "Use the newer sim.createOctree notation",false},
    {"simCreatePointCloud",_simCreatePointCloud,                "Use the newer sim.createPointCloud notation",false},
    {"simSetPointCloudOptions",_simSetPointCloudOptions,        "Use the newer sim.setPointCloudOptions notation",false},
    {"simGetPointCloudOptions",_simGetPointCloudOptions,        "Use the newer sim.getPointCloudOptions notation",false},
    {"simInsertVoxelsIntoOctree",_simInsertVoxelsIntoOctree,    "Use the newer sim.insertVoxelsIntoOctree notation",false},
    {"simRemoveVoxelsFromOctree",_simRemoveVoxelsFromOctree,    "Use the newer sim.removeVoxelsFromOctree notation",false},
    {"simInsertPointsIntoPointCloud",_simInsertPointsIntoPointCloud,"Use the newer sim.insertPointsIntoPointCloud notation",false},
    {"simRemovePointsFromPointCloud",_simRemovePointsFromPointCloud,"Use the newer sim.removePointsFromPointCloud notation",false},
    {"simIntersectPointsWithPointCloud",_simIntersectPointsWithPointCloud,"Use the newer sim.intersectPointsWithPointCloud notation",false},
    {"simGetOctreeVoxels",_simGetOctreeVoxels,                  "Use the newer sim.getOctreeVoxels notation",false},
    {"simGetPointCloudPoints",_simGetPointCloudPoints,          "Use the newer sim.getPointCloudPoints notation",false},
    {"simInsertObjectIntoOctree",_simInsertObjectIntoOctree,    "Use the newer sim.insertObjectIntoOctree notation",false},
    {"simSubtractObjectFromOctree",_simSubtractObjectFromOctree,    "Use the newer sim.subtractObjectFromOctree notation",false},
    {"simInsertObjectIntoPointCloud",_simInsertObjectIntoPointCloud,"Use the newer sim.insertObjectIntoPointCloud notation",false},
    {"simSubtractObjectFromPointCloud",_simSubtractObjectFromPointCloud,    "Use the newer sim.subtractObjectFromPointCloud notation",false},
    {"simCheckOctreePointOccupancy",_simCheckOctreePointOccupancy,"Use the newer sim.checkOctreePointOccupancy notation",false},
    {"simOpenTextEditor",_simOpenTextEditor,                    "Deprecated. Use sim.textEditorOpen instead",false},
    {"simSetVisionSensorFilter",_simSetVisionSensorFilter,      "Deprecated. Use vision callback functions instead",false},
    {"simGetVisionSensorFilter",_simGetVisionSensorFilter,      "Deprecated. Use vision callback functions instead",false},
    {"simGetScriptSimulationParameter",_simGetScriptSimulationParameter,"Deprecated. Use sim.getUserParameter instead",false},
    {"simSetScriptSimulationParameter",_simSetScriptSimulationParameter,"Deprecated. Use sim.setUserParameter instead",false},
    {"simHandleSimulationStart",_simHandleSimulationStart,      "Use the newer sim.handleSimulationStart notation",false},
    {"simHandleSensingStart",_simHandleSensingStart,            "Use the newer sim.handleSensingStart notation",false},
    {"simAuxFunc",_simAuxFunc,                                  "Use the newer sim.auxFunc notation",false},
    {"simSetReferencedHandles",_simSetReferencedHandles,        "Use the newer sim.setReferencedHandles notation",false},
    {"simGetReferencedHandles",_simGetReferencedHandles,        "Use the newer sim.getReferencedHandles notation",false},
    {"simGetGraphCurve",_simGetGraphCurve,                        "Use the newer sim.getGraphCurve notation",false},
    {"simTest",_simTest,                                        "Use the newer sim.test notation",false},
    {"simAddStatusbarMessage",_simAddStatusbarMessage,          "Deprecated. Use sim.addLog instead",false},
    // Following deprecated since 21/05/2017:
    {"simGetObjectSelectionSize",_simGetObjectSelectionSize,    "Deprecated. Use sim.getObjectSel instead",false},
    {"simGetObjectLastSelection",_simGetObjectLastSelection,    "Deprecated. Use sim.getObjectSel instead",false},
    {"simReleaseScriptRawBuffer",_simReleaseScriptRawBuffer,    "Deprecated",false},
    // Following deprecated since V3.3.0:
    {"simGetPathPlanningHandle",_simGetPathPlanningHandle,      "Deprecated. Use the OMPL-based API instead",false},
    {"simSearchPath",_simSearchPath,                            "Deprecated. Use the OMPL-based API instead",false},
    {"simInitializePathSearch",_simInitializePathSearch,        "Deprecated. Use the OMPL-based API instead",false},
    {"simPerformPathSearchStep",_simPerformPathSearchStep,      "Deprecated. Use the OMPL-based API instead",false},
    // Following deprecated since 09/02/2017:
    {"simLoadUI",_simLoadUI,                                    "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSaveUI",_simSaveUI,                                    "Deprecated. Use Qt-based custom UIs instead",false},
    {"simRemoveUI",_simRemoveUI,                                "Deprecated. Use Qt-based custom UIs instead",false},
    {"simCreateUI",_simCreateUI,                                "Deprecated. Use Qt-based custom UIs instead",false},
    {"simCreateUIButton",_simCreateUIButton,                    "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGetUIPosition",_simGetUIPosition,                      "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSetUIPosition",_simSetUIPosition,                      "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGetUIHandle",_simGetUIHandle,                          "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGetUIProperty",_simGetUIProperty,                      "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGetUIEventButton",_simGetUIEventButton,                "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSetUIProperty",_simSetUIProperty,                      "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGetUIButtonProperty",_simGetUIButtonProperty,          "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSetUIButtonProperty",_simSetUIButtonProperty,          "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGetUIButtonSize",_simGetUIButtonSize,                  "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSetUIButtonLabel",_simSetUIButtonLabel,                "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGetUIButtonLabel",_simGetUIButtonLabel,                "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSetUISlider",_simSetUISlider,                          "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGetUISlider",_simGetUISlider,                          "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSetUIButtonColor",_simSetUIButtonColor,                "Deprecated. Use Qt-based custom UIs instead",false},
    {"simCreateUIButtonArray",_simCreateUIButtonArray,          "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSetUIButtonArrayColor",_simSetUIButtonArrayColor,      "Deprecated. Use Qt-based custom UIs instead",false},
    {"simDeleteUIButtonArray",_simDeleteUIButtonArray,          "Deprecated. Use Qt-based custom UIs instead",false},
    // Following for backward compatibility (nov-dec 2011):
    {"simGet2DElementHandle",_simGetUIHandle,                       "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGet2DElementProperty",_simGetUIProperty,                   "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGet2DElementEventButton",_simGetUIEventButton,             "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSet2DElementProperty",_simSetUIProperty,                   "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGet2DElementButtonProperty",_simGetUIButtonProperty,       "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSet2DElementButtonProperty",_simSetUIButtonProperty,       "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGet2DElementButtonSize",_simGetUIButtonSize,               "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSet2DElementButtonLabel",_simSetUIButtonLabel,             "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGet2DElementButtonLabel",_simGetUIButtonLabel,             "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSet2DElementSlider",_simSetUISlider,                       "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGet2DElementSlider",_simGetUISlider,                       "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSet2DElementButtonColor",_simSetUIButtonColor,             "Deprecated. Use Qt-based custom UIs instead",false},
    {"simCreate2DElementButtonArray",_simCreateUIButtonArray,       "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSet2DElementButtonArrayColor",_simSetUIButtonArrayColor,   "Deprecated. Use Qt-based custom UIs instead",false},
    {"simDelete2DElementButtonArray",_simDeleteUIButtonArray,       "Deprecated. Use Qt-based custom UIs instead",false},
    {"simRemove2DElement",_simRemoveUI,                             "Deprecated. Use Qt-based custom UIs instead",false},
    {"simCreate2DElement",_simCreateUI,                             "Deprecated. Use Qt-based custom UIs instead",false},
    {"simCreate2DElementButton",_simCreateUIButton,                 "Deprecated. Use Qt-based custom UIs instead",false},
    {"simGet2DElementPosition",_simGetUIPosition,                   "Deprecated. Use Qt-based custom UIs instead",false},
    {"simSet2DElementPosition",_simSetUIPosition,                   "Deprecated. Use Qt-based custom UIs instead",false},
    // Following deprecated since 26/12/2016:
    {"simAddSceneCustomData",_simAddSceneCustomData,                "Deprecated. Use sim.writeCustomDataBlock instead",false},
    {"simGetSceneCustomData",_simGetSceneCustomData,                "Deprecated. Use sim.readCustomDataBlock instead",false},
    {"simAddObjectCustomData",_simAddObjectCustomData,              "Deprecated. Use sim.writeCustomDataBlock instead",false},
    {"simGetObjectCustomData",_simGetObjectCustomData,              "Deprecated. Use sim.readCustomDataBlock instead",false},
    // Following deprecated since 29/10/2016:
    {"simGetMaterialId",_simGetMaterialId,                          "Deprecated. Use sim.setShapeMaterial instead",false},
    {"simGetShapeMaterial",_simGetShapeMaterial,                    "Deprecated. Use sim.setShapeMaterial instead",false},
    {"simHandleVarious",_simHandleVarious,                          "Deprecated. Use sim.handleSimulationStart and sim.handleSensingStart instead",false},
    // Following deprecated since 13/9/2016:
    {"simPackInts",_simPackInt32Table,                              "Deprecated. Use sim.packInt32Table instead",false},
    {"simPackUInts",_simPackUInt32Table,                            "Deprecated. Use sim.packUInt32Table instead",false},
    {"simPackFloats",_simPackFloatTable,                            "Deprecated. Use sim.packFloatTable instead",false},
    {"simPackDoubles",_simPackDoubleTable,                          "Deprecated. Use sim.packDoubleTable instead",false},
    {"simPackBytes",_simPackUInt8Table,                             "Deprecated. Use sim.packUInt8Table instead",false},
    {"simPackWords",_simPackUInt16Table,                            "Deprecated. Use sim.packUInt16Table instead",false},
    {"simUnpackInts",_simUnpackInt32Table,                          "Deprecated. Use sim.unpackInt32Table instead",false},
    {"simUnpackUInts",_simUnpackUInt32Table,                        "Deprecated. Use sim.unpackUInt32Table instead",false},
    {"simUnpackFloats",_simUnpackFloatTable,                        "Deprecated. Use sim.unpackFloatTable instead",false},
    {"simUnpackDoubles",_simUnpackDoubleTable,                      "Deprecated. Use sim.unpackDoubleTable instead",false},
    {"simUnpackBytes",_simUnpackUInt8Table,                         "Deprecated. Use sim.unpackUInt8Table instead",false},
    {"simUnpackWords",_simUnpackUInt16Table,                        "Deprecated. Use sim.unpackUInt16Table instead",false},
    // Following deprecated:
    {"simGetInvertedMatrix",_simGetInvertedMatrix,                  "Deprecated. Use sim.invertMatrix instead",false}, // 10/05/2016
    {"simEnableWorkThreads",_simEnableWorkThreads,                  "Deprecated. Has no effect",false},
    {"simWaitForWorkThreads",_simWaitForWorkThreads,                "Deprecated. Has no effect",false},
    {"simFindIkPath",_simFindIkPath,                                "Deprecated. Use sim.generateIkPath instead",false},
    {"simHandleChildScript",_simHandleChildScript,                  "Deprecated. Use sim.handleChildScripts instead",false},
    {"simHandleSensingChildScripts",_simHandleSensingChildScripts,  "Deprecated. Use sim.handleChildScripts instead",false},
    {"simDelegateChildScriptExecution",_simDelegateChildScriptExecution,"Deprecated. Has no effect",false},
    {"simResetTracing",_simResetTracing,                            "Deprecated. Has no effect",false},
    {"simHandleTracing",_simHandleTracing,                          "Deprecated. Has no effect",false},
    {"simCopyPasteSelectedObjects",_simCopyPasteSelectedObjects,    "Deprecated. Use sim.copyPasteObjects instead",false},
    {"simGetShapeVertex",_simGetShapeVertex,                        "Deprecated. Use sim.getShapeMesh instead",false},
    {"simGetShapeTriangle",_simGetShapeTriangle,                    "Deprecated. Use sim.getShapeMesh instead",false},
    {"simGetInstanceIndex",_simGetInstanceIndex,                    "Deprecated. Returns 0",false},
    {"simGetVisibleInstanceIndex",_simGetVisibleInstanceIndex,      "Deprecated. Returns 0",false},
    {"simGetSystemTimeInMilliseconds",_simGetSystemTimeInMilliseconds,"Deprecated. Use sim.getSystemTime instead",false},
    {"simLockInterface",_simLockInterface,                          "Deprecated. Has no effect",false},
    {"simJointGetForce",_simJointGetForce,                          "Deprecated. Use sim.getJointForce instead",false},
    {"simScaleSelectedObjects",_simScaleSelectedObjects,            "Deprecated. Use sim.scaleObjects instead",false},
    {"simDeleteSelectedObjects",_simDeleteSelectedObjects,          "Deprecated. Use sim.removeObject instead",false},
    {"simResetPath",_simResetPath,                                  "Deprecated",false},
    {"simResetJoint",_simResetJoint,                                "Deprecated",false},
    {"simGetMpConfigForTipPose",_simGetMpConfigForTipPose,          "Deprecated. Use sim.getConfigForTipPose instead",false},
    // Following for backward compatibility (June 2020):
    {"simGetNameSuffix",_simGetNameSuffix,                      "Deprecated.",false},
    {"simSetNameSuffix",_simSetNameSuffix,                      "Deprecated.",false},
    // Following for backward compatibility (Dec 2015):
    {"simSetBooleanParameter",_simSetBoolParam,                 "Deprecated. Use sim.setBoolParam instead",false},
    {"simGetBooleanParameter",_simGetBoolParam,                 "Deprecated. Use sim.getBoolParam instead",false},
    {"simSetIntegerParameter",_simSetInt32Param,                "Deprecated. Use sim.setInt32Param instead",false},
    {"simGetIntegerParameter",_simGetInt32Param,                "Deprecated. Use sim.getInt32Param instead",false},
    {"simSetFloatingParameter",_simSetFloatParam,               "Deprecated. Use sim.setFloatParam instead",false},
    {"simGetFloatingParameter",_simGetFloatParam,               "Deprecated. Use sim.getFloatParam instead",false},
    {"simGetObjectIntParameter",_simGetObjectInt32Parameter,    "Deprecated. Use sim.getObjectInt32Param instead",false},
    {"simSetObjectIntParameter",_simSetObjectInt32Param,        "Deprecated. Use sim.setObjectInt32Param instead",false},
    // Following for backward compatibility:
    {"simHandleRenderingSensor",_simHandleVisionSensor,             "Deprecated. Use sim.handleVisionSensor instead",false},
    {"simReadRenderingSensor",_simReadVisionSensor,                 "Deprecated. Use sim.readVisionSensor instead",false},
    {"simResetRenderingSensor",_simResetVisionSensor,               "Deprecated. Use sim.resetVisionSensor instead",false},
    {"simGetRenderingSensorResolution",_simGetVisionSensorResolution,"Deprecated. Use sim.getVisionSensorResolution instead",false},
    {"simGetRenderingSensorImage",_simGetVisionSensorImage,         "Deprecated. Use sim.getVisionSensorImage instead",false},
    {"simSetRenderingSensorImage",_simSetVisionSensorImage,         "Deprecated. Use sim.setVisionSensorImage instead",false},
    {"simGetRenderingSensorDepthBuffer",_simGetVisionSensorDepthBuffer,"Deprecated. Use sim.getVisionSensorDepthBuffer instead",false},
    {"simCheckRenderingSensor",_simCheckVisionSensor,               "Deprecated. Use sim.checkVisionSensor instead",false},
    {"simCheckRenderingSensorEx",_simCheckVisionSensorEx,           "Deprecated. Use sim.checkVisionSensorEx instead",false},

    {"simCheckIkGroup",_simCheckIkGroup,                        "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simCreateIkGroup",_simCreateIkGroup,                      "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simRemoveIkGroup",_simRemoveIkGroup,                      "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simCreateIkElement",_simCreateIkElement,                  "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simExportIk",_simExportIk,                                "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simComputeJacobian",_simComputeJacobian,                  "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simGetConfigForTipPose",_simGetConfigForTipPose,          "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simGenerateIkPath",_simGenerateIkPath,                    "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simGetIkGroupHandle",_simGetIkGroupHandle,                "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simGetIkGroupMatrix",_simGetIkGroupMatrix,                "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simHandleIkGroup",_simHandleIkGroup,                      "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simSetIkGroupProperties",_simSetIkGroupProperties,        "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simSetIkElementProperties",_simSetIkElementProperties,    "Deprecated. Use the corresponding API function from the kinematics plugin instead",false},
    {"simTubeRead",_simTubeRead,                                "Deprecated. Use signals or custom data blocks instead",false},
    {"simTubeOpen",_simTubeOpen,                                "Deprecated. Use signals or custom data blocks instead",false},
    {"simTubeClose",_simTubeClose,                              "Deprecated. Use signals or custom data blocks instead",false},
    {"simTubeWrite",_simTubeWrite,                              "Deprecated. Use signals or custom data blocks instead",false},
    {"simTubeStatus",_simTubeStatus,                            "Deprecated. Use signals or custom data blocks instead",false},
    {"simSendData",_simSendData,                                "Deprecated. Use signals or custom data blocks instead",false},
    {"simReceiveData",_simReceiveData,                          "Deprecated. Use signals or custom data blocks instead",false},
    {"simGetPathPosition",_simGetPathPosition,                  "Deprecated",false},
    {"simSetPathPosition",_simSetPathPosition,                  "Deprecated",false},
    {"simGetPathLength",_simGetPathLength,                      "Deprecated",false},
    {"simGetDataOnPath",_simGetDataOnPath,                      "Deprecated",false},
    {"simGetPositionOnPath",_simGetPositionOnPath,              "Deprecated",false},
    {"simGetOrientationOnPath",_simGetOrientationOnPath,        "Deprecated",false},
    {"simGetClosestPositionOnPath",_simGetClosestPositionOnPath,"Deprecated",false},
    {"simCreatePath",_sim_CreatePath,                           "Deprecated",false},
    {"simInsertPathCtrlPoints",_simInsertPathCtrlPoints,        "Deprecated",false},
    {"simCutPathCtrlPoints",_simCutPathCtrlPoints,              "Deprecated",false},
    {"simGetScriptExecutionCount",_simGetScriptExecutionCount,  "Deprecated",false},
    {"simIsScriptExecutionThreaded",_simIsScriptExecutionThreaded,"Deprecated",false},
    {"simIsScriptRunningInThread",_simIsScriptRunningInThread,  "Deprecated",false},
    {"simSetThreadResumeLocation",_simSetThreadResumeLocation,  "Deprecated",false},
    {"simResumeThreads",_simResumeThreads,                      "Deprecated",false},
    {"simLaunchThreadedChildScripts",_simLaunchThreadedChildScripts,"Deprecated",false},
    {"simGetCollectionHandle",_simGetCollectionHandle,          "Deprecated. Use sim.createCollection instead",false},
    {"simRemoveCollection",_simRemoveCollection,                "Deprecated. Use sim.destroyCollection instead",false},
    {"simEmptyCollection",_simEmptyCollection,                  "Deprecated. Use sim.destroyCollection instead",false},
    {"simGetCollectionName",_simGetCollectionName,              "Deprecated",false},
    {"simSetCollectionName",_simSetCollectionName,              "Deprecated",false},
    {"simCreateCollection",_sim_CreateCollection,               "Deprecated. Use sim.createCollection instead",false},
    {"simAddObjectToCollection",_simAddObjectToCollection,      "Deprecated. Use sim.addItemToCollection instead",false},
    {"simHandleDistance",_simHandleDistance,                    "Deprecated. Use sim.checkDistance instead",false},
    {"simReadDistance",_simReadDistance,                        "Deprecated. Use sim.checkDistance instead",false},
    {"simHandleCollision",_simHandleCollision,                  "Deprecated. Use sim.checkCollision instead",false},
    {"simReadCollision",_simReadCollision,                      "Deprecated. Use sim.checkCollision instead",false},
    {"simResetCollision",_simResetCollision,                    "Deprecated",false},
    {"simResetDistance",_simResetDistance,                      "Deprecated",false},
    {"simGetCollisionHandle",_simGetCollisionHandle,            "Deprecated. Use sim.checkCollision instead",false},
    {"simGetDistanceHandle",_simGetDistanceHandle,              "Deprecated. Use sim.checkDistance instead",false},
    {"simAddBanner",_simAddBanner,                              "Deprecated",false},
    {"simRemoveBanner",_simRemoveBanner,                        "Deprecated",false},
    {"simAddGhost",_simAddGhost,                                "Deprecated",false},
    {"simModifyGhost",_simModifyGhost,                          "Deprecated",false},
    {"simAddPointCloud",_simAddPointCloud,                      "Deprecated. Use point cloud objects instead",false},
    {"simModifyPointCloud",_simModifyPointCloud,                "Deprecated. Use point cloud objects instead",false},
    {"simSetGraphUserData",_simSetGraphUserData,                "Deprecated. Use sim.setGraphStreamValue instead",false},
    {"simCopyMatrix",_simCopyMatrix,                            "Deprecated. Use sim.copyTable instead",false},
    {"",nullptr,"",false}
};

const SLuaVariables simLuaVariablesOldApi[]=
{ // Following for backward compatibility (see newer equivalent commands ('simLuaVariables'))
    {"sim.mainscriptcall_initialization",sim_syscb_init,false},
    {"sim.mainscriptcall_cleanup",sim_syscb_cleanup,false},
    {"sim.mainscriptcall_regular",sim_syscb_actuation,false},
    {"sim.syscb_regular",sim_syscb_actuation,false},
    {"sim.childscriptcall_initialization",sim_syscb_init,false},
    {"sim.childscriptcall_cleanup",sim_syscb_cleanup,false},
    {"sim.childscriptcall_actuation",sim_syscb_actuation,false},
    {"sim.childscriptcall_sensing",sim_syscb_sensing,false},
    {"sim.addonscriptcall_initialization",sim_syscb_init,false},
    {"sim.addonscriptcall_suspend",sim_syscb_aos_suspend,false},
    {"sim.addonscriptcall_restarting",sim_syscb_aos_resume,false},
    {"sim.addonscriptcall_cleanup",sim_syscb_cleanup,false},
    {"sim.customizationscriptcall_initialization",sim_syscb_init,false},
    {"sim.customizationscriptcall_cleanup",sim_syscb_cleanup,false},
    {"sim.customizationscriptcall_nonsimulation",sim_syscb_nonsimulation,false},
    {"sim.customizationscriptcall_lastbeforesimulation",sim_syscb_beforesimulation,false},
    {"sim.customizationscriptcall_firstaftersimulation",sim_syscb_aftersimulation,false},
    {"sim.customizationscriptcall_simulationactuation",sim_syscb_actuation,false},
    {"sim.customizationscriptcall_simulationsensing",sim_syscb_sensing,false},
    {"sim.customizationscriptcall_simulationpause",sim_syscb_suspended,false},
    {"sim.customizationscriptcall_simulationpausefirst",sim_syscb_suspend,false},
    {"sim.customizationscriptcall_simulationpauselast",sim_syscb_resume,false},
    {"sim.customizationscriptcall_lastbeforeinstanceswitch",sim_syscb_beforeinstanceswitch,false},
    {"sim.customizationscriptcall_firstafterinstanceswitch",sim_syscb_afterinstanceswitch,false},
    {"sim.customizationscriptcall_beforecopy",sim_syscb_beforecopy,false},
    {"sim.customizationscriptcall_aftercopy",sim_syscb_aftercopy,false},
    {"sim.boolparam_show_w_emitters",sim_boolparam_show_w_emitters,false},
    {"sim.boolparam_show_w_receivers",sim_boolparam_show_w_receivers,false},

    {"sim_object_shape_type",sim_object_shape_type,false},
    {"sim_object_joint_type",sim_object_joint_type,false},
    {"sim_object_graph_type",sim_object_graph_type,false},
    {"sim_object_camera_type",sim_object_camera_type,false},
    {"sim_object_dummy_type",sim_object_dummy_type,false},
    {"sim_object_proximitysensor_type",sim_object_proximitysensor_type,false},
    {"sim_object_path_type",sim_object_path_type,false},
    {"sim_object_renderingsensor_type",sim_object_visionsensor_type,false},
    {"sim_object_visionsensor_type",sim_object_visionsensor_type,false},
    {"sim_object_mill_type",sim_object_mill_type,false},
    {"sim_object_forcesensor_type",sim_object_forcesensor_type,false},
    {"sim_object_light_type",sim_object_light_type,false},
    {"sim_object_mirror_type",sim_object_mirror_type,false},
    {"sim_object_octree_type",sim_object_octree_type,false},
    {"sim_object_pointcloud_type",sim_object_pointcloud_type,false},
    {"sim_light_omnidirectional_subtype",sim_light_omnidirectional_subtype,false},
    {"sim_light_spot_subtype",sim_light_spot_subtype,false},
    {"sim_light_directional_subtype",sim_light_directional_subtype,false},
    {"sim_joint_revolute_subtype",sim_joint_revolute_subtype,false},
    {"sim_joint_prismatic_subtype",sim_joint_prismatic_subtype,false},
    {"sim_joint_spherical_subtype",sim_joint_spherical_subtype,false},
    {"sim_shape_simpleshape_subtype",sim_shape_simpleshape_subtype,false},
    {"sim_shape_multishape_subtype",sim_shape_multishape_subtype,false},
    {"sim_proximitysensor_pyramid_subtype",sim_proximitysensor_pyramid_subtype,false},
    {"sim_proximitysensor_cylinder_subtype",sim_proximitysensor_cylinder_subtype,false},
    {"sim_proximitysensor_disc_subtype",sim_proximitysensor_disc_subtype,false},
    {"sim_proximitysensor_cone_subtype",sim_proximitysensor_cone_subtype,false},
    {"sim_proximitysensor_ray_subtype",sim_proximitysensor_ray_subtype,false},
    {"sim_mill_pyramid_subtype",sim_mill_pyramid_subtype,false},
    {"sim_mill_cylinder_subtype",sim_mill_cylinder_subtype,false},
    {"sim_mill_disc_subtype",sim_mill_disc_subtype,false},
    {"sim_mill_cone_subtype",sim_mill_cone_subtype,false},
    {"sim_object_no_subtype",sim_object_no_subtype,false},
    {"sim_appobj_object_type",sim_appobj_object_type,false},
    {"sim_appobj_collision_type",sim_appobj_collision_type,false},
    {"sim_appobj_distance_type",sim_appobj_distance_type,false},
    {"sim_appobj_simulation_type",sim_appobj_simulation_type,false},
    {"sim_appobj_ik_type",sim_appobj_ik_type,false},
    {"sim_appobj_collection_type",sim_appobj_collection_type,false},
    {"sim_appobj_2delement_type",sim_appobj_ui_type,false},  // for backward compatibility
    {"sim_appobj_ui_type",sim_appobj_ui_type,false},
    {"sim_appobj_script_type",sim_appobj_script_type,false},
    {"sim_appobj_pathplanning_type",sim_appobj_pathplanning_type,false},
    {"sim_appobj_texture_type",sim_appobj_texture_type,false},
    {"sim_ik_pseudo_inverse_method",sim_ik_pseudo_inverse_method,false},
    {"sim_ik_damped_least_squares_method",sim_ik_damped_least_squares_method,false},
    {"sim_ik_jacobian_transpose_method",sim_ik_jacobian_transpose_method,false},
    {"sim_ik_x_constraint",sim_ik_x_constraint,false},
    {"sim_ik_y_constraint",sim_ik_y_constraint,false},
    {"sim_ik_z_constraint",sim_ik_z_constraint,false},
    {"sim_ik_alpha_beta_constraint",sim_ik_alpha_beta_constraint,false},
    {"sim_ik_gamma_constraint",sim_ik_gamma_constraint,false},
    {"sim_ikresult_not_performed",sim_ikresult_not_performed,false},
    {"sim_ikresult_success",sim_ikresult_success,false},
    {"sim_ikresult_fail",sim_ikresult_fail,false},
    {"sim_message_ui_button_state_change",sim_message_ui_button_state_change,false},
    {"sim_message_model_loaded",sim_message_model_loaded,false},
    {"sim_message_scene_loaded",sim_message_scene_loaded,false},
    {"sim_message_object_selection_changed",sim_message_object_selection_changed,false},
    {"sim_message_keypress",sim_message_keypress,false},
    {"sim_message_bannerclicked",sim_message_bannerclicked,false},
    {"sim_message_prox_sensor_select_down",sim_message_prox_sensor_select_down,false},
    {"sim_message_prox_sensor_select_up",sim_message_prox_sensor_select_up,false},
    {"sim_message_pick_select_down",sim_message_pick_select_down,false},
    {"sim_buttonproperty_button",sim_buttonproperty_button,false},
    {"sim_buttonproperty_label",sim_buttonproperty_label,false},
    {"sim_buttonproperty_editbox",sim_buttonproperty_editbox,false},
    {"sim_buttonproperty_slider",sim_buttonproperty_slider,false},
    {"sim_buttonproperty_staydown",sim_buttonproperty_staydown,false},
    {"sim_buttonproperty_enabled",sim_buttonproperty_enabled,false},
    {"sim_buttonproperty_borderless",sim_buttonproperty_borderless,false},
    {"sim_buttonproperty_horizontallycentered",sim_buttonproperty_horizontallycentered,false},
    {"sim_buttonproperty_ignoremouse",sim_buttonproperty_ignoremouse,false},
    {"sim_buttonproperty_isdown",sim_buttonproperty_isdown,false},
    {"sim_buttonproperty_transparent",sim_buttonproperty_transparent,false},
    {"sim_buttonproperty_nobackgroundcolor",sim_buttonproperty_nobackgroundcolor,false},
    {"sim_buttonproperty_rollupaction",sim_buttonproperty_rollupaction,false},
    {"sim_buttonproperty_closeaction",sim_buttonproperty_closeaction,false},
    {"sim_buttonproperty_verticallycentered",sim_buttonproperty_verticallycentered,false},
    {"sim_buttonproperty_downupevent",sim_buttonproperty_downupevent,false},
    {"sim_objectproperty_collapsed",sim_objectproperty_collapsed,false},
    {"sim_objectproperty_selectable",sim_objectproperty_selectable,false},
    {"sim_objectproperty_selectmodelbaseinstead",sim_objectproperty_selectmodelbaseinstead,false},
    {"sim_objectproperty_dontshowasinsidemodel",sim_objectproperty_dontshowasinsidemodel,false},
    {"sim_objectproperty_canupdatedna",sim_objectproperty_canupdatedna,false},
    {"sim_objectproperty_selectinvisible",sim_objectproperty_selectinvisible,false},
    {"sim_objectproperty_depthinvisible",sim_objectproperty_depthinvisible,false},
    {"sim_objectproperty_cannotdelete",sim_objectproperty_cannotdelete,false},
    {"sim_simulation_stopped",sim_simulation_stopped,false},
    {"sim_simulation_paused",sim_simulation_paused,false},
    {"sim_simulation_advancing",sim_simulation_advancing,false},
    {"sim_simulation_advancing_firstafterstop",sim_simulation_advancing_firstafterstop,false},
    {"sim_simulation_advancing_running",sim_simulation_advancing_running,false},
    {"sim_simulation_advancing_lastbeforepause",sim_simulation_advancing_lastbeforepause,false},
    {"sim_simulation_advancing_firstafterpause",sim_simulation_advancing_firstafterpause,false},
    {"sim_simulation_advancing_abouttostop",sim_simulation_advancing_abouttostop,false},
    {"sim_simulation_advancing_lastbeforestop",sim_simulation_advancing_lastbeforestop,false},
    {"sim_texturemap_plane",sim_texturemap_plane,false},
    {"sim_texturemap_cylinder",sim_texturemap_cylinder,false},
    {"sim_texturemap_sphere",sim_texturemap_sphere,false},
    {"sim_texturemap_cube",sim_texturemap_cube,false},
    {"sim_scripttype_mainscript",sim_scripttype_mainscript,false},
    {"sim_scripttype_childscript",sim_scripttype_childscript,false},
    {"sim_scripttype_addonscript",sim_scripttype_addonscript,false},
    {"sim_scripttype_addonfunction",sim_scripttype_addonfunction,false},
    {"sim_scripttype_customizationscript",sim_scripttype_customizationscript,false},
    {"sim_scripttype_threaded",sim_scripttype_threaded_old,false},
    {"sim_mainscriptcall_initialization",sim_syscb_init,false},
    {"sim_mainscriptcall_cleanup",sim_syscb_cleanup,false},
    {"sim_mainscriptcall_regular",sim_syscb_actuation,false},
    {"sim_childscriptcall_initialization",sim_syscb_init,false},
    {"sim_childscriptcall_cleanup",sim_syscb_cleanup,false},
    {"sim_childscriptcall_actuation",sim_syscb_actuation,false},
    {"sim_childscriptcall_sensing",sim_syscb_sensing,false},
    {"sim_childscriptcall_threaded",-1,false},
    {"sim_customizationscriptcall_initialization",sim_syscb_init,false},
    {"sim_customizationscriptcall_cleanup",sim_syscb_cleanup,false},
    {"sim_customizationscriptcall_nonsimulation",sim_syscb_nonsimulation,false},
    {"sim_customizationscriptcall_lastbeforesimulation",sim_syscb_beforesimulation,false},
    {"sim_customizationscriptcall_firstaftersimulation",sim_syscb_aftersimulation,false},
    {"sim_customizationscriptcall_simulationactuation",sim_syscb_actuation,false},
    {"sim_customizationscriptcall_simulationsensing",sim_syscb_sensing,false},
    {"sim_customizationscriptcall_simulationpause",sim_syscb_suspended,false},
    {"sim_customizationscriptcall_simulationpausefirst",sim_syscb_suspend,false},
    {"sim_customizationscriptcall_simulationpauselast",sim_syscb_resume,false},
    {"sim_customizationscriptcall_lastbeforeinstanceswitch",sim_syscb_beforeinstanceswitch,false},
    {"sim_customizationscriptcall_firstafterinstanceswitch",sim_syscb_afterinstanceswitch,false},
    {"sim_addonscriptcall_initialization",sim_syscb_init,false},
    {"sim_addonscriptcall_suspend",sim_syscb_aos_suspend,false},
    {"sim_addonscriptcall_restarting",sim_syscb_aos_resume,false},
    {"sim_addonscriptcall_cleanup",sim_syscb_cleanup,false},
    {"sim_customizationscriptattribute_activeduringsimulation",sim_customizationscriptattribute_activeduringsimulation,false},
    {"sim_scriptattribute_executionorder",sim_scriptattribute_executionorder,false},
    {"sim_scriptattribute_executioncount",sim_scriptattribute_executioncount,false},
    {"sim_childscriptattribute_automaticcascadingcalls",sim_childscriptattribute_automaticcascadingcalls,false},
    {"sim_childscriptattribute_enabled",sim_childscriptattribute_enabled,false},
    {"sim_scriptattribute_enabled",sim_scriptattribute_enabled,false},
    {"sim_customizationscriptattribute_cleanupbeforesave",sim_customizationscriptattribute_cleanupbeforesave,false},
    {"sim_scriptexecorder_first",sim_scriptexecorder_first,false},
    {"sim_scriptexecorder_normal",sim_scriptexecorder_normal,false},
    {"sim_scriptexecorder_last",sim_scriptexecorder_last,false},
    {"sim_scriptthreadresume_allnotyetresumed",sim_scriptthreadresume_allnotyetresumed,false},
    {"sim_scriptthreadresume_default",sim_scriptthreadresume_default,false},
    {"sim_scriptthreadresume_actuation_first",sim_scriptthreadresume_actuation_first,false},
    {"sim_scriptthreadresume_actuation_last",sim_scriptthreadresume_actuation_last,false},
    {"sim_scriptthreadresume_sensing_first",sim_scriptthreadresume_sensing_first,false},
    {"sim_scriptthreadresume_sensing_last",sim_scriptthreadresume_sensing_last,false},
    {"sim_scriptthreadresume_custom",sim_scriptthreadresume_custom,false},
    {"sim_callbackid_rossubscriber",sim_callbackid_rossubscriber,false},
    {"sim_callbackid_dynstep",sim_callbackid_dynstep,false},
    {"sim_callbackid_userdefined",sim_callbackid_userdefined,false},
    {"sim_script_no_error",sim_script_no_error,false},
    {"sim_script_main_script_nonexistent",sim_script_main_script_nonexistent,false},
    {"sim_script_main_not_called",sim_script_main_script_not_called,false},
    {"sim_script_reentrance_error",sim_script_reentrance_error,false},
    {"sim_script_lua_error",sim_script_lua_error,false},
    {"sim_script_call_error",sim_script_call_error,false},
    {"sim_api_error_report",sim_api_error_report,false},
    {"sim_api_error_output",sim_api_error_output,false},
    {"sim_api_warning_output",sim_api_warning_output,false},
    {"sim_handle_all",sim_handle_all,false},
    {"sim_handle_all_except_explicit",sim_handle_all_except_explicit,false},
    {"sim_handle_self",sim_handle_self,false},
    {"sim_handle_main_script",sim_handle_main_script,false},
    {"sim_handle_tree",sim_handle_tree,false},
    {"sim_handle_chain",sim_handle_chain,false},
    {"sim_handle_single",sim_handle_single,false},
    {"sim_handle_default",sim_handle_default,false},
    {"sim_handle_all_except_self",sim_handle_all_except_self,false},
    {"sim_handle_parent",sim_handle_parent,false},
    {"sim_handle_scene",sim_handle_scene,false},
    {"sim_handle_app",sim_handle_app,false},
    {"sim_handleflag_assembly",sim_handleflag_assembly,false},
    {"sim_handleflag_camera",sim_handleflag_camera,false},
    {"sim_handleflag_togglevisibility",sim_handleflag_togglevisibility,false},
    {"sim_handleflag_extended",sim_handleflag_extended,false},
    {"sim_handleflag_greyscale",sim_handleflag_greyscale,false},
    {"sim_handleflag_codedstring",sim_handleflag_codedstring,false},
    {"sim_handleflag_model",sim_handleflag_model,false},
    {"sim_handleflag_rawvalue",sim_handleflag_rawvalue,false},
    {"sim_objectspecialproperty_collidable",sim_objectspecialproperty_collidable,false},
    {"sim_objectspecialproperty_measurable",sim_objectspecialproperty_measurable,false},
    {"sim_objectspecialproperty_detectable_ultrasonic",sim_objectspecialproperty_detectable_ultrasonic,false},
    {"sim_objectspecialproperty_detectable_infrared",sim_objectspecialproperty_detectable_infrared,false},
    {"sim_objectspecialproperty_detectable_laser",sim_objectspecialproperty_detectable_laser,false},
    {"sim_objectspecialproperty_detectable_inductive",sim_objectspecialproperty_detectable_inductive,false},
    {"sim_objectspecialproperty_detectable_capacitive",sim_objectspecialproperty_detectable_capacitive,false},
    {"sim_objectspecialproperty_renderable",sim_objectspecialproperty_renderable,false},
    {"sim_objectspecialproperty_detectable_all",sim_objectspecialproperty_detectable,false},
    {"sim_objectspecialproperty_pathplanning_ignored",sim_objectspecialproperty_pathplanning_ignored,false},
    {"sim_modelproperty_not_collidable",sim_modelproperty_not_collidable,false},
    {"sim_modelproperty_not_measurable",sim_modelproperty_not_measurable,false},
    {"sim_modelproperty_not_renderable",sim_modelproperty_not_renderable,false},
    {"sim_modelproperty_not_detectable",sim_modelproperty_not_detectable,false},
    {"sim_modelproperty_not_dynamic",sim_modelproperty_not_dynamic,false},
    {"sim_modelproperty_not_respondable",sim_modelproperty_not_respondable,false},
    {"sim_modelproperty_not_reset",sim_modelproperty_not_reset,false},
    {"sim_modelproperty_not_visible",sim_modelproperty_not_visible,false},
    {"sim_modelproperty_scripts_inactive",sim_modelproperty_scripts_inactive,false},
    {"sim_modelproperty_not_showasinsidemodel",sim_modelproperty_not_showasinsidemodel,false},
    {"sim_modelproperty_not_model",sim_modelproperty_not_model,false},
    {"sim_dlgstyle_message",sim_dlgstyle_message,false},
    {"sim_dlgstyle_input",sim_dlgstyle_input,false},
    {"sim_dlgstyle_ok",sim_dlgstyle_ok,false},
    {"sim_dlgstyle_ok_cancel",sim_dlgstyle_ok_cancel,false},
    {"sim_dlgstyle_yes_no",sim_dlgstyle_yes_no,false},
    {"sim_dlgstyle_dont_center",sim_dlgstyle_dont_center,false},
    {"sim_dlgret_still_open",sim_dlgret_still_open,false},
    {"sim_dlgret_ok",sim_dlgret_ok,false},
    {"sim_dlgret_cancel",sim_dlgret_cancel,false},
    {"sim_dlgret_yes",sim_dlgret_yes,false},
    {"sim_dlgret_no",sim_dlgret_no,false},
    {"sim_pathproperty_show_line",sim_pathproperty_show_line,false},
    {"sim_pathproperty_show_orientation",sim_pathproperty_show_orientation,false},
    {"sim_pathproperty_closed_path",sim_pathproperty_closed_path,false},
    {"sim_pathproperty_automatic_orientation",sim_pathproperty_automatic_orientation,false},
    {"sim_pathproperty_flat_path",sim_pathproperty_flat_path,false},
    {"sim_pathproperty_show_position",sim_pathproperty_show_position,false},
    {"sim_pathproperty_keep_x_up",sim_pathproperty_keep_x_up,false},
    {"sim_distcalcmethod_dl",sim_distcalcmethod_dl,false},
    {"sim_distcalcmethod_dac",sim_distcalcmethod_dac,false},
    {"sim_distcalcmethod_max_dl_dac",sim_distcalcmethod_max_dl_dac,false},
    {"sim_distcalcmethod_dl_and_dac",sim_distcalcmethod_dl_and_dac,false},
    {"sim_distcalcmethod_sqrt_dl2_and_dac2",sim_distcalcmethod_sqrt_dl2_and_dac2,false},
    {"sim_distcalcmethod_dl_if_nonzero",sim_distcalcmethod_dl_if_nonzero,false},
    {"sim_distcalcmethod_dac_if_nonzero",sim_distcalcmethod_dac_if_nonzero,false},
    {"sim_boolparam_hierarchy_visible",sim_boolparam_hierarchy_visible,false},
    {"sim_boolparam_console_visible",sim_boolparam_console_visible,false},
    {"sim_boolparam_collision_handling_enabled",sim_boolparam_collision_handling_enabled,false},
    {"sim_boolparam_distance_handling_enabled",sim_boolparam_distance_handling_enabled,false},
    {"sim_boolparam_ik_handling_enabled",sim_boolparam_ik_handling_enabled,false},
    {"sim_boolparam_gcs_handling_enabled",sim_boolparam_gcs_handling_enabled,false},
    {"sim_boolparam_dynamics_handling_enabled",sim_boolparam_dynamics_handling_enabled,false},
    {"sim_boolparam_proximity_sensor_handling_enabled",sim_boolparam_proximity_sensor_handling_enabled,false},
    {"sim_boolparam_vision_sensor_handling_enabled",sim_boolparam_vision_sensor_handling_enabled,false},
    {"sim_boolparam_rendering_sensor_handling_enabled",sim_boolparam_vision_sensor_handling_enabled,false},
    {"sim_boolparam_mill_handling_enabled",sim_boolparam_mill_handling_enabled,false},
    {"sim_boolparam_browser_visible",sim_boolparam_browser_visible,false},
    {"sim_boolparam_scene_and_model_load_messages",sim_boolparam_scene_and_model_load_messages,false},
    {"sim_boolparam_shape_textures_are_visible",sim_boolparam_shape_textures_are_visible,false},
    {"sim_boolparam_display_enabled",sim_boolparam_display_enabled,false},
    {"sim_boolparam_infotext_visible",sim_boolparam_infotext_visible,false},
    {"sim_boolparam_statustext_open",sim_boolparam_statustext_open,false},
    {"sim_boolparam_fog_enabled",sim_boolparam_fog_enabled,false},
    {"sim_boolparam_rml2_available",sim_boolparam_rml2_available,false},
    {"sim_boolparam_rml4_available",sim_boolparam_rml4_available,false},
    {"sim_boolparam_mirrors_enabled",sim_boolparam_mirrors_enabled,false},
    {"sim_boolparam_aux_clip_planes_enabled",sim_boolparam_aux_clip_planes_enabled,false},
    {"sim_boolparam_full_model_copy_from_api",sim_boolparam_reserved3,false},
    {"sim_boolparam_realtime_simulation",sim_boolparam_realtime_simulation,false},
    {"sim_boolparam_use_glfinish_cmd",sim_boolparam_use_glfinish_cmd,false},
    {"sim_boolparam_force_show_wireless_emission",sim_boolparam_force_show_wireless_emission,false},
    {"sim_boolparam_force_show_wireless_reception",sim_boolparam_force_show_wireless_reception,false},
    {"sim_boolparam_video_recording_triggered",sim_boolparam_video_recording_triggered,false},
    {"sim_boolparam_fullscreen",sim_boolparam_fullscreen,false},
    {"sim_boolparam_headless",sim_boolparam_headless,false},
    {"sim_boolparam_hierarchy_toolbarbutton_enabled",sim_boolparam_hierarchy_toolbarbutton_enabled,false},
    {"sim_boolparam_browser_toolbarbutton_enabled",sim_boolparam_browser_toolbarbutton_enabled,false},
    {"sim_boolparam_objectshift_toolbarbutton_enabled",sim_boolparam_objectshift_toolbarbutton_enabled,false},
    {"sim_boolparam_objectrotate_toolbarbutton_enabled",sim_boolparam_objectrotate_toolbarbutton_enabled,false},
    {"sim_boolparam_force_calcstruct_all_visible",sim_boolparam_force_calcstruct_all_visible,false},
    {"sim_boolparam_force_calcstruct_all",sim_boolparam_force_calcstruct_all,false},
    {"sim_boolparam_exit_request",sim_boolparam_exit_request,false},
    {"sim_boolparam_play_toolbarbutton_enabled",sim_boolparam_play_toolbarbutton_enabled,false},
    {"sim_boolparam_pause_toolbarbutton_enabled",sim_boolparam_pause_toolbarbutton_enabled,false},
    {"sim_boolparam_stop_toolbarbutton_enabled",sim_boolparam_stop_toolbarbutton_enabled,false},
    {"sim_boolparam_waiting_for_trigger",sim_boolparam_waiting_for_trigger,false},
    {"sim_boolparam_objproperties_toolbarbutton_enabled",sim_boolparam_objproperties_toolbarbutton_enabled,false},
    {"sim_boolparam_calcmodules_toolbarbutton_enabled",sim_boolparam_calcmodules_toolbarbutton_enabled,false},
    {"sim_boolparam_rosinterface_donotrunmainscript",sim_boolparam_rosinterface_donotrunmainscript,false},
    {"sim_intparam_error_report_mode",sim_intparam_error_report_mode,false},
    {"sim_intparam_program_version",sim_intparam_program_version,false},
    {"sim_intparam_compilation_version",sim_intparam_compilation_version,false},
    {"sim_intparam_current_page",sim_intparam_current_page,false},
    {"sim_intparam_flymode_camera_handle",sim_intparam_flymode_camera_handle,false},
    {"sim_intparam_dynamic_step_divider",sim_intparam_dynamic_step_divider,false},
    {"sim_intparam_dynamic_engine",sim_intparam_dynamic_engine,false},
    {"sim_intparam_server_port_start",sim_intparam_server_port_start,false},
    {"sim_intparam_server_port_range",sim_intparam_server_port_range,false},
    {"sim_intparam_server_port_next",sim_intparam_server_port_next,false},
    {"sim_intparam_visible_layers",sim_intparam_visible_layers,false},
    {"sim_intparam_infotext_style",sim_intparam_infotext_style,false},
    {"sim_intparam_settings",sim_intparam_settings,false},
    {"sim_intparam_qt_version",sim_intparam_qt_version,false},
    {"sim_intparam_platform",sim_intparam_platform,false},
    {"sim_intparam_scene_unique_id",sim_intparam_scene_unique_id,false},
    {"sim_intparam_edit_mode_type",sim_intparam_edit_mode_type,false},
    {"sim_intparam_work_thread_count",sim_intparam_work_thread_count,false}, // deprecated
    {"sim_intparam_mouse_x",sim_intparam_mouse_x,false},
    {"sim_intparam_mouse_y",sim_intparam_mouse_y,false},
    {"sim_intparam_core_count",sim_intparam_core_count,false},
    {"sim_intparam_work_thread_calc_time_ms",sim_intparam_work_thread_calc_time_ms,false}, // deprecated
    {"sim_intparam_idle_fps",sim_intparam_idle_fps,false},
    {"sim_intparam_prox_sensor_select_down",sim_intparam_prox_sensor_select_down,false},
    {"sim_intparam_prox_sensor_select_up",sim_intparam_prox_sensor_select_up,false},
    {"sim_intparam_stop_request_counter",sim_intparam_stop_request_counter,false},
    {"sim_intparam_program_revision",sim_intparam_program_revision,false},
    {"sim_intparam_mouse_buttons",sim_intparam_mouse_buttons,false},
    {"sim_intparam_dynamic_warning_disabled_mask",sim_intparam_dynamic_warning_disabled_mask,false},
    {"sim_intparam_simulation_warning_disabled_mask",sim_intparam_simulation_warning_disabled_mask,false},
    {"sim_intparam_scene_index",sim_intparam_scene_index,false},
    {"sim_intparam_motionplanning_seed",sim_intparam_motionplanning_seed,false},
    {"sim_intparam_speedmodifier",sim_intparam_speedmodifier,false},
    {"sim_intparam_dynamic_iteration_count",sim_intparam_dynamic_iteration_count,false},
    {"sim_floatparam_rand",sim_floatparam_rand,false},
    {"sim_floatparam_simulation_time_step",sim_floatparam_simulation_time_step,false},
    {"sim_floatparam_stereo_distance",sim_floatparam_stereo_distance,false},
    {"sim_floatparam_dynamic_step_size",sim_floatparam_dynamic_step_size,false},
    {"sim_floatparam_mouse_wheel_zoom_factor",sim_floatparam_mouse_wheel_zoom_factor,false},
    {"sim_arrayparam_gravity",sim_arrayparam_gravity,false},
    {"sim_arrayparam_fog",sim_arrayparam_fog,false},
    {"sim_arrayparam_fog_color",sim_arrayparam_fog_color,false},
    {"sim_arrayparam_background_color1",sim_arrayparam_background_color1,false},
    {"sim_arrayparam_background_color2",sim_arrayparam_background_color2,false},
    {"sim_arrayparam_ambient_light",sim_arrayparam_ambient_light,false},
    {"sim_arrayparam_random_euler",sim_arrayparam_random_euler,false},
    {"sim_stringparam_application_path",sim_stringparam_application_path,false},
    {"sim_stringparam_video_filename",sim_stringparam_video_filename,false},
    {"sim_stringparam_app_arg1",sim_stringparam_app_arg1,false},
    {"sim_stringparam_app_arg2",sim_stringparam_app_arg2,false},
    {"sim_stringparam_app_arg3",sim_stringparam_app_arg3,false},
    {"sim_stringparam_app_arg4",sim_stringparam_app_arg4,false},
    {"sim_stringparam_app_arg5",sim_stringparam_app_arg5,false},
    {"sim_stringparam_app_arg6",sim_stringparam_app_arg6,false},
    {"sim_stringparam_app_arg7",sim_stringparam_app_arg7,false},
    {"sim_stringparam_app_arg8",sim_stringparam_app_arg8,false},
    {"sim_stringparam_app_arg9",sim_stringparam_app_arg9,false},
    {"sim_stringparam_scene_path_and_name",sim_stringparam_scene_path_and_name,false},
    {"sim_stringparam_remoteapi_temp_file_dir",sim_stringparam_remoteapi_temp_file_dir,false},
    {"sim_stringparam_scene_path",sim_stringparam_scene_path,false},
    {"sim_stringparam_scene_name",sim_stringparam_scene_name,false},
    {"sim_displayattribute_renderpass",sim_displayattribute_renderpass,false},
    {"sim_displayattribute_depthpass",sim_displayattribute_depthpass,false},
    {"sim_displayattribute_pickpass",sim_displayattribute_pickpass,false},
    {"sim_displayattribute_selected",sim_displayattribute_selected,false},
    {"sim_displayattribute_mainselection",sim_displayattribute_mainselection,false},
    {"sim_displayattribute_forcewireframe",sim_displayattribute_forcewireframe,false},
    {"sim_displayattribute_forbidwireframe",sim_displayattribute_forbidwireframe,false},
    {"sim_displayattribute_forbidedges",sim_displayattribute_forbidedges,false},
    {"sim_displayattribute_originalcolors",sim_displayattribute_originalcolors,false},
    {"sim_displayattribute_ignorelayer",sim_displayattribute_ignorelayer,false},
    {"sim_displayattribute_forvisionsensor",sim_displayattribute_forvisionsensor,false},
    {"sim_displayattribute_colorcodedpickpass",sim_displayattribute_colorcodedpickpass,false},
    {"sim_displayattribute_colorcoded",sim_displayattribute_colorcoded,false},
    {"sim_displayattribute_trianglewireframe",sim_displayattribute_trianglewireframe,false},
    {"sim_displayattribute_thickEdges",sim_displayattribute_thickEdges,false},
    {"sim_displayattribute_dynamiccontentonly",sim_displayattribute_dynamiccontentonly,false},
    {"sim_displayattribute_mirror",sim_displayattribute_mirror,false},
    {"sim_displayattribute_useauxcomponent",sim_displayattribute_useauxcomponent,false},
    {"sim_displayattribute_ignorerenderableflag",sim_displayattribute_ignorerenderableflag,false},
    {"sim_displayattribute_noopenglcallbacks",sim_displayattribute_noopenglcallbacks,false},
    {"sim_displayattribute_noghosts",sim_displayattribute_noghosts,false},
    {"sim_displayattribute_nopointclouds",sim_displayattribute_nopointclouds,false},
    {"sim_displayattribute_nodrawingobjects",sim_displayattribute_nodrawingobjects,false},
    {"sim_displayattribute_noparticles",sim_displayattribute_noparticles,false},
    {"sim_displayattribute_colorcodedtriangles",sim_displayattribute_colorcodedtriangles,false},
    {"sim_navigation_passive",sim_navigation_passive,false},
    {"sim_navigation_camerashift",sim_navigation_camerashift,false},
    {"sim_navigation_camerarotate",sim_navigation_camerarotate,false},
    {"sim_navigation_camerazoom",sim_navigation_camerazoom,false},
    {"sim_navigation_cameratilt",sim_navigation_cameratilt,false},
    {"sim_navigation_cameraangle",sim_navigation_cameraangle,false},
    {"sim_navigation_objectshift",sim_navigation_objectshift,false},
    {"sim_navigation_objectrotate",sim_navigation_objectrotate,false},
    {"sim_navigation_createpathpoint",sim_navigation_createpathpoint,false},
    {"sim_navigation_clickselection",sim_navigation_clickselection,false},
    {"sim_navigation_ctrlselection",sim_navigation_ctrlselection,false},
    {"sim_navigation_shiftselection",sim_navigation_shiftselection,false},
    {"sim_navigation_camerazoomwheel",sim_navigation_camerazoomwheel,false},
    {"sim_navigation_camerarotaterightbutton",sim_navigation_camerarotaterightbutton,false},
    {"sim_navigation_camerarotatemiddlebutton",sim_navigation_camerarotatemiddlebutton,false},
    {"sim_drawing_points",sim_drawing_points,false},
    {"sim_drawing_lines",sim_drawing_lines,false},
    {"sim_drawing_triangles",sim_drawing_triangles,false},
    {"sim_drawing_trianglepoints",sim_drawing_trianglepoints,false},
    {"sim_drawing_quadpoints",sim_drawing_quadpoints,false},
    {"sim_drawing_discpoints",sim_drawing_discpoints,false},
    {"sim_drawing_cubepoints",sim_drawing_cubepoints,false},
    {"sim_drawing_spherepoints",sim_drawing_spherepoints,false},
    {"sim_drawing_itemcolors",sim_drawing_itemcolors,false},
    {"sim_drawing_vertexcolors",sim_drawing_vertexcolors,false},
    {"sim_drawing_itemsizes",sim_drawing_itemsizes,false},
    {"sim_drawing_backfaceculling",sim_drawing_backfaceculling,false},
    {"sim_drawing_wireframe",sim_drawing_wireframe,false},
    {"sim_drawing_painttag",sim_drawing_painttag,false},
    {"sim_drawing_followparentvisibility",sim_drawing_followparentvisibility,false},
    {"sim_drawing_cyclic",sim_drawing_cyclic,false},
    {"sim_drawing_50percenttransparency",sim_drawing_50percenttransparency,false},
    {"sim_drawing_25percenttransparency",sim_drawing_25percenttransparency,false},
    {"sim_drawing_12percenttransparency",sim_drawing_12percenttransparency,false},
    {"sim_drawing_emissioncolor",sim_drawing_emissioncolor,false},
    {"sim_drawing_facingcamera",sim_drawing_facingcamera,false},
    {"sim_drawing_overlay",sim_drawing_overlay,false},
    {"sim_drawing_itemtransparency",sim_drawing_itemtransparency,false},
    {"sim_drawing_persistent",sim_drawing_persistent,false},
    {"sim_drawing_auxchannelcolor1",sim_drawing_auxchannelcolor1,false},
    {"sim_drawing_auxchannelcolor2",sim_drawing_auxchannelcolor2,false},
    {"sim_particle_points1",sim_particle_points1,false},
    {"sim_particle_points2",sim_particle_points2,false},
    {"sim_particle_points4",sim_particle_points4,false},
    {"sim_particle_roughspheres",sim_particle_roughspheres,false},
    {"sim_particle_spheres",sim_particle_spheres,false},
    {"sim_particle_respondable1to4",sim_particle_respondable1to4,false},
    {"sim_particle_respondable5to8",sim_particle_respondable5to8,false},
    {"sim_particle_particlerespondable",sim_particle_particlerespondable,false},
    {"sim_particle_ignoresgravity",sim_particle_ignoresgravity,false},
    {"sim_particle_invisible",sim_particle_invisible,false},
    {"sim_particle_painttag",sim_particle_painttag,false},
    {"sim_particle_itemsizes",sim_particle_itemsizes,false},
    {"sim_particle_itemdensities",sim_particle_itemdensities,false},
    {"sim_particle_itemcolors",sim_particle_itemcolors,false},
    {"sim_particle_cyclic",sim_particle_cyclic,false},
    {"sim_particle_emissioncolor",sim_particle_emissioncolor,false},
    {"sim_particle_water",sim_particle_water,false},
    {"sim_jointmode_kinematic",sim_jointmode_kinematic,false},
    {"sim_jointmode_ik",sim_jointmode_ik_deprecated,false},
    {"sim_jointmode_ikdependent",sim_jointmode_reserved_previously_ikdependent,false},
    {"sim_jointmode_dependent",sim_jointmode_dependent,false},
    {"sim_jointmode_dynamic",sim_jointmode_dynamic,false},
    {"sim_filedlg_type_load",sim_filedlg_type_load,false},
    {"sim_filedlg_type_save",sim_filedlg_type_save,false},
    {"sim_filedlg_type_load_multiple",sim_filedlg_type_load_multiple,false},
    {"sim_filedlg_type_folder",sim_filedlg_type_folder,false},
    {"sim_msgbox_type_info",sim_msgbox_type_info,false},
    {"sim_msgbox_type_question",sim_msgbox_type_question,false},
    {"sim_msgbox_type_warning",sim_msgbox_type_warning,false},
    {"sim_msgbox_type_critical",sim_msgbox_type_critical,false},
    {"sim_msgbox_buttons_ok",sim_msgbox_buttons_ok,false},
    {"sim_msgbox_buttons_yesno",sim_msgbox_buttons_yesno,false},
    {"sim_msgbox_buttons_yesnocancel",sim_msgbox_buttons_yesnocancel,false},
    {"sim_msgbox_buttons_okcancel",sim_msgbox_buttons_okcancel,false},
    {"sim_msgbox_return_cancel",sim_msgbox_return_cancel,false},
    {"sim_msgbox_return_no",sim_msgbox_return_no,false},
    {"sim_msgbox_return_yes",sim_msgbox_return_yes,false},
    {"sim_msgbox_return_ok",sim_msgbox_return_ok,false},
    {"sim_msgbox_return_error",sim_msgbox_return_error,false},
    {"sim_physics_bullet",sim_physics_bullet,false},
    {"sim_physics_ode",sim_physics_ode,false},
    {"sim_physics_vortex",sim_physics_vortex,false},
    {"sim_physics_newton",sim_physics_newton,false},
    {"sim_pure_primitive_none",sim_primitiveshape_none,false},
    {"sim_pure_primitive_plane",sim_primitiveshape_plane,false},
    {"sim_pure_primitive_disc",sim_primitiveshape_disc,false},
    {"sim_pure_primitive_cuboid",sim_primitiveshape_cuboid,false},
    {"sim_pure_primitive_spheroid",sim_primitiveshape_spheroid,false},
    {"sim_pure_primitive_cylinder",sim_primitiveshape_cylinder,false},
    {"sim_pure_primitive_cone",sim_primitiveshape_cone,false},
    {"sim_pure_primitive_heightfield",sim_primitiveshape_heightfield,false},
    {"sim_dummy_linktype_dynamics_loop_closure",sim_dummylink_dynloopclosure,false},
    {"sim_dummy_linktype_dynamics_force_constraint",sim_dummy_linktype_dynamics_force_constraint,false},
    {"sim_dummy_linktype_gcs_loop_closure",sim_dummy_linktype_gcs_loop_closure,false},
    {"sim_dummy_linktype_gcs_tip",sim_dummy_linktype_gcs_tip,false},
    {"sim_dummy_linktype_gcs_target",sim_dummy_linktype_gcs_target,false},
    {"sim_dummy_linktype_ik_tip_target",sim_dummy_linktype_ik_tip_target,false},
    {"sim_colorcomponent_ambient",sim_colorcomponent_ambient,false},
    {"sim_colorcomponent_ambient_diffuse",sim_colorcomponent_ambient_diffuse,false},
    {"sim_colorcomponent_diffuse",sim_colorcomponent_diffuse,false},
    {"sim_colorcomponent_specular",sim_colorcomponent_specular,false},
    {"sim_colorcomponent_emission",sim_colorcomponent_emission,false},
    {"sim_colorcomponent_transparency",sim_colorcomponent_transparency,false},
    {"sim_colorcomponent_auxiliary",sim_colorcomponent_auxiliary,false},
    {"sim_volume_ray",sim_volume_ray,false},
    {"sim_volume_randomizedray",sim_volume_randomizedray,false},
    {"sim_volume_pyramid",sim_volume_pyramid,false},
    {"sim_volume_cylinder",sim_volume_cylinder,false},
    {"sim_volume_disc",sim_volume_disc,false},
    {"sim_volume_cone",sim_volume_cone,false},
    {"sim_objintparam_visibility_layer",sim_objintparam_visibility_layer,false},
    {"sim_objfloatparam_abs_x_velocity",sim_objfloatparam_abs_x_velocity,false},
    {"sim_objfloatparam_abs_y_velocity",sim_objfloatparam_abs_y_velocity,false},
    {"sim_objfloatparam_abs_z_velocity",sim_objfloatparam_abs_z_velocity,false},
    {"sim_objfloatparam_abs_rot_velocity",sim_objfloatparam_abs_rot_velocity,false},
    {"sim_objfloatparam_objbbox_min_x",sim_objfloatparam_objbbox_min_x,false},
    {"sim_objfloatparam_objbbox_min_y",sim_objfloatparam_objbbox_min_y,false},
    {"sim_objfloatparam_objbbox_min_z",sim_objfloatparam_objbbox_min_z,false},
    {"sim_objfloatparam_objbbox_max_x",sim_objfloatparam_objbbox_max_x,false},
    {"sim_objfloatparam_objbbox_max_y",sim_objfloatparam_objbbox_max_y,false},
    {"sim_objfloatparam_objbbox_max_z",sim_objfloatparam_objbbox_max_z,false},
    {"sim_objfloatparam_modelbbox_min_x",sim_objfloatparam_modelbbox_min_x,false},
    {"sim_objfloatparam_modelbbox_min_y",sim_objfloatparam_modelbbox_min_y,false},
    {"sim_objfloatparam_modelbbox_min_z",sim_objfloatparam_modelbbox_min_z,false},
    {"sim_objfloatparam_modelbbox_max_x",sim_objfloatparam_modelbbox_max_x,false},
    {"sim_objfloatparam_modelbbox_max_y",sim_objfloatparam_modelbbox_max_y,false},
    {"sim_objfloatparam_modelbbox_max_z",sim_objfloatparam_modelbbox_max_z,false},
    {"sim_objintparam_collection_self_collision_indicator",sim_objintparam_collection_self_collision_indicator,false},
    {"sim_objfloatparam_transparency_offset",sim_objfloatparam_transparency_offset,false},
    {"sim_objintparam_child_role",sim_objintparam_child_role,false},
    {"sim_objintparam_parent_role",sim_objintparam_parent_role,false},
    {"sim_objintparam_manipulation_permissions",sim_objintparam_manipulation_permissions,false},
    {"sim_objintparam_illumination_handle",sim_objintparam_illumination_handle,false},
    {"sim_objstringparam_dna",sim_objstringparam_dna,false},
    {"sim_visionfloatparam_near_clipping",sim_visionfloatparam_near_clipping,false},
    {"sim_visionfloatparam_far_clipping",sim_visionfloatparam_far_clipping,false},
    {"sim_visionintparam_resolution_x",sim_visionintparam_resolution_x,false},
    {"sim_visionintparam_resolution_y",sim_visionintparam_resolution_y,false},
    {"sim_visionfloatparam_perspective_angle",sim_visionfloatparam_perspective_angle,false},
    {"sim_visionfloatparam_ortho_size",sim_visionfloatparam_ortho_size,false},
    {"sim_visionintparam_disabled_light_components",sim_visionintparam_disabled_light_components,false},
    {"sim_visionintparam_rendering_attributes",sim_visionintparam_rendering_attributes,false},
    {"sim_visionintparam_entity_to_render",sim_visionintparam_entity_to_render,false},
    {"sim_visionintparam_windowed_size_x",sim_visionintparam_windowed_size_x,false},
    {"sim_visionintparam_windowed_size_y",sim_visionintparam_windowed_size_y,false},
    {"sim_visionintparam_windowed_pos_x",sim_visionintparam_windowed_pos_x,false},
    {"sim_visionintparam_windowed_pos_y",sim_visionintparam_windowed_pos_y,false},
    {"sim_visionintparam_pov_focal_blur",sim_visionintparam_pov_focal_blur,false},
    {"sim_visionfloatparam_pov_blur_distance",sim_visionfloatparam_pov_blur_distance,false},
    {"sim_visionfloatparam_pov_aperture",sim_visionfloatparam_pov_aperture,false},
    {"sim_visionintparam_pov_blur_sampled",sim_visionintparam_pov_blur_sampled,false},
    {"sim_visionintparam_render_mode",sim_visionintparam_render_mode,false},
    {"sim_jointintparam_motor_enabled",sim_jointintparam_motor_enabled,false},
    {"sim_jointintparam_ctrl_enabled",sim_jointintparam_ctrl_enabled,false},
    {"sim_jointfloatparam_pid_p",sim_jointfloatparam_pid_p,false},
    {"sim_jointfloatparam_pid_i",sim_jointfloatparam_pid_i,false},
    {"sim_jointfloatparam_pid_d",sim_jointfloatparam_pid_d,false},
    {"sim_jointfloatparam_intrinsic_x",sim_jointfloatparam_intrinsic_x,false},
    {"sim_jointfloatparam_intrinsic_y",sim_jointfloatparam_intrinsic_y,false},
    {"sim_jointfloatparam_intrinsic_z",sim_jointfloatparam_intrinsic_z,false},
    {"sim_jointfloatparam_intrinsic_qx",sim_jointfloatparam_intrinsic_qx,false},
    {"sim_jointfloatparam_intrinsic_qy",sim_jointfloatparam_intrinsic_qy,false},
    {"sim_jointfloatparam_intrinsic_qz",sim_jointfloatparam_intrinsic_qz,false},
    {"sim_jointfloatparam_intrinsic_qw",sim_jointfloatparam_intrinsic_qw,false},
    {"sim_jointfloatparam_velocity",sim_jointfloatparam_velocity,false},
    {"sim_jointfloatparam_spherical_qx",sim_jointfloatparam_spherical_qx,false},
    {"sim_jointfloatparam_spherical_qy",sim_jointfloatparam_spherical_qy,false},
    {"sim_jointfloatparam_spherical_qz",sim_jointfloatparam_spherical_qz,false},
    {"sim_jointfloatparam_spherical_qw",sim_jointfloatparam_spherical_qw,false},
    {"sim_jointfloatparam_upper_limit",sim_jointfloatparam_upper_limit,false},
    {"sim_jointfloatparam_kc_k",sim_jointfloatparam_kc_k,false},
    {"sim_jointfloatparam_kc_c",sim_jointfloatparam_kc_c,false},
    {"sim_jointfloatparam_ik_weight",sim_jointfloatparam_ik_weight,false},
    {"sim_jointfloatparam_error_x",sim_jointfloatparam_error_x,false},
    {"sim_jointfloatparam_error_y",sim_jointfloatparam_error_y,false},
    {"sim_jointfloatparam_error_z",sim_jointfloatparam_error_z,false},
    {"sim_jointfloatparam_error_a",sim_jointfloatparam_error_a,false},
    {"sim_jointfloatparam_error_b",sim_jointfloatparam_error_b,false},
    {"sim_jointfloatparam_error_g",sim_jointfloatparam_error_g,false},
    {"sim_jointfloatparam_error_pos",sim_jointfloatparam_error_pos,false},
    {"sim_jointfloatparam_error_angle",sim_jointfloatparam_error_angle,false},
    {"sim_jointintparam_velocity_lock",sim_jointintparam_velocity_lock,false},
    {"sim_jointintparam_vortex_dep_handle",sim_jointintparam_vortex_dep_handle,false},
    {"sim_jointfloatparam_vortex_dep_multiplication",sim_jointfloatparam_vortex_dep_multiplication,false},
    {"sim_jointfloatparam_vortex_dep_offset",sim_jointfloatparam_vortex_dep_offset,false},
    {"sim_shapefloatparam_init_velocity_x",sim_shapefloatparam_init_velocity_x,false},
    {"sim_shapefloatparam_init_velocity_y",sim_shapefloatparam_init_velocity_y,false},
    {"sim_shapefloatparam_init_velocity_z",sim_shapefloatparam_init_velocity_z,false},
    {"sim_shapeintparam_static",sim_shapeintparam_static,false},
    {"sim_shapeintparam_respondable",sim_shapeintparam_respondable,false},
    {"sim_shapefloatparam_mass",sim_shapefloatparam_mass,false},
    {"sim_shapefloatparam_texture_x",sim_shapefloatparam_texture_x,false},
    {"sim_shapefloatparam_texture_y",sim_shapefloatparam_texture_y,false},
    {"sim_shapefloatparam_texture_z",sim_shapefloatparam_texture_z,false},
    {"sim_shapefloatparam_texture_a",sim_shapefloatparam_texture_a,false},
    {"sim_shapefloatparam_texture_b",sim_shapefloatparam_texture_b,false},
    {"sim_shapefloatparam_texture_g",sim_shapefloatparam_texture_g,false},
    {"sim_shapefloatparam_texture_scaling_x",sim_shapefloatparam_texture_scaling_x,false},
    {"sim_shapefloatparam_texture_scaling_y",sim_shapefloatparam_texture_scaling_y,false},
    {"sim_shapeintparam_culling",sim_shapeintparam_culling,false},
    {"sim_shapeintparam_wireframe",sim_shapeintparam_wireframe,false},
    {"sim_shapeintparam_compound",sim_shapeintparam_compound,false},
    {"sim_shapeintparam_convex",sim_shapeintparam_convex,false},
    {"sim_shapeintparam_convex_check",sim_shapeintparam_convex_check,false},
    {"sim_shapeintparam_respondable_mask",sim_shapeintparam_respondable_mask,false},
    {"sim_shapefloatparam_init_velocity_a",sim_shapefloatparam_init_ang_velocity_x,false},
    {"sim_shapefloatparam_init_velocity_b",sim_shapefloatparam_init_ang_velocity_y,false},
    {"sim_shapefloatparam_init_velocity_g",sim_shapefloatparam_init_ang_velocity_z,false},
    {"sim_shapestringparam_color_name",sim_shapestringparam_color_name,false},
    {"sim_shapeintparam_edge_visibility",sim_shapeintparam_edge_visibility,false},
    {"sim_shapefloatparam_shading_angle",sim_shapefloatparam_shading_angle,false},
    {"sim_shapefloatparam_edge_angle",sim_shapefloatparam_edge_angle,false},
    {"sim_shapeintparam_edge_borders_hidden",sim_shapeintparam_edge_borders_hidden,false},
    {"sim_proxintparam_ray_invisibility",sim_proxintparam_ray_invisibility,false},
    {"sim_proxintparam_volume_type",sim_proxintparam_volume_type,false},
    {"sim_proxintparam_entity_to_detect",sim_proxintparam_entity_to_detect,false},
    {"sim_forcefloatparam_error_x",sim_forcefloatparam_error_x,false},
    {"sim_forcefloatparam_error_y",sim_forcefloatparam_error_y,false},
    {"sim_forcefloatparam_error_z",sim_forcefloatparam_error_z,false},
    {"sim_forcefloatparam_error_a",sim_forcefloatparam_error_a,false},
    {"sim_forcefloatparam_error_b",sim_forcefloatparam_error_b,false},
    {"sim_forcefloatparam_error_g",sim_forcefloatparam_error_g,false},
    {"sim_forcefloatparam_error_pos",sim_forcefloatparam_error_pos,false},
    {"sim_forcefloatparam_error_angle",sim_forcefloatparam_error_angle,false},
    {"sim_lightintparam_pov_casts_shadows",sim_lightintparam_pov_casts_shadows,false},
    {"sim_cameraintparam_disabled_light_components",sim_cameraintparam_disabled_light_components,false},
    {"sim_camerafloatparam_perspective_angle",sim_camerafloatparam_perspective_angle,false},
    {"sim_camerafloatparam_ortho_size",sim_camerafloatparam_ortho_size,false},
    {"sim_cameraintparam_rendering_attributes",sim_cameraintparam_rendering_attributes,false},
    {"sim_cameraintparam_pov_focal_blur",sim_cameraintparam_pov_focal_blur,false},
    {"sim_camerafloatparam_pov_blur_distance",sim_camerafloatparam_pov_blur_distance,false},
    {"sim_camerafloatparam_pov_aperture",sim_camerafloatparam_pov_aperture,false},
    {"sim_cameraintparam_pov_blur_samples",sim_cameraintparam_pov_blur_samples,false},
    {"sim_dummyintparam_link_type",sim_dummyintparam_link_type,false},
    {"sim_dummyintparam_follow_path",sim_dummyintparam_follow_path,false},
    {"sim_dummyfloatparam_follow_path_offset",sim_dummyfloatparam_follow_path_offset,false},
    {"sim.dummyintparam_follow_path",sim_dummyintparam_follow_path,false},
    {"sim.dummyfloatparam_follow_path_offset",sim_dummyfloatparam_follow_path_offset,false},
    {"sim_millintparam_volume_type",sim_millintparam_volume_type,false},
    {"sim_mirrorfloatparam_width",sim_mirrorfloatparam_width,false},
    {"sim_mirrorfloatparam_height",sim_mirrorfloatparam_height,false},
    {"sim_mirrorfloatparam_reflectance",sim_mirrorfloatparam_reflectance,false},
    {"sim_mirrorintparam_enable",sim_mirrorintparam_enable,false},
    {"sim_bullet_global_stepsize",sim_bullet_global_stepsize,false},
    {"sim_bullet_global_internalscalingfactor",sim_bullet_global_internalscalingfactor,false},
    {"sim_bullet_global_collisionmarginfactor",sim_bullet_global_collisionmarginfactor,false},
    {"sim_bullet_global_constraintsolvingiterations",sim_bullet_global_constraintsolvingiterations,false},
    {"sim_bullet_global_bitcoded",sim_bullet_global_bitcoded,false},
    {"sim_bullet_global_constraintsolvertype",sim_bullet_global_constraintsolvertype,false},
    {"sim_bullet_global_fullinternalscaling",sim_bullet_global_fullinternalscaling,false},
    {"sim_bullet_joint_stoperp",sim_bullet_joint_stoperp,false},
    {"sim_bullet_joint_stopcfm",sim_bullet_joint_stopcfm,false},
    {"sim_bullet_joint_normalcfm",sim_bullet_joint_normalcfm,false},
    {"sim_bullet_body_restitution",sim_bullet_body_restitution,false},
    {"sim_bullet_body_oldfriction",sim_bullet_body_oldfriction,false},
    {"sim_bullet_body_friction",sim_bullet_body_friction,false},
    {"sim_bullet_body_lineardamping",sim_bullet_body_lineardamping,false},
    {"sim_bullet_body_angulardamping",sim_bullet_body_angulardamping,false},
    {"sim_bullet_body_nondefaultcollisionmargingfactor",sim_bullet_body_nondefaultcollisionmargingfactor,false},
    {"sim_bullet_body_nondefaultcollisionmargingfactorconvex",sim_bullet_body_nondefaultcollisionmargingfactorconvex,false},
    {"sim_bullet_body_bitcoded",sim_bullet_body_bitcoded,false},
    {"sim_bullet_body_sticky",sim_bullet_body_sticky,false},
    {"sim_bullet_body_usenondefaultcollisionmargin",sim_bullet_body_usenondefaultcollisionmargin,false},
    {"sim_bullet_body_usenondefaultcollisionmarginconvex",sim_bullet_body_usenondefaultcollisionmarginconvex,false},
    {"sim_bullet_body_autoshrinkconvex",sim_bullet_body_autoshrinkconvex,false},
    {"sim_ode_global_stepsize",sim_ode_global_stepsize,false},
    {"sim_ode_global_internalscalingfactor",sim_ode_global_internalscalingfactor,false},
    {"sim_ode_global_cfm",sim_ode_global_cfm,false},
    {"sim_ode_global_erp",sim_ode_global_erp,false},
    {"sim_ode_global_constraintsolvingiterations",sim_ode_global_constraintsolvingiterations,false},
    {"sim_ode_global_bitcoded",sim_ode_global_bitcoded,false},
    {"sim_ode_global_randomseed",sim_ode_global_randomseed,false},
    {"sim_ode_global_fullinternalscaling",sim_ode_global_fullinternalscaling,false},
    {"sim_ode_global_quickstep",sim_ode_global_quickstep,false},
    {"sim_ode_joint_stoperp",sim_ode_joint_stoperp,false},
    {"sim_ode_joint_stopcfm",sim_ode_joint_stopcfm,false},
    {"sim_ode_joint_bounce",sim_ode_joint_bounce,false},
    {"sim_ode_joint_fudgefactor",sim_ode_joint_fudgefactor,false},
    {"sim_ode_joint_normalcfm",sim_ode_joint_normalcfm,false},
    {"sim_ode_body_friction",sim_ode_body_friction,false},
    {"sim_ode_body_softerp",sim_ode_body_softerp,false},
    {"sim_ode_body_softcfm",sim_ode_body_softcfm,false},
    {"sim_ode_body_lineardamping",sim_ode_body_lineardamping,false},
    {"sim_ode_body_angulardamping",sim_ode_body_angulardamping,false},
    {"sim_ode_body_maxcontacts",sim_ode_body_maxcontacts,false},
    {"sim_vortex_global_stepsize",sim_vortex_global_stepsize,false},
    {"sim_vortex_global_internalscalingfactor",sim_vortex_global_internalscalingfactor,false},
    {"sim_vortex_global_contacttolerance",sim_vortex_global_contacttolerance,false},
    {"sim_vortex_global_constraintlinearcompliance",sim_vortex_global_constraintlinearcompliance,false},
    {"sim_vortex_global_constraintlineardamping",sim_vortex_global_constraintlineardamping,false},
    {"sim_vortex_global_constraintlinearkineticloss",sim_vortex_global_constraintlinearkineticloss,false},
    {"sim_vortex_global_constraintangularcompliance",sim_vortex_global_constraintangularcompliance,false},
    {"sim_vortex_global_constraintangulardamping",sim_vortex_global_constraintangulardamping,false},
    {"sim_vortex_global_constraintangularkineticloss",sim_vortex_global_constraintangularkineticloss,false},
    {"sim_vortex_global_bitcoded",sim_vortex_global_bitcoded,false},
    {"sim_vortex_global_autosleep",sim_vortex_global_autosleep,false},
    {"sim_vortex_global_multithreading",sim_vortex_global_multithreading,false},
    {"sim_vortex_joint_lowerlimitdamping",sim_vortex_joint_lowerlimitdamping,false},
    {"sim_vortex_joint_upperlimitdamping",sim_vortex_joint_upperlimitdamping,false},
    {"sim_vortex_joint_lowerlimitstiffness",sim_vortex_joint_lowerlimitstiffness,false},
    {"sim_vortex_joint_upperlimitstiffness",sim_vortex_joint_upperlimitstiffness,false},
    {"sim_vortex_joint_lowerlimitrestitution",sim_vortex_joint_lowerlimitrestitution,false},
    {"sim_vortex_joint_upperlimitrestitution",sim_vortex_joint_upperlimitrestitution,false},
    {"sim_vortex_joint_lowerlimitmaxforce",sim_vortex_joint_lowerlimitmaxforce,false},
    {"sim_vortex_joint_upperlimitmaxforce",sim_vortex_joint_upperlimitmaxforce,false},
    {"sim_vortex_joint_motorconstraintfrictioncoeff",sim_vortex_joint_motorconstraintfrictioncoeff,false},
    {"sim_vortex_joint_motorconstraintfrictionmaxforce",sim_vortex_joint_motorconstraintfrictionmaxforce,false},
    {"sim_vortex_joint_motorconstraintfrictionloss",sim_vortex_joint_motorconstraintfrictionloss,false},
    {"sim_vortex_joint_p0loss",sim_vortex_joint_p0loss,false},
    {"sim_vortex_joint_p0stiffness",sim_vortex_joint_p0stiffness,false},
    {"sim_vortex_joint_p0damping",sim_vortex_joint_p0damping,false},
    {"sim_vortex_joint_p0frictioncoeff",sim_vortex_joint_p0frictioncoeff,false},
    {"sim_vortex_joint_p0frictionmaxforce",sim_vortex_joint_p0frictionmaxforce,false},
    {"sim_vortex_joint_p0frictionloss",sim_vortex_joint_p0frictionloss,false},
    {"sim_vortex_joint_p1loss",sim_vortex_joint_p1loss,false},
    {"sim_vortex_joint_p1stiffness",sim_vortex_joint_p1stiffness,false},
    {"sim_vortex_joint_p1damping",sim_vortex_joint_p1damping,false},
    {"sim_vortex_joint_p1frictioncoeff",sim_vortex_joint_p1frictioncoeff,false},
    {"sim_vortex_joint_p1frictionmaxforce",sim_vortex_joint_p1frictionmaxforce,false},
    {"sim_vortex_joint_p1frictionloss",sim_vortex_joint_p1frictionloss,false},
    {"sim_vortex_joint_p2loss",sim_vortex_joint_p2loss,false},
    {"sim_vortex_joint_p2stiffness",sim_vortex_joint_p2stiffness,false},
    {"sim_vortex_joint_p2damping",sim_vortex_joint_p2damping,false},
    {"sim_vortex_joint_p2frictioncoeff",sim_vortex_joint_p2frictioncoeff,false},
    {"sim_vortex_joint_p2frictionmaxforce",sim_vortex_joint_p2frictionmaxforce,false},
    {"sim_vortex_joint_p2frictionloss",sim_vortex_joint_p2frictionloss,false},
    {"sim_vortex_joint_a0loss",sim_vortex_joint_a0loss,false},
    {"sim_vortex_joint_a0stiffness",sim_vortex_joint_a0stiffness,false},
    {"sim_vortex_joint_a0damping",sim_vortex_joint_a0damping,false},
    {"sim_vortex_joint_a0frictioncoeff",sim_vortex_joint_a0frictioncoeff,false},
    {"sim_vortex_joint_a0frictionmaxforce",sim_vortex_joint_a0frictionmaxforce,false},
    {"sim_vortex_joint_a0frictionloss",sim_vortex_joint_a0frictionloss,false},
    {"sim_vortex_joint_a1loss",sim_vortex_joint_a1loss,false},
    {"sim_vortex_joint_a1stiffness",sim_vortex_joint_a1stiffness,false},
    {"sim_vortex_joint_a1damping",sim_vortex_joint_a1damping,false},
    {"sim_vortex_joint_a1frictioncoeff",sim_vortex_joint_a1frictioncoeff,false},
    {"sim_vortex_joint_a1frictionmaxforce",sim_vortex_joint_a1frictionmaxforce,false},
    {"sim_vortex_joint_a1frictionloss",sim_vortex_joint_a1frictionloss,false},
    {"sim_vortex_joint_a2loss",sim_vortex_joint_a2loss,false},
    {"sim_vortex_joint_a2stiffness",sim_vortex_joint_a2stiffness,false},
    {"sim_vortex_joint_a2damping",sim_vortex_joint_a2damping,false},
    {"sim_vortex_joint_a2frictioncoeff",sim_vortex_joint_a2frictioncoeff,false},
    {"sim_vortex_joint_a2frictionmaxforce",sim_vortex_joint_a2frictionmaxforce,false},
    {"sim_vortex_joint_a2frictionloss",sim_vortex_joint_a2frictionloss,false},
    {"sim_vortex_joint_dependencyfactor",sim_vortex_joint_dependencyfactor,false},
    {"sim_vortex_joint_dependencyoffset",sim_vortex_joint_dependencyoffset,false},
    {"sim_vortex_joint_bitcoded",sim_vortex_joint_bitcoded,false},
    {"sim_vortex_joint_relaxationenabledbc",sim_vortex_joint_relaxationenabledbc,false},
    {"sim_vortex_joint_frictionenabledbc",sim_vortex_joint_frictionenabledbc,false},
    {"sim_vortex_joint_frictionproportionalbc",sim_vortex_joint_frictionproportionalbc,false},
    {"sim_vortex_joint_objectid",sim_vortex_joint_objectid,false},
    {"sim_vortex_joint_dependentobjectid",sim_vortex_joint_dependentobjectid,false},
    {"sim_vortex_joint_motorfrictionenabled",sim_vortex_joint_motorfrictionenabled,false},
    {"sim_vortex_joint_proportionalmotorfriction",sim_vortex_joint_proportionalmotorfriction,false},
    {"sim_vortex_body_primlinearaxisfriction",sim_vortex_body_primlinearaxisfriction,false},
    {"sim_vortex_body_seclinearaxisfriction",sim_vortex_body_seclinearaxisfriction,false},
    {"sim_vortex_body_primangularaxisfriction",sim_vortex_body_primangularaxisfriction,false},
    {"sim_vortex_body_secangularaxisfriction",sim_vortex_body_secangularaxisfriction,false},
    {"sim_vortex_body_normalangularaxisfriction",sim_vortex_body_normalangularaxisfriction,false},
    {"sim_vortex_body_primlinearaxisstaticfrictionscale",sim_vortex_body_primlinearaxisstaticfrictionscale,false},
    {"sim_vortex_body_seclinearaxisstaticfrictionscale",sim_vortex_body_seclinearaxisstaticfrictionscale,false},
    {"sim_vortex_body_primangularaxisstaticfrictionscale",sim_vortex_body_primangularaxisstaticfrictionscale,false},
    {"sim_vortex_body_secangularaxisstaticfrictionscale",sim_vortex_body_secangularaxisstaticfrictionscale,false},
    {"sim_vortex_body_normalangularaxisstaticfrictionscale",sim_vortex_body_normalangularaxisstaticfrictionscale,false},
    {"sim_vortex_body_compliance",sim_vortex_body_compliance,false},
    {"sim_vortex_body_damping",sim_vortex_body_damping,false},
    {"sim_vortex_body_restitution",sim_vortex_body_restitution,false},
    {"sim_vortex_body_restitutionthreshold",sim_vortex_body_restitutionthreshold,false},
    {"sim_vortex_body_adhesiveforce",sim_vortex_body_adhesiveforce,false},
    {"sim_vortex_body_linearvelocitydamping",sim_vortex_body_linearvelocitydamping,false},
    {"sim_vortex_body_angularvelocitydamping",sim_vortex_body_angularvelocitydamping,false},
    {"sim_vortex_body_primlinearaxisslide",sim_vortex_body_primlinearaxisslide,false},
    {"sim_vortex_body_seclinearaxisslide",sim_vortex_body_seclinearaxisslide,false},
    {"sim_vortex_body_primangularaxisslide",sim_vortex_body_primangularaxisslide,false},
    {"sim_vortex_body_secangularaxisslide",sim_vortex_body_secangularaxisslide,false},
    {"sim_vortex_body_normalangularaxisslide",sim_vortex_body_normalangularaxisslide,false},
    {"sim_vortex_body_primlinearaxisslip",sim_vortex_body_primlinearaxisslip,false},
    {"sim_vortex_body_seclinearaxisslip",sim_vortex_body_seclinearaxisslip,false},
    {"sim_vortex_body_primangularaxisslip",sim_vortex_body_primangularaxisslip,false},
    {"sim_vortex_body_secangularaxisslip",sim_vortex_body_secangularaxisslip,false},
    {"sim_vortex_body_normalangularaxisslip",sim_vortex_body_normalangularaxisslip,false},
    {"sim_vortex_body_autosleeplinearspeedthreshold",sim_vortex_body_autosleeplinearspeedthreshold,false},
    {"sim_vortex_body_autosleeplinearaccelthreshold",sim_vortex_body_autosleeplinearaccelthreshold,false},
    {"sim_vortex_body_autosleepangularspeedthreshold",sim_vortex_body_autosleepangularspeedthreshold,false},
    {"sim_vortex_body_autosleepangularaccelthreshold",sim_vortex_body_autosleepangularaccelthreshold,false},
    {"sim_vortex_body_skinthickness",sim_vortex_body_skinthickness,false},
    {"sim_vortex_body_autoangulardampingtensionratio",sim_vortex_body_autoangulardampingtensionratio,false},
    {"sim_vortex_body_primaxisvectorx",sim_vortex_body_primaxisvectorx,false},
    {"sim_vortex_body_primaxisvectory",sim_vortex_body_primaxisvectory,false},
    {"sim_vortex_body_primaxisvectorz",sim_vortex_body_primaxisvectorz,false},
    {"sim_vortex_body_primlinearaxisfrictionmodel",sim_vortex_body_primlinearaxisfrictionmodel,false},
    {"sim_vortex_body_seclinearaxisfrictionmodel",sim_vortex_body_seclinearaxisfrictionmodel,false},
    {"sim_vortex_body_primangulararaxisfrictionmodel",sim_vortex_body_primangulararaxisfrictionmodel,false},
    {"sim_vortex_body_secmangulararaxisfrictionmodel",sim_vortex_body_secmangulararaxisfrictionmodel,false},
    {"sim_vortex_body_normalmangulararaxisfrictionmodel",sim_vortex_body_normalmangulararaxisfrictionmodel,false},
    {"sim_vortex_body_bitcoded",sim_vortex_body_bitcoded,false},
    {"sim_vortex_body_autosleepsteplivethreshold",sim_vortex_body_autosleepsteplivethreshold,false},
    {"sim_vortex_body_materialuniqueid",sim_vortex_body_materialuniqueid,false},
    {"sim_vortex_body_pureshapesasconvex",sim_vortex_body_pureshapesasconvex,false},
    {"sim_vortex_body_convexshapesasrandom",sim_vortex_body_convexshapesasrandom,false},
    {"sim_vortex_body_randomshapesasterrain",sim_vortex_body_randomshapesasterrain,false},
    {"sim_vortex_body_fastmoving",sim_vortex_body_fastmoving,false},
    {"sim_vortex_body_autoslip",sim_vortex_body_autoslip,false},
    {"sim_vortex_body_seclinaxissameasprimlinaxis",sim_vortex_body_seclinaxissameasprimlinaxis,false},
    {"sim_vortex_body_secangaxissameasprimangaxis",sim_vortex_body_secangaxissameasprimangaxis,false},
    {"sim_vortex_body_normangaxissameasprimangaxis",sim_vortex_body_normangaxissameasprimangaxis,false},
    {"sim_vortex_body_autoangulardamping",sim_vortex_body_autoangulardamping,false},
    {"sim_newton_global_stepsize",sim_newton_global_stepsize,false},
    {"sim_newton_global_contactmergetolerance",sim_newton_global_contactmergetolerance,false},
    {"sim_newton_global_constraintsolvingiterations",sim_newton_global_constraintsolvingiterations,false},
    {"sim_newton_global_bitcoded",sim_newton_global_bitcoded,false},
    {"sim_newton_global_multithreading",sim_newton_global_multithreading,false},
    {"sim_newton_global_exactsolver",sim_newton_global_exactsolver,false},
    {"sim_newton_global_highjointaccuracy",sim_newton_global_highjointaccuracy,false},
    {"sim_newton_joint_dependencyfactor",sim_newton_joint_dependencyfactor,false},
    {"sim_newton_joint_dependencyoffset",sim_newton_joint_dependencyoffset,false},
    {"sim_newton_joint_objectid",sim_newton_joint_objectid,false},
    {"sim_newton_joint_dependentobjectid",sim_newton_joint_dependentobjectid,false},
    {"sim_newton_body_staticfriction",sim_newton_body_staticfriction,false},
    {"sim_newton_body_kineticfriction",sim_newton_body_kineticfriction,false},
    {"sim_newton_body_restitution",sim_newton_body_restitution,false},
    {"sim_newton_body_lineardrag",sim_newton_body_lineardrag,false},
    {"sim_newton_body_angulardrag",sim_newton_body_angulardrag,false},
    {"sim_newton_body_bitcoded",sim_newton_body_bitcoded,false},
    {"sim_newton_body_fastmoving",sim_newton_body_fastmoving,false},
    {"sim_vortex_bodyfrictionmodel_box",sim_vortex_bodyfrictionmodel_box,false},
    {"sim_vortex_bodyfrictionmodel_scaledbox",sim_vortex_bodyfrictionmodel_scaledbox,false},
    {"sim_vortex_bodyfrictionmodel_proplow",sim_vortex_bodyfrictionmodel_proplow,false},
    {"sim_vortex_bodyfrictionmodel_prophigh",sim_vortex_bodyfrictionmodel_prophigh,false},
    {"sim_vortex_bodyfrictionmodel_scaledboxfast",sim_vortex_bodyfrictionmodel_scaledboxfast,false},
    {"sim_vortex_bodyfrictionmodel_neutral",sim_vortex_bodyfrictionmodel_neutral,false},
    {"sim_vortex_bodyfrictionmodel_none",sim_vortex_bodyfrictionmodel_none,false},
    {"sim_bullet_constraintsolvertype_sequentialimpulse",sim_bullet_constraintsolvertype_sequentialimpulse,false},
    {"sim_bullet_constraintsolvertype_nncg",sim_bullet_constraintsolvertype_nncg,false},
    {"sim_bullet_constraintsolvertype_dantzig",sim_bullet_constraintsolvertype_dantzig,false},
    {"sim_bullet_constraintsolvertype_projectedgaussseidel",sim_bullet_constraintsolvertype_projectedgaussseidel,false},
    {"sim_filtercomponent_originalimage",sim_filtercomponent_originalimage_deprecated,false},
    {"sim_filtercomponent_originaldepth",sim_filtercomponent_originaldepth_deprecated,false},
    {"sim_filtercomponent_uniformimage",sim_filtercomponent_uniformimage_deprecated,false},
    {"sim_filtercomponent_tooutput",sim_filtercomponent_tooutput_deprecated,false},
    {"sim_filtercomponent_tobuffer1",sim_filtercomponent_tobuffer1_deprecated,false},
    {"sim_filtercomponent_tobuffer2",sim_filtercomponent_tobuffer2_deprecated,false},
    {"sim_filtercomponent_frombuffer1",sim_filtercomponent_frombuffer1_deprecated,false},
    {"sim_filtercomponent_frombuffer2",sim_filtercomponent_frombuffer2_deprecated,false},
    {"sim_filtercomponent_swapbuffers",sim_filtercomponent_swapbuffers_deprecated,false},
    {"sim_filtercomponent_addbuffer1",sim_filtercomponent_addbuffer1_deprecated,false},
    {"sim_filtercomponent_subtractbuffer1",sim_filtercomponent_subtractbuffer1_deprecated,false},
    {"sim_filtercomponent_multiplywithbuffer1",sim_filtercomponent_multiplywithbuffer1_deprecated,false},
    {"sim_filtercomponent_horizontalflip",sim_filtercomponent_horizontalflip_deprecated,false},
    {"sim_filtercomponent_verticalflip",sim_filtercomponent_verticalflip_deprecated,false},
    {"sim_filtercomponent_rotate",sim_filtercomponent_rotate_deprecated,false},
    {"sim_filtercomponent_shift",sim_filtercomponent_shift_deprecated,false},
    {"sim_filtercomponent_resize",sim_filtercomponent_resize_deprecated,false},
    {"sim_filtercomponent_3x3filter",sim_filtercomponent_3x3filter_deprecated,false},
    {"sim_filtercomponent_5x5filter",sim_filtercomponent_5x5filter_deprecated,false},
    {"sim_filtercomponent_sharpen",sim_filtercomponent_sharpen_deprecated,false},
    {"sim_filtercomponent_edge",sim_filtercomponent_edge_deprecated,false},
    {"sim_filtercomponent_rectangularcut",sim_filtercomponent_rectangularcut_deprecated,false},
    {"sim_filtercomponent_circularcut",sim_filtercomponent_circularcut_deprecated,false},
    {"sim_filtercomponent_normalize",sim_filtercomponent_normalize_deprecated,false},
    {"sim_filtercomponent_intensityscale",sim_filtercomponent_intensityscale_deprecated,false},
    {"sim_filtercomponent_keeporremovecolors",sim_filtercomponent_keeporremovecolors_deprecated,false},
    {"sim_filtercomponent_scaleandoffsetcolors",sim_filtercomponent_scaleandoffsetcolors_deprecated,false},
    {"sim_filtercomponent_binary",sim_filtercomponent_binary_deprecated,false},
    {"sim_filtercomponent_swapwithbuffer1",sim_filtercomponent_swapwithbuffer1_deprecated,false},
    {"sim_filtercomponent_addtobuffer1",sim_filtercomponent_addtobuffer1_deprecated,false},
    {"sim_filtercomponent_subtractfrombuffer1",sim_filtercomponent_subtractfrombuffer1_deprecated,false},
    {"sim_filtercomponent_correlationwithbuffer1",sim_filtercomponent_correlationwithbuffer1_deprecated,false},
    {"sim_filtercomponent_colorsegmentation",sim_filtercomponent_colorsegmentation_deprecated,false},
    {"sim_filtercomponent_blobextraction",sim_filtercomponent_blobextraction_deprecated,false},
    {"sim_filtercomponent_imagetocoord",sim_filtercomponent_imagetocoord_deprecated,false},
    {"sim_filtercomponent_pixelchange",sim_filtercomponent_pixelchange_deprecated,false},
    {"sim_filtercomponent_velodyne",sim_filtercomponent_velodyne_deprecated,false},
    {"sim_filtercomponent_todepthoutput",sim_filtercomponent_todepthoutput_deprecated,false},
    {"sim_filtercomponent_customized",sim_filtercomponent_customized_deprecated,false},
    {"sim_buffer_uint8",sim_buffer_uint8,false},
    {"sim_buffer_int8",sim_buffer_int8,false},
    {"sim_buffer_uint16",sim_buffer_uint16,false},
    {"sim_buffer_int16",sim_buffer_int16,false},
    {"sim_buffer_uint32",sim_buffer_uint32,false},
    {"sim_buffer_int32",sim_buffer_int32,false},
    {"sim_buffer_float",sim_buffer_float,false},
    {"sim_buffer_double",sim_buffer_double,false},
    {"sim_buffer_uint8rgb",sim_buffer_uint8rgb,false},
    {"sim_buffer_uint8bgr",sim_buffer_uint8bgr,false},
    {"sim_imgcomb_vertical",sim_imgcomb_vertical,false},
    {"sim_imgcomb_horizontal",sim_imgcomb_horizontal,false},
    {"sim_dynmat_default",sim_dynmat_default,false},
    {"sim_dynmat_highfriction",sim_dynmat_highfriction,false},
    {"sim_dynmat_lowfriction",sim_dynmat_lowfriction,false},
    {"sim_dynmat_nofriction",sim_dynmat_nofriction,false},
    {"sim_dynmat_reststackgrasp",sim_dynmat_reststackgrasp,false},
    {"sim_dynmat_foot",sim_dynmat_foot,false},
    {"sim_dynmat_wheel",sim_dynmat_wheel,false},
    {"sim_dynmat_gripper",sim_dynmat_gripper,false},
    {"sim_dynmat_floor",sim_dynmat_floor,false},
    // for backward compatibility:
    {"sim_pplanfloatparam_x_min",sim_pplanfloatparam_x_min,false},
    {"sim_pplanfloatparam_x_range",sim_pplanfloatparam_x_range,false},
    {"sim_pplanfloatparam_y_min",sim_pplanfloatparam_y_min,false},
    {"sim_pplanfloatparam_y_range",sim_pplanfloatparam_y_range,false},
    {"sim_pplanfloatparam_z_min",sim_pplanfloatparam_z_min,false},
    {"sim_pplanfloatparam_z_range",sim_pplanfloatparam_z_range,false},
    {"sim_pplanfloatparam_delta_min",sim_pplanfloatparam_delta_min,false},
    {"sim_pplanfloatparam_delta_range",sim_pplanfloatparam_delta_range,false},
    {"sim_ui_menu_title",sim_ui_menu_title,false},
    {"sim_ui_menu_minimize",sim_ui_menu_minimize,false},
    {"sim_ui_menu_close",sim_ui_menu_close,false},
    {"sim_api_errormessage_ignore",sim_api_errormessage_ignore,false},
    {"sim_api_errormessage_report",sim_api_errormessage_report,false},
    {"sim_api_errormessage_output",sim_api_errormessage_output,false},
    {"sim_ui_property_visible",sim_ui_property_visible,false},
    {"sim_ui_property_visibleduringsimulationonly",sim_ui_property_visibleduringsimulationonly,false},
    {"sim_ui_property_moveable",sim_ui_property_moveable,false},
    {"sim_ui_property_relativetoleftborder",sim_ui_property_relativetoleftborder,false},
    {"sim_ui_property_relativetotopborder",sim_ui_property_relativetotopborder,false},
    {"sim_ui_property_fixedwidthfont",sim_ui_property_fixedwidthfont,false},
    {"sim_ui_property_systemblock",sim_ui_property_systemblock,false},
    {"sim_ui_property_settocenter",sim_ui_property_settocenter,false},
    {"sim_ui_property_rolledup",sim_ui_property_rolledup,false},
    {"sim_ui_property_selectassociatedobject",sim_ui_property_selectassociatedobject,false},
    {"sim_ui_property_visiblewhenobjectselected",sim_ui_property_visiblewhenobjectselected,false},
    {"sim_ui_property_systemblockcanmovetofront",sim_ui_property_systemblockcanmovetofront,false},
    {"sim_ui_property_pauseactive",sim_ui_property_pauseactive,false},
    {"sim_2delement_menu_title",sim_ui_menu_title,false},
    {"sim_2delement_menu_minimize",sim_ui_menu_minimize,false},
    {"sim_2delement_menu_close",sim_ui_menu_close,false},
    {"sim_2delement_property_visible",sim_ui_property_visible,false},
    {"sim_2delement_property_visibleduringsimulationonly",sim_ui_property_visibleduringsimulationonly,false},
    {"sim_2delement_property_moveable",sim_ui_property_moveable,false},
    {"sim_2delement_property_relativetoleftborder",sim_ui_property_relativetoleftborder,false},
    {"sim_2delement_property_relativetotopborder",sim_ui_property_relativetotopborder,false},
    {"sim_2delement_property_fixedwidthfont",sim_ui_property_fixedwidthfont,false},
    {"sim_2delement_property_systemblock",sim_ui_property_systemblock,false},
    {"sim_2delement_property_settocenter",sim_ui_property_settocenter,false},
    {"sim_2delement_property_rolledup",sim_ui_property_rolledup,false},
    {"sim_2delement_property_selectassociatedobject",sim_ui_property_selectassociatedobject,false},
    {"sim_2delement_property_visiblewhenobjectselected",sim_ui_property_visiblewhenobjectselected,false},
    {"sim_pathproperty_invert_velocity",sim_pathproperty_invert_velocity_deprecated,false},
    {"sim_pathproperty_infinite_acceleration",sim_pathproperty_infinite_acceleration_deprecated,false},
    {"sim_pathproperty_auto_velocity_profile_translation",sim_pathproperty_auto_velocity_profile_translation_deprecated,false},
    {"sim_pathproperty_auto_velocity_profile_rotation",sim_pathproperty_auto_velocity_profile_rotation_deprecated,false},
    {"sim_pathproperty_endpoints_at_zero",sim_pathproperty_endpoints_at_zero_deprecated,false},
    {"sim_boolparam_joint_motion_handling_enabled",sim_boolparam_joint_motion_handling_enabled_deprecated,false},
    {"sim_boolparam_path_motion_handling_enabled",sim_boolparam_path_motion_handling_enabled_deprecated,false},
    {"sim_jointmode_motion",sim_jointmode_motion_deprecated,false},
    {"sim.syscb_aos_run",sim_syscb_aos_run_old,false},
    {"sim.addonscriptcall_run",sim_syscb_aos_run_old,false},
    {"sim_addonscriptcall_run",sim_syscb_aos_run_old,false},
    {"sim_navigation_camerafly",sim_navigation_camerafly_old,false},
    {"sim_banner_left",sim_banner_left,false},
    {"sim_banner_right",sim_banner_right,false},
    {"sim_banner_nobackground",sim_banner_nobackground,false},
    {"sim_banner_overlay",sim_banner_overlay,false},
    {"sim_banner_followparentvisibility",sim_banner_followparentvisibility,false},
    {"sim_banner_clickselectsparent",sim_banner_clickselectsparent,false},
    {"sim_banner_clicktriggersevent",sim_banner_clicktriggersevent,false},
    {"sim_banner_facingcamera",sim_banner_facingcamera,false},
    {"sim_banner_fullyfacingcamera",sim_banner_fullyfacingcamera,false},
    {"sim_banner_backfaceculling",sim_banner_backfaceculling,false},
    {"sim_banner_keepsamesize",sim_banner_keepsamesize,false},
    {"sim_banner_bitmapfont",sim_banner_bitmapfont,false},
    {"",-1,false}
};

bool isObjectAssociatedWithThisThreadedChildScriptValid_old(luaWrap_lua_State* L)
{
    int id=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* script=App::worldContainer->getScriptFromHandle(id);
    if (script==nullptr)
        return(false);
    int h=script->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_childscript);
    return(h!=-1);
}

void pushCorrectTypeOntoLuaStack_old(luaWrap_lua_State* L,const std::string& buff)
{ // Pushes nil, false, true, number or string (in that order!!) onto the stack depending on the buff content!
    int t=getCorrectType_old(buff);
    if (t==0)
        luaWrap_lua_pushnil(L);
    if (t==1)
        luaWrap_lua_pushboolean(L,0);
    if (t==2)
        luaWrap_lua_pushboolean(L,1);
    if (t==3)
    {
        double floatVal;
        tt::getValidFloat(buff.c_str(),floatVal);
        luaWrap_lua_pushnumber(L,floatVal);
    }
    if (t==4)
        luaWrap_lua_pushlstring(L,buff.c_str(),buff.length());
}

int getCorrectType_old(const std::string& buff)
{ // returns 0=nil, 1=boolean false, 2=boolean true, 3=number or 4=string (in that order!!) depending on the buff content!
    if (buff.length()!=0)
    {
        if (buff.length()!=strlen(buff.c_str()))
            return(4); // We have embedded zeros, this has definitively to be a string:
    }
    if (strcmp(buff.c_str(),"nil")==0)
        return(0);
    if (strcmp(buff.c_str(),"false")==0)
        return(1);
    if (strcmp(buff.c_str(),"true")==0)
        return(2);
    double floatVal;
    if (tt::getValidFloat(buff.c_str(),floatVal))
        return(3);
    return(4);
}

void getScriptTree_old(luaWrap_lua_State* L,bool selfIncluded,std::vector<int>& scriptHandles)
{ // Returns all scripts that are built under the current one
    scriptHandles.clear();
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);

    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (it!=nullptr)
    {
        if (it->getScriptType()==sim_scripttype_mainscript)
        { // we have a main script here
            if (selfIncluded)
                scriptHandles.push_back(currentScriptID);
            for (size_t i=0;i<App::currentWorld->sceneObjects->getObjectCount();i++)
            {
                CSceneObject* q=App::currentWorld->sceneObjects->getObjectFromIndex(i);
                CScriptObject* lso=App::currentWorld->embeddedScriptContainer->getScriptFromObjectAttachedTo(sim_scripttype_childscript,q->getObjectHandle());
                if (lso!=nullptr)
                    scriptHandles.push_back(lso->getScriptHandle());
            }
        }

        if (it->getScriptType()==sim_scripttype_childscript)
        { // we have a child script
            CSceneObject* obj=App::currentWorld->sceneObjects->getObjectFromHandle(it->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_childscript));
            if (obj!=nullptr)
            { // should always pass
                if (selfIncluded)
                    scriptHandles.push_back(currentScriptID);

                std::vector<CSceneObject*> objList;
                obj->getAllObjectsRecursive(&objList,false);
                for (int i=0;i<int(objList.size());i++)
                {
                    CScriptObject* lso=App::currentWorld->embeddedScriptContainer->getScriptFromObjectAttachedTo(sim_scripttype_childscript,objList[i]->getObjectHandle());
                    if (lso!=nullptr)
                        scriptHandles.push_back(lso->getScriptHandle());
                }
            }
        }

        if (it->getScriptType()==sim_scripttype_customizationscript)
        { // we have a customization script
            CSceneObject* obj=App::currentWorld->sceneObjects->getObjectFromHandle(it->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_customizationscript));
            if (obj!=nullptr)
            { // should always pass
                if (selfIncluded)
                {
                    CScriptObject* aScript=App::currentWorld->embeddedScriptContainer->getScriptFromObjectAttachedTo(sim_scripttype_childscript,obj->getObjectHandle());
                    if (aScript!=nullptr)
                        scriptHandles.push_back(aScript->getScriptHandle());
                }

                std::vector<CSceneObject*> objList;
                obj->getAllObjectsRecursive(&objList,false);
                for (int i=0;i<int(objList.size());i++)
                {
                    CScriptObject* lso=App::currentWorld->embeddedScriptContainer->getScriptFromObjectAttachedTo(sim_scripttype_childscript,objList[i]->getObjectHandle());
                    if (lso!=nullptr)
                    {
                        scriptHandles.push_back(lso->getScriptHandle());
                    }
                }
            }
        }
    }
}

void getScriptChain_old(luaWrap_lua_State* L,bool selfIncluded,bool mainIncluded,std::vector<int>& scriptHandles)
{ // Returns all script IDs that are parents (or grand-parents,grand-grand-parents, etc.) of the current one
    scriptHandles.clear();
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);

    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);

    if (it!=nullptr)
    {
        if (it->getScriptType()==sim_scripttype_mainscript)
        { // we have a main script here
            if (selfIncluded&&mainIncluded)
                scriptHandles.push_back(currentScriptID);
        }

        if (it->getScriptType()==sim_scripttype_childscript)
        { // we have a child script here
            CSceneObject* obj=App::currentWorld->sceneObjects->getObjectFromHandle(it->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_childscript));
            if (obj!=nullptr)
            {
                if (selfIncluded)
                    scriptHandles.push_back(currentScriptID);
                while (obj->getParent()!=nullptr)
                {
                    obj=obj->getParent();
                    CScriptObject* lso=App::currentWorld->embeddedScriptContainer->getScriptFromObjectAttachedTo(sim_scripttype_childscript,obj->getObjectHandle());
                    if (lso!=nullptr)
                        scriptHandles.push_back(lso->getScriptHandle());
                }
                if (mainIncluded)
                {
                    CScriptObject* lso=App::currentWorld->embeddedScriptContainer->getMainScript();
                    if (lso!=nullptr)
                        scriptHandles.push_back(lso->getScriptHandle());
                }
            }
        }

        if (it->getScriptType()==sim_scripttype_customizationscript)
        { // we have a customization script here
            CSceneObject* obj=App::currentWorld->sceneObjects->getObjectFromHandle(it->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_customizationscript));
            if (obj!=nullptr)
            {
                if (selfIncluded)
                {
                    CScriptObject* aScript=App::currentWorld->embeddedScriptContainer->getScriptFromObjectAttachedTo(sim_scripttype_childscript,obj->getObjectHandle());
                    if (aScript!=nullptr)
                        scriptHandles.push_back(aScript->getScriptHandle());
                }
                while (obj->getParent()!=nullptr)
                {
                    obj=obj->getParent();
                    CScriptObject* lso=App::currentWorld->embeddedScriptContainer->getScriptFromObjectAttachedTo(sim_scripttype_childscript,obj->getObjectHandle());
                    if (lso!=nullptr)
                        scriptHandles.push_back(lso->getScriptHandle());
                }
                if (mainIncluded)
                {
                    CScriptObject* lso=App::currentWorld->embeddedScriptContainer->getMainScript();
                    if (lso!=nullptr)
                        scriptHandles.push_back(lso->getScriptHandle());
                }
            }
        }
    }
}
bool readCustomFunctionDataFromStack_old(luaWrap_lua_State* L,int ind,int dataType,
                                     std::vector<char>& inBoolVector,
                                     std::vector<int>& inIntVector,
                                     std::vector<float>& inFloatVector,
                                     std::vector<double>& inDoubleVector,
                                     std::vector<std::string>& inStringVector,
                                     std::vector<std::string>& inCharVector,
                                     std::vector<int>& inInfoVector)
{ // return value false means there is no more data on the stack
    if (luaWrap_lua_gettop(L)<ind)
        return(false); // not enough data on the stack
    inInfoVector.push_back(sim_script_arg_invalid); // Dummy value for type
    inInfoVector.push_back(0);                      // dummy value for size
    if (luaWrap_lua_isnil(L,ind))
    { // Special case: nil should not generate a sim_script_arg_invalid type!
        inInfoVector[inInfoVector.size()-2]=sim_script_arg_null;
        return(true);
    }
    if (dataType&sim_script_arg_table)
    { // we have to read a table:
        dataType^=sim_script_arg_table;
        if (!luaWrap_lua_istable(L,ind))
            return(true); // this is not a table
        int dataSize=int(luaWrap_lua_rawlen(L,ind));
        std::vector<char> boolV;
        std::vector<int> intV;
        std::vector<float> floatV;
        std::vector<double> doubleV;
        std::vector<std::string> stringV;
        for (int i=0;i<dataSize;i++)
        {
            luaWrap_lua_rawgeti(L,ind,i+1);
            if (dataType==sim_script_arg_bool)
            {
                if (!luaWrap_lua_isboolean(L,-1))
                {
                    luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                    return(true); // we don't have the correct data type
                }
                boolV.push_back(luaToBool(L,-1));
            }
            else if (dataType==sim_script_arg_int32)
            {
                if (!luaWrap_lua_isnumber(L,-1))
                {
                    luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                    return(true); // we don't have the correct data type
                }
                intV.push_back(luaToInt(L,-1));
            }
            else if (dataType==sim_script_arg_float)
            {
                if (!luaWrap_lua_isnumber(L,-1))
                {
                    luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                    return(true); // we don't have the correct data type
                }
                floatV.push_back(luaToDouble(L,-1));
            }
            else if (dataType==sim_script_arg_double)
            {
                if (!luaWrap_lua_isnumber(L,-1))
                {
                    luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                    return(true); // we don't have the correct data type
                }
                doubleV.push_back(luaToDouble(L,-1));
            }
            else if (dataType==sim_script_arg_string)
            {
                if (!luaWrap_lua_isstring(L,-1))
                {
                    luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                    return(true); // we don't have the correct data type
                }
                stringV.push_back(std::string(luaWrap_lua_tostring(L,-1)));
            }
            else
            {
                luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
                return(true); // data type not recognized!
            }
            luaWrap_lua_pop(L,1); // we have to pop the value that was pushed with luaWrap_lua_rawgeti
        }
        // All values in the tables passed the check!
        for (int i=0;i<int(boolV.size());i++)
            inBoolVector.push_back(boolV[i]);
        for (int i=0;i<int(intV.size());i++)
            inIntVector.push_back(intV[i]);
        for (int i=0;i<int(floatV.size());i++)
            inFloatVector.push_back(floatV[i]);
        for (int i=0;i<int(doubleV.size());i++)
            inDoubleVector.push_back(doubleV[i]);
        for (int i=0;i<int(stringV.size());i++)
            inStringVector.push_back(stringV[i]);
        inInfoVector[inInfoVector.size()-1]=dataSize; // Size of the table
        inInfoVector[inInfoVector.size()-2]=dataType|sim_script_arg_table; // Type
        return(true);
    }
    else
    { // we have simple data here (not a table)
        if (dataType==sim_script_arg_bool)
        {
            if (!luaWrap_lua_isboolean(L,ind))
                return(true); // we don't have the correct data type
            inBoolVector.push_back(luaToBool(L,ind));
            inInfoVector[inInfoVector.size()-2]=dataType;
        }
        else if (dataType==sim_script_arg_int32)
        {
            if (!luaWrap_lua_isnumber(L,ind))
                return(true); // we don't have the correct data type
            inIntVector.push_back(luaToInt(L,ind));
            inInfoVector[inInfoVector.size()-2]=dataType;
        }
        else if (dataType==sim_script_arg_float)
        {
            if (!luaWrap_lua_isnumber(L,ind))
                return(true); // we don't have the correct data type
            inFloatVector.push_back(luaToDouble(L,ind));
            inInfoVector[inInfoVector.size()-2]=dataType;
        }
        else if (dataType==sim_script_arg_double)
        {
            if (!luaWrap_lua_isnumber(L,ind))
                return(true); // we don't have the correct data type
            inDoubleVector.push_back(luaToDouble(L,ind));
            inInfoVector[inInfoVector.size()-2]=dataType;
        }
        else if (dataType==sim_script_arg_string)
        {
            if (!luaWrap_lua_isstring(L,ind))
                return(true); // we don't have the correct data type
            inStringVector.push_back(std::string(luaWrap_lua_tostring(L,ind)));
            inInfoVector[inInfoVector.size()-2]=dataType;
        }
        else if (dataType==sim_script_arg_charbuff)
        {
            if (!luaWrap_lua_isstring(L,ind))
                return(true); // we don't have the correct data type
            size_t dataLength;
            char* data=(char*)luaWrap_lua_tolstring(L,ind,&dataLength);
            inCharVector.push_back(std::string(data,dataLength));
            inInfoVector[inInfoVector.size()-2]=dataType;
            inInfoVector[inInfoVector.size()-1]=int(dataLength);
        }
        return(true); // data type not recognized!
    }
    return(true);
}

void writeCustomFunctionDataOntoStack_old(luaWrap_lua_State* L,int dataType,int dataSize,
                                      bool* boolData,int& boolDataPos,
                                      int* intData,int& intDataPos,
                                      float* floatData,int& floatDataPos,
                                      double* doubleData,int& doubleDataPos,
                                      char* stringData,int& stringDataPos,
                                      char* charData,int& charDataPos)
{
    if (((dataType|sim_script_arg_table)-sim_script_arg_table)==sim_script_arg_charbuff)
    { // special handling here
        luaWrap_lua_pushlstring(L,charData+charDataPos,dataSize);
        charDataPos+=dataSize;
    }
    else
    {
        int newTablePos=0;
        bool weHaveATable=false;
        if (dataType&sim_script_arg_table)
        { // we have a table
            luaWrap_lua_newtable(L);
            newTablePos=luaWrap_lua_gettop(L);
            dataType^=sim_script_arg_table;
            weHaveATable=true;
        }
        else
            dataSize=1;
        for (int i=0;i<dataSize;i++)
        {
            if (dataType==sim_script_arg_bool)
                luaWrap_lua_pushboolean(L,boolData[boolDataPos++]);
            else if (dataType==sim_script_arg_int32)
                luaWrap_lua_pushinteger(L,intData[intDataPos++]);
            else if (dataType==sim_script_arg_float)
                luaWrap_lua_pushnumber(L,floatData[floatDataPos++]);
            else if (dataType==sim_script_arg_double)
                luaWrap_lua_pushnumber(L,doubleData[doubleDataPos++]);
            else if (dataType==sim_script_arg_string)
            {
                luaWrap_lua_pushstring(L,stringData+stringDataPos);
                stringDataPos+=(int)strlen(stringData+stringDataPos)+1; // Thanks to Ulrich Schwesinger for noticing a bug here!
            }
            else
                luaWrap_lua_pushnil(L); // that is an error!

            if (weHaveATable) // that's when we have a table
                luaWrap_lua_rawseti(L,newTablePos,i+1);
        }
    }
}
int _simResetMill(luaWrap_lua_State* L)
{ // DEPRECATED since V4.0.1. has no effect anymore
    LUA_START("sim.ResetMill");
    LUA_END(0);
}

int _simHandleMill(luaWrap_lua_State* L)
{ // DEPRECATED since V4.0.1. has no effect anymore
    LUA_START("sim.HandleMill");
    LUA_END(0);
}

int _simResetMilling(luaWrap_lua_State* L)
{ // DEPRECATED since V4.0.1. has no effect anymore
    LUA_START("sim.ResetMilling");
    LUA_END(0);
}


int _simOpenTextEditor(luaWrap_lua_State* L)
{ // DEPRECATED since V3.6.0
    TRACE_LUA_API;
    LUA_START("sim.openTextEditor");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string initText(luaWrap_lua_tostring(L,1));
        int res=checkOneGeneralInputArgument(L,2,lua_arg_string,0,true,true,&errorString);
        if (res>=0)
        {
            std::string xml;
            const char* _xml=nullptr;
            if (res==2)
            {
                xml=luaWrap_lua_tostring(L,2);
                _xml=xml.c_str();
            }
            int res=checkOneGeneralInputArgument(L,3,lua_arg_string,0,true,true,&errorString);
            if (res!=2)
            { // Modal dlg
                int various[4];
                char* outText=simOpenTextEditor_internal(initText.c_str(),_xml,various);
                if (outText!=nullptr)
                {
                    luaWrap_lua_pushstring(L,outText);
                    delete[] outText;
                    pushIntTableOntoStack(L,2,various+0);
                    pushIntTableOntoStack(L,2,various+2);
                    LUA_END(3);
                }
            }
            else
            { // non-modal dlg
                int handle=-1;
                CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
                if (it!=nullptr)
                {
                    std::string callbackFunction(luaWrap_lua_tostring(L,3));
#ifdef SIM_WITH_GUI
                    if (App::mainWindow!=nullptr)
                        handle=App::mainWindow->codeEditorContainer->openTextEditor(initText.c_str(),xml.c_str(),callbackFunction.c_str(),it->getScriptHandle(),it->isSimulationScript());
#endif
                }
                luaWrap_lua_pushinteger(L,handle);
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simCloseTextEditor(luaWrap_lua_State* L)
{ // DEPRECATED since V3.6.0
    TRACE_LUA_API;
    LUA_START("sim.closeTextEditor");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_bool,0))
    {
        int h=luaToInt(L,1);
        bool ignoreCb=luaToBool(L,2);
        int res=0;
        std::string txt;
        std::string cb;
        int posAndSize[4];
#ifdef SIM_WITH_GUI
        if (App::mainWindow!=nullptr)
        {
            if (App::mainWindow->codeEditorContainer->close(h,posAndSize,&txt,&cb))
                res=1;
        }
#endif
        if ( (res>0)&&(!ignoreCb) )
        {   // We call the callback directly from here:
            CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
            if (it!=nullptr)
            {
                CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
                stack->pushStringOntoStack(txt.c_str(),txt.size());
                stack->pushInt32ArrayOntoStack(posAndSize+0,2);
                stack->pushInt32ArrayOntoStack(posAndSize+2,2);
                it->callCustomScriptFunction(cb.c_str(),stack);
                App::worldContainer->interfaceStackContainer->destroyStack(stack);
            }
        }
        luaWrap_lua_pushinteger(L,res);
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simHandleVarious(luaWrap_lua_State* L)
{ // DEPRECATED since V3.4.0
    TRACE_LUA_API;
    LUA_START("simHandleVarious");

    int retVal=simHandleVarious_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetMpConfigForTipPose(luaWrap_lua_State* L)
{ // DEPRECATED since V3.3.0
    TRACE_LUA_API;
    LUA_START("simGetMpConfigForTipPose");
    errorString="not supported anymore.";
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,-1);
    LUA_END(1);
}


int _simResetPath(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simResetPath");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simResetPath_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simHandlePath(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simHandlePath");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simHandlePath_internal(luaToInt(L,1),luaToDouble(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simResetJoint(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simResetJoint");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simResetJoint_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simHandleJoint(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simHandleJoint");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simHandleJoint_internal(luaToInt(L,1),luaToDouble(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simResetTracing(luaWrap_lua_State* L)
{ // deprecated
    TRACE_LUA_API;
    LUA_START("simResetTracing");

    warningString=SIM_ERROR_FUNCTION_DEPRECATED_AND_HAS_NO_EFFECT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simHandleTracing(luaWrap_lua_State* L)
{ // deprecated
    TRACE_LUA_API;
    LUA_START("simHandleTracing");

    warningString=SIM_ERROR_FUNCTION_DEPRECATED_AND_HAS_NO_EFFECT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

static int _nextMemHandle_old=0;
static std::vector<int> _memHandles_old;
static std::vector<void*> _memBuffers_old;

typedef struct{
    double accel;
    double vdl;
    double currentPos;
    double lastTime;
    double maxVelocity;
    double currentVel;
    C7Vector startTr;
    C7Vector targetTr;
    int objID;
    CSceneObject* object;
    int relativeToObjID;
    CSceneObject* relToObject;
    unsigned char posAndOrient;
} simMoveToPosData_old;

typedef struct{
    double lastTime;
    bool sameTimeFinish;
    int maxVirtualDistIndex;
    double maxVelocity;
    double accel;
    std::vector<int> jointHandles;
    std::vector<double> jointCurrentVirtualPositions;
    std::vector<double> jointCurrentVirtualVelocities;
    std::vector<double> jointStartPositions;
    std::vector<double> jointTargetPositions;
    std::vector<double> jointVirtualDistances;
    std::vector<double> jointMaxVelocities;
    std::vector<double> jointAccels;
} simMoveToJointPosData_old;

int _sim_moveToPos_1(luaWrap_lua_State* L)
{ // for backward compatibility with simMoveToPosition on old threaded scripts
    TRACE_LUA_API;
    LUA_START("sim._moveToPos_1");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    { // Those are the arguments that are always required! (the rest can be ignored or set to nil!
        int objID=luaToInt(L,1);
        int relativeToObjID=luaToInt(L,2);
        double posTarget[3];
        double eulerTarget[3];
        double maxVelocity;
        CSceneObject* object=App::currentWorld->sceneObjects->getObjectFromHandle(objID);
        CSceneObject* relToObject=nullptr;
        double accel=0.0; // means infinite accel!! (default value)
        double angleToLinearCoeff=0.1/(90.0*degToRad); // (default value)
        int distCalcMethod=sim_distcalcmethod_dl_if_nonzero; // (default value)
        bool foundError=false;
        if ((!foundError)&&(object==nullptr))
        {
            errorString=SIM_ERROR_OBJECT_INEXISTANT;
            foundError=true;
        }
        if ((!foundError)&&(relativeToObjID==sim_handle_parent))
        {
            relativeToObjID=-1;
            CSceneObject* parent=object->getParent();
            if (parent!=nullptr)
                relativeToObjID=parent->getObjectHandle();
        }
        if ((!foundError)&&(relativeToObjID!=-1))
        {
            relToObject=App::currentWorld->sceneObjects->getObjectFromHandle(relativeToObjID);
            if (relToObject==nullptr)
            { // error, object doesn't exist!
                errorString=SIM_ERROR_OBJECT_INEXISTANT;
                foundError=true;
            }
        }

        // Now check the optional arguments:
        int res;
        unsigned char posAndOrient=0;
        if (!foundError) // position argument:
        {
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,3,true,true,&errorString);
            if (res==2)
            { // get the data
                getDoublesFromTable(L,3,3,posTarget);
                posAndOrient|=1;
            }
            foundError=(res==-1);
        }
        if (!foundError) // orientation argument:
        {
            res=checkOneGeneralInputArgument(L,4,lua_arg_number,3,true,true,&errorString);
            if (res==2)
            { // get the data
                getDoublesFromTable(L,4,3,eulerTarget);
                posAndOrient|=2;
            }
            foundError=(res==-1);
        }
        if ((!foundError)&&(posAndOrient==0))
        {
            foundError=true;
            errorString="target position and/or target orientation has to be specified.";
        }
        if (!foundError) // target velocity argument:
        {
            res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,false,false,&errorString);
            if (res==2)
            { // get the data
                maxVelocity=luaToDouble(L,5);
            }
            else
                foundError=true;
        }
        if (!foundError) // Accel argument:
        {
            res=checkOneGeneralInputArgument(L,6,lua_arg_number,0,true,true,&errorString);
            if (res==2)
            { // get the data
                accel=fabs(luaToDouble(L,6));
            }
            foundError=(res==-1);
        }
        if (!foundError) // distance method:
        {
            res=checkOneGeneralInputArgument(L,7,lua_arg_number,2,true,true,&errorString);
            if (res==2)
            { // get the data
                double tmpF[2];
                getDoublesFromTable(L,7,2,tmpF);
                angleToLinearCoeff=tmpF[1];
                getIntsFromTable(L,7,1,&distCalcMethod);
            }
            foundError=(res==-1);
        }
        if (!foundError)
        { // do the job here!
            C7Vector startTr(object->getCumulativeTransformation());
            C7Vector relTr;
            relTr.setIdentity();
            if (relToObject!=nullptr)
                relTr=relToObject->getFullCumulativeTransformation();
            startTr=relTr.getInverse()*startTr;

            C7Vector targetTr(startTr);
            if (posAndOrient&1)
                targetTr.X.setData(posTarget);
            if (posAndOrient&2)
                targetTr.Q.setEulerAngles(eulerTarget[0],eulerTarget[1],eulerTarget[2]);
            double currentVel=0.0;
            CVThreadData* threadData=CThreadPool_old::getCurrentThreadData();

            double dl=(targetTr.X-startTr.X).getLength();
            double da=targetTr.Q.getAngleBetweenQuaternions(startTr.Q)*angleToLinearCoeff;
            double vdl=dl;
            if (distCalcMethod==sim_distcalcmethod_dl)
                vdl=dl;
            if (distCalcMethod==sim_distcalcmethod_dac)
                vdl=da;
            if (distCalcMethod==sim_distcalcmethod_max_dl_dac)
                vdl=std::max<double>(dl,da);
            if (distCalcMethod==sim_distcalcmethod_dl_and_dac)
                vdl=dl+da;
            if (distCalcMethod==sim_distcalcmethod_sqrt_dl2_and_dac2)
                vdl=sqrt(dl*dl+da*da);
            if (distCalcMethod==sim_distcalcmethod_dl_if_nonzero)
            {
                vdl=dl;
                if (dl<0.00005) // Was dl==0.0 before (tolerance problem). Changed on 1/4/2011
                    vdl=da;
            }
            if (distCalcMethod==sim_distcalcmethod_dac_if_nonzero)
            {
                vdl=da;
                if (da<0.01*degToRad) // Was da==0.0 before (tolerance problem). Changed on 1/4/2011
                    vdl=dl;
            }
            // vld is the totalvirtual distance
            double currentPos=0.0;
            bool movementFinished=false;

            if (vdl==0.0)
            { // if the path length is 0 (the two positions might still be not-coincident, depending on the calculation method!)
                if (App::currentWorld->sceneObjects->getObjectFromHandle(objID)==object) // make sure the object is still valid (running in a thread)
                {
                    if (relToObject==nullptr)
                    { // absolute
                        C7Vector parentInv(object->getFullParentCumulativeTransformation().getInverse());
                        object->setLocalTransformation(parentInv*targetTr);
                    }
                    else
                    { // relative to a specific object (2009/11/17)
                        if (App::currentWorld->sceneObjects->getObjectFromHandle(relativeToObjID)==relToObject) // make sure the object is still valid (running in a thread)
                        { // ok
                            C7Vector relToTr(relToObject->getFullCumulativeTransformation());
                            targetTr=relToTr*targetTr;
                            C7Vector parentInv(object->getFullParentCumulativeTransformation().getInverse());
                            object->setLocalTransformation(parentInv*targetTr);
                        }
                    }
                }
                movementFinished=true;
            }

            if (movementFinished)
                luaWrap_lua_pushinteger(L,-1);
            else
            {
                _memHandles_old.push_back(_nextMemHandle_old);
                simMoveToPosData_old* mem=new simMoveToPosData_old();
                mem->accel=accel;
                mem->vdl=vdl;
                mem->currentPos=currentPos;
                mem->lastTime=App::currentWorld->simulation->getSimulationTime();
                mem->maxVelocity=maxVelocity;
                mem->currentVel=currentVel;
                mem->startTr=startTr;
                mem->targetTr=targetTr;
                mem->objID=objID;
                mem->object=object;
                mem->relativeToObjID=relativeToObjID;
                mem->relToObject=relToObject;
                mem->posAndOrient=posAndOrient;

                _memBuffers_old.push_back(mem);
                luaWrap_lua_pushinteger(L,_nextMemHandle_old);
                _nextMemHandle_old++;
            }
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED();
    LUA_END(0);
}

int _sim_moveToPos_2(luaWrap_lua_State* L)
{ // for backward compatibility with simMoveToPosition on old threaded scripts
    TRACE_LUA_API;
    LUA_START("sim._moveToPos_2");
    if (checkInputArguments(L,&errorString,lua_arg_integer,0))
    {
        int h=luaToInt(L,1);
        simMoveToPosData_old* mem=nullptr;
        for (size_t i=0;i<_memHandles_old.size();i++)
        {
            if (_memHandles_old[i]==h)
            {
                mem=(simMoveToPosData_old*)_memBuffers_old[i];
                break;
            }
        }
        if (mem!=nullptr)
        {
            bool err=false;
            bool movementFinished=false;
            double currentTime=App::currentWorld->simulation->getSimulationTime()+App::currentWorld->simulation->getTimeStep();
            double dt=currentTime-mem->lastTime;
            mem->lastTime=currentTime;

            if (mem->accel==0.0)
            { // Means infinite acceleration
                double timeNeeded=(mem->vdl-mem->currentPos)/mem->maxVelocity;
                mem->currentVel=mem->maxVelocity;
                if (timeNeeded>dt)
                {
                    mem->currentPos+=dt*mem->maxVelocity;
                    dt=0.0; // this is what is left
                }
                else
                {
                    mem->currentPos=mem->vdl;
                    if (timeNeeded>=0.0)
                        dt-=timeNeeded;
                }
            }
            else
            {
                double p=mem->currentPos;
                double v=mem->currentVel;
                double t=dt;
                CLinMotionRoutines::getNextValues(p,v,mem->maxVelocity,mem->accel,0.0,mem->vdl,0.0,0.0,t);
                mem->currentPos=double(p);
                mem->currentVel=double(v);
                dt=double(t);
            }

            // Now check if we are within tolerances:
            if (fabs(mem->currentPos-mem->vdl)<=0.00001)//tol[0])
                movementFinished=true;

            // Set the new configuration of the object:
            double ll=mem->currentPos/mem->vdl;
            if (ll>1.0)
                ll=1.0;
            C7Vector newAbs;
            newAbs.buildInterpolation(mem->startTr,mem->targetTr,ll);
            if (App::currentWorld->sceneObjects->getObjectFromHandle(mem->objID)==mem->object) // make sure the object is still valid (running in a thread)
            {
                if ( (mem->relToObject!=nullptr)&&(App::currentWorld->sceneObjects->getObjectFromHandle(mem->relativeToObjID)!=mem->relToObject) )
                    movementFinished=true; // the object was destroyed during execution of the command!
                else
                {
                    C7Vector parentInv(mem->object->getFullParentCumulativeTransformation().getInverse());
                    C7Vector currAbs(mem->object->getCumulativeTransformation());
                    C7Vector relToTr;
                    relToTr.setIdentity();
                    if (mem->relToObject!=nullptr)
                        relToTr=mem->relToObject->getFullCumulativeTransformation();
                    currAbs=relToTr.getInverse()*currAbs;
                    if ((mem->posAndOrient&1)==0)
                        newAbs.X=currAbs.X;
                    if ((mem->posAndOrient&2)==0)
                        newAbs.Q=currAbs.Q;
                    newAbs=relToTr*newAbs;
                    mem->object->setLocalTransformation(parentInv*newAbs);
                }
            }
            else
                movementFinished=true; // the object was destroyed during execution of the command!

            if (!movementFinished)
            {
                luaWrap_lua_pushinteger(L,0); // mov. not yet finished
                LUA_END(1);
            }
            if (!err)
            {
                luaWrap_lua_pushinteger(L,1); // mov. finished
                luaWrap_lua_pushnumber(L,dt); // success (deltaTime left)
                LUA_END(2);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED();
    LUA_END(0);
}

int _sim_del(luaWrap_lua_State* L)
{ // for backward compatibility with simMoveToPosition on old threaded scripts
    TRACE_LUA_API;
    LUA_START("sim._del");
    if (checkInputArguments(L,&errorString,lua_arg_integer,0))
    {
        int h=luaToInt(L,1);
        for (size_t i=0;i<_memHandles_old.size();i++)
        {
            if (_memHandles_old[i]==h)
            {
                delete _memBuffers_old[i];
                _memHandles_old.erase(_memHandles_old.begin()+i);
                _memBuffers_old.erase(_memBuffers_old.begin()+i);
                break;
            }
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED();
    LUA_END(0);
}

int _sim_moveToJointPos_1(luaWrap_lua_State* L)
{ // for backward compatibility with simMoveToJointPositions on old threaded scripts
    TRACE_LUA_API;
    LUA_START("sim._moveToJointPos_1");
    if (!( (!luaWrap_lua_istable(L,1))||(!luaWrap_lua_istable(L,2))||(luaWrap_lua_rawlen(L,1)>luaWrap_lua_rawlen(L,2))||(luaWrap_lua_rawlen(L,1)==0) ))
    { // Ok we have 2 tables with same sizes.
        int tableLen=(int)luaWrap_lua_rawlen(L,1);
        bool sameTimeFinish=true;
        double maxVelocity=0.0;
        double accel=0.0; // means infinite accel!! (default value)
        bool accelTablePresent=false;
        double angleToLinearCoeff=1.0;
        bool foundError=false;
        // Now check the other arguments:
        int res;
        if (luaWrap_lua_istable(L,3))
            sameTimeFinish=false; // we do not finish at the same time!
        if (!foundError) // velocity or velocities argument (not optional!):
        {
            if (sameTimeFinish)
            {
                res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,false,false,&errorString);
                if (res==2)
                    maxVelocity=luaToDouble(L,3);
                else
                    foundError=true;
            }
            else
            {
                res=checkOneGeneralInputArgument(L,3,lua_arg_number,tableLen,false,false,&errorString);
                if (res!=2)
                    foundError=true;
            }
        }
        if (!foundError) // Accel argument:
        {
            if (sameTimeFinish)
            {
                res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,true,&errorString);
                if (res==2)
                { // get the data
                    accel=fabs(luaToDouble(L,4));
                }
                foundError=(res==-1);
            }
            else
            {
                res=checkOneGeneralInputArgument(L,4,lua_arg_number,tableLen,true,true,&errorString);
                if (res==2)
                    accelTablePresent=true;
                foundError=(res==-1);
            }
        }
        if (!foundError) // angleToLinearCoeff argument:
        {
            if (sameTimeFinish)
            {
                res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,true,true,&errorString);
                if (res==2)
                { // get the data
                    angleToLinearCoeff=fabs(luaToDouble(L,5));
                }
                foundError=(res==-1);
            }
            else
                angleToLinearCoeff=1.0; // no conversion!
        }
        if (!foundError)
        { // do the job here!
            std::vector<int> jointHandles;
            jointHandles.resize(tableLen);
            std::vector<double> jointCurrentVirtualPositions;
            jointCurrentVirtualPositions.resize(tableLen);
            std::vector<double> jointCurrentVirtualVelocities;
            jointCurrentVirtualVelocities.resize(tableLen);
            std::vector<double> jointStartPositions;
            jointStartPositions.resize(tableLen);
            std::vector<double> jointTargetPositions;
            jointTargetPositions.resize(tableLen);
            std::vector<double> jointVirtualDistances;
            jointVirtualDistances.resize(tableLen);
            std::vector<double> jointMaxVelocities;
            jointMaxVelocities.resize(tableLen);
            std::vector<double> jointAccels;
            jointAccels.resize(tableLen);

            getIntsFromTable(L,1,tableLen,&jointHandles[0]);
            getDoublesFromTable(L,2,tableLen,&jointTargetPositions[0]);
            if (!sameTimeFinish)
            {
                getDoublesFromTable(L,3,tableLen,&jointMaxVelocities[0]);
                for (int i=0;i<tableLen;i++)
                    jointMaxVelocities[i]=fabs(jointMaxVelocities[i]);
                if (accelTablePresent)
                {
                    getDoublesFromTable(L,4,tableLen,&jointAccels[0]);
                    for (int i=0;i<tableLen;i++)
                        jointAccels[i]=fabs(jointAccels[i]);
                }
            }
            double maxVirtualDist=0.0;
            int maxVirtualDistIndex=0;
            for (int i=0;i<tableLen;i++)
            {
                jointCurrentVirtualPositions[i]=0.0;
                jointCurrentVirtualVelocities[i]=0.0;
                if (sameTimeFinish)
                    jointMaxVelocities[i]=maxVelocity;
                if (!accelTablePresent)
                    jointAccels[i]=accel;

                CJoint* it=App::currentWorld->sceneObjects->getJointFromHandle(jointHandles[i]);
                if ((it!=nullptr)&&(it->getJointType()!=sim_joint_spherical_subtype))
                { // make sure target is within allowed range, and check the maximum virtual distance:
                    jointStartPositions[i]=it->getPosition();
                    double minP=it->getPositionMin();
                    double maxP=minP+it->getPositionRange();
                    if (it->getIsCyclic())
                    {
                        double da=tt::getAngleMinusAlpha(jointTargetPositions[i],jointStartPositions[i]);
                        jointTargetPositions[i]=jointStartPositions[i]+da;
                    }
                    else
                    {
                        if (minP>jointTargetPositions[i])
                            jointTargetPositions[i]=minP;
                        if (maxP<jointTargetPositions[i])
                            jointTargetPositions[i]=maxP;
                    }
                    double d=fabs(jointTargetPositions[i]-jointStartPositions[i]);
                    if (it->getJointType()==sim_joint_revolute_subtype)
                        d*=angleToLinearCoeff;
                    jointVirtualDistances[i]=d;
                    if (d>maxVirtualDist)
                    {
                        maxVirtualDist=d;
                        maxVirtualDistIndex=i;
                    }
                }
                else
                {
                    // Following are default values in case the joint doesn't exist or is spherical:
                    jointStartPositions[i]=0.0;
                    jointTargetPositions[i]=0.0;
                    jointVirtualDistances[i]=0.0;
                }
            }
            double lastTime=App::currentWorld->simulation->getSimulationTime();
            bool movementFinished=false;
            double dt=App::currentWorld->simulation->getTimeStep(); // this is the time left if we leave here

            if (maxVirtualDist==0.0)
                luaWrap_lua_pushinteger(L,-1);
            else
            {
                _memHandles_old.push_back(_nextMemHandle_old);
                simMoveToJointPosData_old* mem=new simMoveToJointPosData_old();
                mem->lastTime=lastTime;
                mem->sameTimeFinish=sameTimeFinish;
                mem->maxVirtualDistIndex=maxVirtualDistIndex;
                mem->maxVelocity=maxVelocity;
                mem->accel=accel;
                mem->jointHandles.assign(jointHandles.begin(),jointHandles.end());
                mem->jointCurrentVirtualPositions.assign(jointCurrentVirtualPositions.begin(),jointCurrentVirtualPositions.end());
                mem->jointCurrentVirtualVelocities.assign(jointCurrentVirtualVelocities.begin(),jointCurrentVirtualVelocities.end());
                mem->jointStartPositions.assign(jointStartPositions.begin(),jointStartPositions.end());
                mem->jointTargetPositions.assign(jointTargetPositions.begin(),jointTargetPositions.end());
                mem->jointVirtualDistances.assign(jointVirtualDistances.begin(),jointVirtualDistances.end());
                mem->jointMaxVelocities.assign(jointMaxVelocities.begin(),jointMaxVelocities.end());
                mem->jointAccels.assign(jointAccels.begin(),jointAccels.end());



                _memBuffers_old.push_back(mem);
                luaWrap_lua_pushinteger(L,_nextMemHandle_old);
                _nextMemHandle_old++;
            }
            LUA_END(1);

        }
    }
    else
        errorString="one of the function's argument type is not correct or table sizes are invalid or do not match";

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _sim_moveToJointPos_2(luaWrap_lua_State* L)
{ // for backward compatibility with simMoveToJointPositions on old threaded scripts
    TRACE_LUA_API;
    LUA_START("sim._moveToJointPos_2");

    if (checkInputArguments(L,&errorString,lua_arg_integer,0))
    {
        int h=luaToInt(L,1);
        simMoveToJointPosData_old* mem=nullptr;
        for (size_t i=0;i<_memHandles_old.size();i++)
        {
            if (_memHandles_old[i]==h)
            {
                mem=(simMoveToJointPosData_old*)_memBuffers_old[i];
                break;
            }
        }
        if (mem!=nullptr)
        {
            int tableLen=int(mem->jointHandles.size());
            bool err=false;
            bool movementFinished=false;
            double currentTime=App::currentWorld->simulation->getSimulationTime()+App::currentWorld->simulation->getTimeStep();
            double dt=currentTime-mem->lastTime;
            double minTimeLeft=dt;
            mem->lastTime=currentTime;
            if (mem->sameTimeFinish)
            {
                double timeLeftLocal=dt;
                // 1. handle the joint with longest distance first:
                // Does the main joint still exist?
                if (App::currentWorld->sceneObjects->getJointFromHandle(mem->jointHandles[mem->maxVirtualDistIndex])!=nullptr)
                {
                    if (mem->accel==0.0)
                    { // means infinite accel
                        double timeNeeded=(mem->jointVirtualDistances[mem->maxVirtualDistIndex]-mem->jointCurrentVirtualPositions[mem->maxVirtualDistIndex])/mem->maxVelocity;
                        mem->jointCurrentVirtualVelocities[mem->maxVirtualDistIndex]=mem->maxVelocity;
                        if (timeNeeded>timeLeftLocal)
                        {
                            mem->jointCurrentVirtualPositions[mem->maxVirtualDistIndex]+=timeLeftLocal*mem->maxVelocity;
                            timeLeftLocal=0.0; // this is what is left
                        }
                        else
                        {
                            mem->jointCurrentVirtualPositions[mem->maxVirtualDistIndex]=mem->jointVirtualDistances[mem->maxVirtualDistIndex];
                            if (timeNeeded>=0.0)
                                timeLeftLocal-=timeNeeded;
                        }
                    }
                    else
                    {
                        double p=mem->jointCurrentVirtualPositions[mem->maxVirtualDistIndex];
                        double v=mem->jointCurrentVirtualVelocities[mem->maxVirtualDistIndex];
                        double t=timeLeftLocal;
                        CLinMotionRoutines::getNextValues(p,v,mem->maxVelocity,mem->accel,0.0,mem->jointVirtualDistances[mem->maxVirtualDistIndex],0.0,0.0,t);
                        mem->jointCurrentVirtualPositions[mem->maxVirtualDistIndex]=double(p);
                        mem->jointCurrentVirtualVelocities[mem->maxVirtualDistIndex]=double(v);
                        timeLeftLocal=double(t);
                    }
                    minTimeLeft=timeLeftLocal;
                    // 2. We adjust the other joints accordingly:
                    double f=1;
                    if (mem->jointVirtualDistances[mem->maxVirtualDistIndex]!=0.0)
                        f=mem->jointCurrentVirtualPositions[mem->maxVirtualDistIndex]/mem->jointVirtualDistances[mem->maxVirtualDistIndex];
                    for (int i=0;i<tableLen;i++)
                    {
                        if (i!=mem->maxVirtualDistIndex)
                            mem->jointCurrentVirtualPositions[i]=mem->jointVirtualDistances[i]*f;
                    }
                    // 3. Check if within tolerance:
                    if (fabs(mem->jointCurrentVirtualPositions[mem->maxVirtualDistIndex]-mem->jointVirtualDistances[mem->maxVirtualDistIndex])<=0.00001)
                        movementFinished=true;
                }
                else
                { // the main joint was removed. End here!
                    movementFinished=true;
                }
            }
            else
            {
                bool withinTolerance=true;
                for (int i=0;i<tableLen;i++)
                {
                    if (App::currentWorld->sceneObjects->getJointFromHandle(mem->jointHandles[i])!=nullptr)
                    {
                        // Check if within tolerance (before):
                        if (fabs(mem->jointCurrentVirtualPositions[i]-mem->jointVirtualDistances[i])>0.00001)
                        {
                            double timeLeftLocal=dt;
                            if (mem->jointAccels[i]==0.0)
                            { // means infinite accel
                                double timeNeeded=(mem->jointVirtualDistances[i]-mem->jointCurrentVirtualPositions[i])/mem->jointMaxVelocities[i];
                                mem->jointCurrentVirtualVelocities[i]=mem->jointMaxVelocities[i];
                                if (timeNeeded>timeLeftLocal)
                                {
                                    mem->jointCurrentVirtualPositions[i]+=timeLeftLocal*mem->jointMaxVelocities[i];
                                    timeLeftLocal=0.0; // this is what is left
                                }
                                else
                                {
                                    mem->jointCurrentVirtualPositions[i]=mem->jointVirtualDistances[i];
                                    if (timeNeeded>=0.0)
                                        timeLeftLocal-=timeNeeded;
                                }
                            }
                            else
                            {
                                double p=mem->jointCurrentVirtualPositions[i];
                                double v=mem->jointCurrentVirtualVelocities[i];
                                double t=timeLeftLocal;
                                CLinMotionRoutines::getNextValues(p,v,mem->jointMaxVelocities[i],mem->jointAccels[i],0.0,mem->jointVirtualDistances[i],0.0,0.0,t);
                                mem->jointCurrentVirtualPositions[i]=double(p);
                                mem->jointCurrentVirtualVelocities[i]=double(v);
                                timeLeftLocal=double(t);
                            }
                            if (timeLeftLocal<minTimeLeft)
                                minTimeLeft=timeLeftLocal;
                            // Check if within tolerance (after):
                            if (fabs(mem->jointCurrentVirtualPositions[i]-mem->jointVirtualDistances[i])>0.00001)
                                withinTolerance=false;
                        }
                    }
                }
                if (withinTolerance)
                    movementFinished=true;
            }
            dt=minTimeLeft;

            // We set all joint positions:
            for (int i=0;i<tableLen;i++)
            {
                CJoint* joint=App::currentWorld->sceneObjects->getJointFromHandle(mem->jointHandles[i]);
                if ( (joint!=nullptr)&&(joint->getJointType()!=sim_joint_spherical_subtype)&&(mem->jointVirtualDistances[i]!=0.0) )
                {
                    joint->setTargetPosition(mem->jointStartPositions[i]+(mem->jointTargetPositions[i]-mem->jointStartPositions[i])*mem->jointCurrentVirtualPositions[i]/mem->jointVirtualDistances[i]);
                    if (joint->getJointMode()==sim_jointmode_kinematic)
                        joint->setPosition(mem->jointStartPositions[i]+(mem->jointTargetPositions[i]-mem->jointStartPositions[i])*mem->jointCurrentVirtualPositions[i]/mem->jointVirtualDistances[i]);
                }
            }

            if (!movementFinished)
            {
                luaWrap_lua_pushinteger(L,0); // mov. not yet finished
                LUA_END(1);
            }
            if (!err)
            {
                luaWrap_lua_pushinteger(L,1); // mov. finished
                luaWrap_lua_pushnumber(L,dt); // success (deltaTime left)
                LUA_END(2);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetInstanceIndex(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("");

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,0);
    LUA_END(1);
}

int _simGetVisibleInstanceIndex(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("");

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,0);
    LUA_END(1);
}

int _simGetSystemTimeInMilliseconds(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simGetSystemTimeInMilliseconds");

    int res=checkOneGeneralInputArgument(L,1,lua_arg_number,0,true,false,&errorString);
    if (res==0)
    {
        luaWrap_lua_pushinteger(L,VDateTime::getTimeInMs());
        LUA_END(1);
    }
    if (res==2)
    {
        int lastTime=luaToInt(L,1);
        luaWrap_lua_pushinteger(L,VDateTime::getTimeDiffInMs(lastTime));
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simLockInterface(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simLockInterface");

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,-1);
    LUA_END(1);
}

int _simJointGetForce(luaWrap_lua_State* L)
{ // DEPRECATED since release 3.1.2
    TRACE_LUA_API;
    LUA_START("simJointGetForce");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double jointF[1];
        if (simGetJointForce_internal(luaToInt(L,1),jointF)>0)
        {
            luaWrap_lua_pushnumber(L,jointF[0]);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simCopyPasteSelectedObjects(luaWrap_lua_State* L)
{ // DEPRECATED since release 3.1.3
    TRACE_LUA_API;
    LUA_START("simCopyPasteSelectedObjects");

    int retVal=simCopyPasteSelectedObjects_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simFindIkPath(luaWrap_lua_State* L)
{ // DEPRECATED since 3.3.0
    TRACE_LUA_API;
    LUA_START("simFindIkPath");
    errorString="not supported anymore.";
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetPathPlanningHandle(luaWrap_lua_State* L)
{ // DEPRECATED since 3.3.0
    TRACE_LUA_API;
    LUA_START("simGetPathPlanningHandle");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string name(luaWrap_lua_tostring(L,1));
        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
        retVal=simGetPathPlanningHandle_internal(name.c_str());
        setCurrentScriptInfo_cSide(-1,-1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSearchPath(luaWrap_lua_State* L)
{ // DEPRECATED since 3.3.0
    TRACE_LUA_API;
    LUA_START("simSearchPath");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int pathPlanningObjectHandle=luaToInt(L,1);
        double maximumSearchTime=tt::getLimitedFloat(0.001,36000.0,luaToDouble(L,2));
        double subDt=0.05; // 50 ms at a time (default)
        bool foundError=false;
        // Now check the optional argument:
        int res;
        if (!foundError) // sub-dt argument:
        {
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,true,&errorString);
            if (res==2)
            { // get the data
                subDt=tt::getLimitedFloat(0.001,std::min<double>(1.0,maximumSearchTime),luaToDouble(L,3));
            }
            foundError=(res==-1);
        }
        if (!foundError)
        {
            CPathPlanningTask* it=App::currentWorld->pathPlanning->getObject(pathPlanningObjectHandle);
            if (it==nullptr)
                errorString=SIM_ERROR_PATH_PLANNING_OBJECT_INEXISTANT;
            else
            {
                retVal=0;
                //if (VThread::isCurrentThreadTheMainSimulationThread())
                { // non-threaded
                    if (it->performSearch(false,maximumSearchTime))
                        retVal=1;
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simInitializePathSearch(luaWrap_lua_State* L)
{ // DEPRECATED since 3.3.0
    TRACE_LUA_API;
    LUA_START("simInitializePathSearch");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int pathPlanningObjectHandle=luaToInt(L,1);
        double maximumSearchTime=luaToDouble(L,2);
        double searchTimeStep=luaToDouble(L,3);
        retVal=simInitializePathSearch_internal(pathPlanningObjectHandle,maximumSearchTime,searchTimeStep);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simPerformPathSearchStep(luaWrap_lua_State* L)
{ // DEPRECATED since 3.3.0
    TRACE_LUA_API;
    LUA_START("simPerformPathSearchStep");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_bool,0))
    {
        int temporaryPathSearchObjectHandle=luaToInt(L,1);
        bool abortSearch=luaToBool(L,2);
        retVal=simPerformPathSearchStep_internal(temporaryPathSearchObjectHandle,abortSearch);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simEnableWorkThreads(luaWrap_lua_State* L)
{ // DEPRECATED since 3/4/2016
    TRACE_LUA_API;
    LUA_START("simEnableWorkThreads");
    luaWrap_lua_pushinteger(L,0);
    LUA_END(1);
}

int _simWaitForWorkThreads(luaWrap_lua_State* L)
{ // DEPRECATED since 3/4/2016
    TRACE_LUA_API;
    LUA_START("simWaitForWorkThreads");
    LUA_END(0);
}

int _simGetInvertedMatrix(luaWrap_lua_State* L)
{ // DEPRECATED since 10/5/2016
    TRACE_LUA_API;
    LUA_START("simGetInvertedMatrix");

    if (checkInputArguments(L,&errorString,lua_arg_number,12))
    {
        double arr[12];
        getDoublesFromTable(L,1,12,arr);
        simInvertMatrix_internal(arr);
        pushDoubleTableOntoStack(L,12,arr);
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simAddSceneCustomData(luaWrap_lua_State* L)
{ // DEPRECATED since 26/12/2016
    TRACE_LUA_API;
    LUA_START("simAddSceneCustomData");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
    {
        int headerNumber=luaToInt(L,1);
        size_t dataLength;
        char* data=(char*)luaWrap_lua_tolstring(L,2,&dataLength);
        retVal=simAddSceneCustomData_internal(headerNumber,data,(int)dataLength);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetSceneCustomData(luaWrap_lua_State* L)
{ // DEPRECATED since 26/12/2016
    TRACE_LUA_API;
    LUA_START("simGetSceneCustomData");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int headerNumber=luaToInt(L,1);
        int dataLength=simGetSceneCustomDataLength_internal(headerNumber);
        if (dataLength>=0)
        {
            char* data=new char[dataLength];
            int retVal=simGetSceneCustomData_internal(headerNumber,data);
            if (retVal==-1)
                delete[] data;
            else
            {
                luaWrap_lua_pushlstring(L,data,dataLength);
                delete[] data;
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}


int _simAddObjectCustomData(luaWrap_lua_State* L)
{ // DEPRECATED since 26/12/2016
    TRACE_LUA_API;
    LUA_START("simAddObjectCustomData");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_string,0))
    {
        int objectHandle=luaToInt(L,1);
        int headerNumber=luaToInt(L,2);
        size_t dataLength;
        char* data=(char*)luaWrap_lua_tolstring(L,3,&dataLength);
        retVal=simAddObjectCustomData_internal(objectHandle,headerNumber,data,(int)dataLength);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}


int _simGetObjectCustomData(luaWrap_lua_State* L)
{ // DEPRECATED since 26/12/2016
    TRACE_LUA_API;
    LUA_START("simGetObjectCustomData");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int objectHandle=luaToInt(L,1);
        int headerNumber=luaToInt(L,2);
        int dataLength=simGetObjectCustomDataLength_internal(objectHandle,headerNumber);
        if (dataLength>=0)
        {
            char* data=new char[dataLength];
            int retVal=simGetObjectCustomData_internal(objectHandle,headerNumber,data);
            if (retVal==-1)
                delete[] data;
            else
            {
                luaWrap_lua_pushlstring(L,data,dataLength);
                delete[] data;
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetUIHandle(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simGetUIHandle");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string name(luaWrap_lua_tostring(L,1));
        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
        retVal=simGetUIHandle_internal(name.c_str());
        setCurrentScriptInfo_cSide(-1,-1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetUIProperty(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simGetUIProperty");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetUIProperty_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetUIEventButton(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simGetUIEventButton");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int auxVals[2];
        retVal=simGetUIEventButton_internal(luaToInt(L,1),auxVals);
        luaWrap_lua_pushinteger(L,retVal);
        if (retVal==-1)
        {
            LUA_END(1);
        }
        pushIntTableOntoStack(L,2,auxVals);
        LUA_END(2);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetUIProperty(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simSetUIProperty");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetUIProperty_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetUIButtonSize(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simGetUIButtonSize");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int size[2];
        if (simGetUIButtonSize_internal(luaToInt(L,1),luaToInt(L,2),size)==1)
        {
            pushIntTableOntoStack(L,2,size);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetUIButtonProperty(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simGetUIButtonProperty");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simGetUIButtonProperty_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetUIButtonProperty(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simSetUIButtonProperty");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetUIButtonProperty_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetUISlider(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simGetUISlider");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simGetUISlider_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetUISlider(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simSetUISlider");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetUISlider_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetUIButtonLabel(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simSetUIButtonLabel");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        std::string stra;
        std::string strb;
        char* str1=nullptr;
        char* str2=nullptr;
        int res=checkOneGeneralInputArgument(L,3,lua_arg_string,0,false,true,&errorString);
        if (res!=-1)
        {
            if (res==2)
            {
                stra=luaWrap_lua_tostring(L,3);
                str1=(char*)stra.c_str();
            }
            res=checkOneGeneralInputArgument(L,4,lua_arg_string,0,true,true,&errorString);
            if (res!=-1)
            {
                if (res==2)
                {
                    strb=luaWrap_lua_tostring(L,4);
                    str2=(char*)strb.c_str();
                }
                retVal=simSetUIButtonLabel_internal(luaToInt(L,1),luaToInt(L,2),str1,str2);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetUIButtonLabel(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simGetUIButtonLabel");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        char* label=simGetUIButtonLabel_internal(luaToInt(L,1),luaToInt(L,2));
        if (label!=nullptr)
        {
            luaWrap_lua_pushstring(L,label);
            simReleaseBuffer_internal(label);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simCreateUIButtonArray(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simCreateUIButtonArray");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simCreateUIButtonArray_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetUIButtonArrayColor(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simSetUIButtonArrayColor");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,2,lua_arg_number,3))
    {
        int pos[2];
        float col[3];
        getIntsFromTable(L,3,2,pos);
        getFloatsFromTable(L,4,3,col);
        retVal=simSetUIButtonArrayColor_internal(luaToInt(L,1),luaToInt(L,2),pos,col);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simDeleteUIButtonArray(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simDeleteUIButtonArray");
    int retVal=-1;// error

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simDeleteUIButtonArray_internal(luaToInt(L,1),luaToInt(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCreateUI(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simCreateUI");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0,lua_arg_number,2,lua_arg_number,2))
    {
        int clientSize[2];
        int cellSize[2];
        getIntsFromTable(L,3,2,clientSize);
        getIntsFromTable(L,4,2,cellSize);
        int menuAttributes=luaToInt(L,2);
        int b=0;
        for (int i=0;i<8;i++)
        {
            if (menuAttributes&(1<<i))
                b++;
        }
        int* buttonHandles=new int[b];
        menuAttributes=(menuAttributes|sim_ui_menu_systemblock)-sim_ui_menu_systemblock;
        retVal=simCreateUI_internal(luaWrap_lua_tostring(L,1),menuAttributes,clientSize,cellSize,buttonHandles);
        if (retVal!=-1)
        {
            luaWrap_lua_pushinteger(L,retVal);
            pushIntTableOntoStack(L,b,buttonHandles);
            delete[] buttonHandles;
            LUA_END(2);
        }
        delete[] buttonHandles;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCreateUIButton(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simCreateUIButton");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,2,lua_arg_number,2,lua_arg_number,0))
    {
        int pos[2];
        int size[2];
        getIntsFromTable(L,2,2,pos);
        getIntsFromTable(L,3,2,size);
        retVal=simCreateUIButton_internal(luaToInt(L,1),pos,size,luaToInt(L,4));
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSaveUI(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simSaveUI");

    int retVal=-1;// error
    int res=checkOneGeneralInputArgument(L,1,lua_arg_table,1,false,true,&errorString);
    if (res>0)
    {
        int res2=checkOneGeneralInputArgument(L,2,lua_arg_string,0,false,false,&errorString);
        if (res2==2)
        {
            if (res==1)
                retVal=simSaveUI_internal(0,nullptr,luaWrap_lua_tostring(L,2));
            else
            {
                int tl=int(luaWrap_lua_rawlen(L,1));
                int* tble=new int[tl];
                getIntsFromTable(L,1,tl,tble);
                retVal=simSaveUI_internal(tl,tble,luaWrap_lua_tostring(L,2));
                delete[] tble;
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simLoadUI(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simLoadUI");
    LUA_END(0);
}

int _simRemoveUI(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simRemoveUI");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simRemoveUI_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetUIPosition(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simGetUIPosition");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int position[2];
        if (simGetUIPosition_internal(luaToInt(L,1),position)!=-1)
        {
            pushIntTableOntoStack(L,2,position);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetUIPosition(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simSetUIPosition");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,2))
    {
        int position[2];
        getIntsFromTable(L,2,2,position);
        retVal=simSetUIPosition_internal(luaToInt(L,1),position);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetUIButtonColor(luaWrap_lua_State* L)
{ // DEPRECATED since 09/02/2017
    TRACE_LUA_API;
    LUA_START("simSetUIButtonColor");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        float col[9];
        float* acp[3]={col,col+3,col+6};
//      bool failed=true;
        int res=checkOneGeneralInputArgument(L,3,lua_arg_number,3,false,true,&errorString);
        if (res!=-1)
        {
            if (res!=2)
                acp[0]=nullptr;
            res=checkOneGeneralInputArgument(L,4,lua_arg_number,3,true,true,&errorString);
            if (res!=-1)
            {
                if (res!=2)
                    acp[1]=nullptr;
                res=checkOneGeneralInputArgument(L,5,lua_arg_number,3,true,true,&errorString);
                if (res!=-1)
                {
                    if (res!=2)
                        acp[2]=nullptr;
                    for (int i=0;i<3;i++)
                    {
                        if (acp[i]!=nullptr)
                            getFloatsFromTable(L,3+i,3,acp[i]);
                    }
                    retVal=simSetUIButtonColor_internal(luaToInt(L,1),luaToInt(L,2),acp[0],acp[1],acp[2]);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simHandleChildScript(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simHandleChildScript");

    warningString="function is deprecated. Use simHandleChildScripts instead.";
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (!it->checkAndSetWarningAboutSimHandleChildScriptAlreadyIssued_oldCompatibility_7_8_2014())
    {
        std::string title("Compatibility issue with ");
        title+=it->getShortDescriptiveName();
        std::string txt("The command simHandleChildScript is not supported anymore and was replaced ");
        txt+="with sim.handleChildScripts, which operates in a slightly different manner. Make sure to ";
        txt+="adjust this script manually.";
        App::logMsg(sim_verbosity_errors,txt.c_str());
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simHandleSensingChildScripts(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simHandleSensingChildScripts");

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simScaleSelectedObjects(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simScaleSelectedObjects");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_bool,0))
        retVal=simScaleSelectedObjects_internal(luaToDouble(L,1),luaToBool(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simDeleteSelectedObjects(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simDeleteSelectedObjects");

    int retVal=simDeleteSelectedObjects_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simDelegateChildScriptExecution(luaWrap_lua_State* L)
{ // DEPRECATED. can only be called from a script running in a thread!!
    TRACE_LUA_API;
    LUA_START("simDelegateChildScriptExecution");

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetShapeVertex(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simGetShapeVertex");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        double relPos[3];
        retVal=simGetShapeVertex_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3),relPos);
        if (retVal==1)
        {
            luaWrap_lua_pushinteger(L,retVal);
            pushDoubleTableOntoStack(L,3,relPos);
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetShapeTriangle(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simGetShapeTriangle");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int indices[3];
        double normals[9];
        retVal=simGetShapeTriangle_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3),indices,normals);
        if (retVal==1)
        {
            luaWrap_lua_pushinteger(L,retVal);
            pushIntTableOntoStack(L,3,indices);
            pushDoubleTableOntoStack(L,9,normals);
            LUA_END(3);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetMaterialId(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simGetMaterialId");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string matName(luaWrap_lua_tostring(L,1));
        retVal=simGetMaterialId_internal(matName.c_str());
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetShapeMaterial(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simGetShapeMaterial");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int shapeHandle=luaToInt(L,1);
        retVal=simGetShapeMaterial_internal(shapeHandle);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simReleaseScriptRawBuffer(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simReleaseScriptRawBuffer");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        if (handle==sim_handle_self)
            handle=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        if ( (handle!=sim_handle_tree)&&(handle!=sim_handle_chain) )
            retVal=simReleaseScriptRawBuffer_internal(handle,luaToInt(L,2));
        else
        {
            std::vector<int> scriptHandles;
            if (handle==sim_handle_tree)
                getScriptTree_old(L,false,scriptHandles);
            else
                getScriptChain_old(L,false,false,scriptHandles);
            for (int i=0;i<int(scriptHandles.size());i++)
                retVal=simReleaseScriptRawBuffer_internal(scriptHandles[i],sim_handle_all);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectSelectionSize(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simGetObjectSelectionSize");

    int retVal=simGetObjectSelectionSize_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectLastSelection(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("simGetObjectLastSelection");

    int retVal=simGetObjectLastSelection_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetVisionSensorFilter(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("sim.setVisionSensorFilter");
    int retVal=-1; // error

    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetVisionSensorFilter(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("sim.getVisionSensorFilter");

    luaWrap_lua_pushinteger(L,-1);
    LUA_END(1);
}

int _simGetScriptSimulationParameter(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("sim.getScriptSimulationParameter");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
    {
        bool goOn=true;
        int handle=luaToInt(L,1);
        if (handle==sim_handle_self)
        {
            handle=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
            // Since this routine can also be called by customization scripts, check for that here:
            CScriptObject* it=App::worldContainer->getScriptFromHandle(handle);
            if (it->getScriptType()==sim_scripttype_customizationscript)
            {
                handle=it->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_customizationscript);
                it=App::currentWorld->embeddedScriptContainer->getScriptFromObjectAttachedTo(sim_scripttype_childscript,handle);
                if (it!=nullptr)
                    handle=it->getScriptHandle();
                else
                    goOn=false;
            }
        }
        if (goOn)
        {
            bool returnString=false;
            int ret=checkOneGeneralInputArgument(L,3,lua_arg_bool,0,true,false,&errorString);
            if ((ret==0)||(ret==2))
            {
                if (ret==2)
                    returnString=luaToBool(L,3);
                std::string parameterName(luaWrap_lua_tostring(L,2));
                if ( (handle!=sim_handle_tree)&&(handle!=sim_handle_chain)&&(handle!=sim_handle_all) )
                {
                    int l;
                    char* p=simGetScriptSimulationParameter_internal(handle,parameterName.c_str(),&l);
                    if (p!=nullptr)
                    {
                        std::string a;
                        a.assign(p,l);
                        if (returnString)
                            luaWrap_lua_pushlstring(L,a.c_str(),a.length());
                        else
                            pushCorrectTypeOntoLuaStack_old(L,a);
                        simReleaseBuffer_internal(p);
                        LUA_END(1);
                    }
                }
                else
                {
                    std::vector<int> scriptHandles;
                    if (handle==sim_handle_tree)
                        getScriptTree_old(L,false,scriptHandles);
                    if (handle==sim_handle_chain)
                        getScriptChain_old(L,false,false,scriptHandles);
                    if (handle==sim_handle_all)
                    {
                        for (int i=0;i<int(App::currentWorld->embeddedScriptContainer->allScripts.size());i++)
                        {
                            CScriptObject* it=App::currentWorld->embeddedScriptContainer->allScripts[i];
                            int scrType=it->getScriptType();
                            if ((scrType==sim_scripttype_mainscript)||(scrType==sim_scripttype_childscript)) // make sure plugin script etc. are not included!
                                scriptHandles.push_back(it->getScriptHandle());
                        }
                    }
                    std::vector<std::string> retParams;
                    std::vector<int> retHandles;
                    for (int i=0;i<int(scriptHandles.size());i++)
                    {
                        int l;
                        char* p=simGetScriptSimulationParameter_internal(scriptHandles[i],parameterName.c_str(),&l);
                        if (p!=nullptr)
                        {
                            std::string a;
                            a.assign(p,l);
                            simReleaseBuffer_internal(p);
                            retParams.push_back(a);
                            retHandles.push_back(scriptHandles[i]);
                        }
                    }
                    if (retParams.size()!=0)
                    { // now we push two tables onto the stack:
                        CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
                        stack->pushTableOntoStack();
                        for (int i=0;i<int(retParams.size());i++)
                        {
                            stack->pushInt32OntoStack(i+1); // key
                            int t=getCorrectType_old(retParams[i]);
                            if (returnString)
                                t=4; // we force for strings!
                            if (t==0)
                                stack->pushNullOntoStack();
                            if ((t==1)||(t==2))
                                stack->pushBoolOntoStack(t==2);
                            if (t==3)
                            {
                                double v;
                                tt::getValidFloat(retParams[i].c_str(),v);
                                stack->pushFloatOntoStack(v);
                            }
                            if (t==4)
                                stack->pushStringOntoStack(retParams[i].c_str(),0);
                            if (stack->getStackSize()<2)
                                stack->pushNullOntoStack();
                            stack->insertDataIntoStackTable();
                        }
                        CScriptObject::buildOntoInterpreterStack_lua(L,stack,true);
                        App::worldContainer->interfaceStackContainer->destroyStack(stack);
                        pushIntTableOntoStack(L,(int)retHandles.size(),&retHandles[0]);
                        LUA_END(2);
                    }
                }
            }
        }
        else
            errorString=SIM_ERROR_NO_ASSOCIATED_CHILD_SCRIPT_FOUND;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetScriptSimulationParameter(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("sim.setScriptSimulationParameter");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0,lua_arg_string,0))
    {
        bool goOn=true;
        int handle=luaToInt(L,1);
        if (handle==sim_handle_self)
        {
            handle=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
            // Since this routine can also be called by customization scripts, check for that here:
            CScriptObject* it=App::worldContainer->getScriptFromHandle(handle);
            if (it->getScriptType()==sim_scripttype_customizationscript)
            {
                handle=it->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_customizationscript);
                it=App::currentWorld->embeddedScriptContainer->getScriptFromObjectAttachedTo(sim_scripttype_childscript,handle);
                if (it!=nullptr)
                    handle=it->getScriptHandle();
                else
                    goOn=false;
            }
        }
        if (goOn)
        {
            std::string parameterName(luaWrap_lua_tostring(L,2));
            size_t parameterValueLength;
            char* parameterValue=(char*)luaWrap_lua_tolstring(L,3,&parameterValueLength);
            if ( (handle!=sim_handle_tree)&&(handle!=sim_handle_chain) )
            {
                retVal=simSetScriptSimulationParameter_internal(handle,parameterName.c_str(),parameterValue,(int)parameterValueLength);
            }
            else
            {
                std::vector<int> scriptHandles;
                if (handle==sim_handle_tree)
                    getScriptTree_old(L,false,scriptHandles);
                else
                    getScriptChain_old(L,false,false,scriptHandles);
                retVal=0;
                for (size_t i=0;i<scriptHandles.size();i++)
                {
                    if (simSetScriptSimulationParameter_internal(scriptHandles[i],parameterName.c_str(),parameterValue,(int)parameterValueLength)==1)
                        retVal++;
                }
            }
        }
        else
            errorString=SIM_ERROR_NO_ASSOCIATED_CHILD_SCRIPT_FOUND;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simHandleMechanism(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("sim.handleMechanism");
    int retVal=-1; // means error
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetPathTargetNominalVelocity(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("sim.setPathTargetNominalVelocity");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetPathTargetNominalVelocity_internal(luaToInt(L,1),luaToDouble(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetNameSuffix(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("sim.getNameSuffix");

    if (checkInputArguments(L,nullptr,lua_arg_nil,0))
    { // we want the suffix of current script
        luaWrap_lua_pushinteger(L,CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L));
        LUA_END(1);
    }
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    { // we want the suffix of the provided name
        std::string nameWithSuffix(luaWrap_lua_tostring(L,1));
        std::string name(tt::getNameWithoutSuffixNumber(nameWithSuffix.c_str(),true));
        int suffixNumber=tt::getNameSuffixNumber(nameWithSuffix.c_str(),true);
        luaWrap_lua_pushinteger(L,suffixNumber);
        luaWrap_lua_pushstring(L,name.c_str());
        LUA_END(2);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetNameSuffix(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("sim.setNameSuffix");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int nb=luaToInt(L,1);
        CScriptObject::setScriptNameIndexToInterpreterState_lua_old(L,nb);
        retVal=1;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAddStatusbarMessage(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("sim.addStatusbarMessage");

    int retVal=-1; // means error
    if (luaWrap_lua_gettop(L)==0)
    {
        App::clearStatusbar();
        retVal=1;
    }
    else
    {
        if (checkInputArguments(L,nullptr,lua_arg_nil,0))
        {
            App::clearStatusbar();
            retVal=1;
        }
        else
        {
            if (checkInputArguments(L,&errorString,lua_arg_string,0))
            {
                //retVal=simAddStatusbarMessage_internal(luaWrap_lua_tostring(L,1));
                CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
                if (it!=nullptr)
                {
                    App::logScriptMsg(it->getShortDescriptiveName().c_str(),sim_verbosity_msgs,luaWrap_lua_tostring(L,1));
                    retVal=1;
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetShapeMassAndInertia(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("sim.setShapeMassAndInertia");

    int result=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,9,lua_arg_number,3))
    {
        int handle=luaToInt(L,1);
        double mass=luaToDouble(L,2);
        double inertiaMatrix[9];
        getDoublesFromTable(L,3,9,inertiaMatrix);
        double centerOfMass[3];
        getDoublesFromTable(L,4,3,centerOfMass);
        double* transf=nullptr;
        double transformation[12];
        int res=checkOneGeneralInputArgument(L,5,lua_arg_number,12,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getDoublesFromTable(L,5,12,transformation);
                transf=transformation;
            }
            result=simSetShapeMassAndInertia_internal(handle,mass,inertiaMatrix,centerOfMass,transf);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,result);
    LUA_END(1);
}

int _simGetShapeMassAndInertia(luaWrap_lua_State* L)
{ // DEPRECATED
    TRACE_LUA_API;
    LUA_START("sim.getShapeMassAndInertia");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        double* transf=nullptr;
        double transformation[12];
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,12,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getDoublesFromTable(L,2,12,transformation);
                transf=transformation;
            }
            double mass;
            double inertiaMatrix[9];
            double centerOfMass[3];
            int result=simGetShapeMassAndInertia_internal(handle,&mass,inertiaMatrix,centerOfMass,transf);
            if (result==-1)
                luaWrap_lua_pushnil(L);
            luaWrap_lua_pushnumber(L,mass);
            pushDoubleTableOntoStack(L,9,inertiaMatrix);
            pushDoubleTableOntoStack(L,3,centerOfMass);
            LUA_END(3);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

typedef struct{
    double lastTime;
    double accel;
    int vdl;
    double currentPos;
    double maxVelocity;
    double currentVel;
    int objID;
    CSceneObject* object;
    int targetObjID;
    CSceneObject* targetObject;
    double relativeDistanceOnPath;
    double previousLL;
    C7Vector startTr;
    int positionAndOrOrientation;
} simMoveToObjData_old;

int _sim_moveToObj_1(luaWrap_lua_State* L)
{ // for backward compatibility with simMoveToObject on old threaded scripts
    TRACE_LUA_API;
    LUA_START("sim._moveToObj_1");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    { // Those are the arguments that are always required! (the rest can be ignored or set to nil!
        int objID=luaToInt(L,1);
        int targetObjID=luaToInt(L,2);
        double maxVelocity=0.1;
        double relativeDistanceOnPath=-1.0;
        int positionAndOrOrientation=3; // position and orientation (default value)
        CSceneObject* object=App::currentWorld->sceneObjects->getObjectFromHandle(objID);
        CSceneObject* targetObject=App::currentWorld->sceneObjects->getObjectFromHandle(targetObjID);
        double accel=0.0; // means infinite accel!! (default value)
        bool foundError=false;
        if ((!foundError)&&((object==nullptr)||(targetObject==nullptr)))
        {
            errorString=SIM_ERROR_OBJECT_OR_TARGET_OBJECT_DOES_NOT_EXIST;
            foundError=true;
        }
        if ((!foundError)&&(targetObject==object))
        {
            errorString=SIM_ERROR_OBJECT_IS_SAME_AS_TARGET_OBJECT;
            foundError=true;
        }
        // Now check the optional arguments:
        int res;
        if (!foundError) // position and/or orientation argument:
        {
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,true,&errorString);
            if (res==2)
            { // get the data
                positionAndOrOrientation=abs(luaToInt(L,3));
                if ((positionAndOrOrientation&3)==0)
                    positionAndOrOrientation=1; // position only
            }
            foundError=(res==-1);
        }
        if (!foundError) // positionOnPath argument:
        {
            res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,true,&errorString);
            if (res==2)
            { // get the data
                relativeDistanceOnPath=tt::getLimitedFloat(0.0,1.0,luaToDouble(L,4));
                if (targetObject->getObjectType()!=sim_object_path_type)
                {
                    errorString=SIM_ERROR_TARGET_OBJECT_IS_NOT_A_PATH;
                    foundError=true;
                }
            }
            foundError=(res==-1);
        }
        if (!foundError) // Velocity argument:
        {
            res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,false,false,&errorString);
            if (res==2)
            { // get the data
                maxVelocity=luaToDouble(L,5);
            }
            else
                foundError=true; // this argument is not optional!
        }
        if (!foundError) // Accel argument:
        {
            res=checkOneGeneralInputArgument(L,6,lua_arg_number,0,true,true,&errorString);
            if (res==2)
            { // get the data
                accel=fabs(luaToDouble(L,6));
            }
            foundError=(res==-1);
        }
        if (!foundError)
        { // do the job here!
            C7Vector startTr(object->getCumulativeTransformation());
            double currentVel=0.0;
            double lastTime=App::currentWorld->simulation->getSimulationTime();
            double vdl=1.0;
            // vld is the totalvirtual distance
            double currentPos=0.0;
            double previousLL=0.0;

            _memHandles_old.push_back(_nextMemHandle_old);
            simMoveToObjData_old* mem=new simMoveToObjData_old();
            mem->lastTime=lastTime;
            mem->accel=accel;
            mem->vdl=vdl;
            mem->currentPos=currentPos;
            mem->maxVelocity=maxVelocity;
            mem->currentVel=currentVel;
            mem->objID=objID;
            mem->object=object;
            mem->targetObjID=targetObjID;
            mem->targetObject=targetObject;
            mem->relativeDistanceOnPath=relativeDistanceOnPath;
            mem->previousLL=previousLL;
            mem->startTr=startTr;
            mem->positionAndOrOrientation=positionAndOrOrientation;

            _memBuffers_old.push_back(mem);
            luaWrap_lua_pushinteger(L,_nextMemHandle_old);
            _nextMemHandle_old++;
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED();
    LUA_END(0);
}

int _sim_moveToObj_2(luaWrap_lua_State* L)
{ // for backward compatibility with simMoveToObject on old threaded scripts
    TRACE_LUA_API;
    LUA_START("sim._moveToObj_2");
    if (checkInputArguments(L,&errorString,lua_arg_integer,0))
    {
        int h=luaToInt(L,1);
        simMoveToObjData_old* mem=nullptr;
        for (size_t i=0;i<_memHandles_old.size();i++)
        {
            if (_memHandles_old[i]==h)
            {
                mem=(simMoveToObjData_old*)_memBuffers_old[i];
                break;
            }
        }
        if (mem!=nullptr)
        {
            bool movementFinished=false;
            double currentTime=App::currentWorld->simulation->getSimulationTime()+App::currentWorld->simulation->getTimeStep();
            double dt=currentTime-mem->lastTime;
            mem->lastTime=currentTime;

            if (mem->accel==0.0)
            { // Means infinite acceleration
                double timeNeeded=(mem->vdl-mem->currentPos)/mem->maxVelocity;
                mem->currentVel=mem->maxVelocity;
                if (timeNeeded>dt)
                {
                    mem->currentPos+=dt*mem->maxVelocity;
                    dt=0.0; // this is what is left
                }
                else
                {
                    mem->currentPos=mem->vdl;
                    if (timeNeeded>=0.0)
                        dt-=timeNeeded;
                }
            }
            else
            {
                double p=mem->currentPos;
                double v=mem->currentVel;
                double t=dt;
                CLinMotionRoutines::getNextValues(p,v,mem->maxVelocity,mem->accel,0.0,mem->vdl,0.0,0.0,t);
                mem->currentPos=double(p);
                mem->currentVel=double(v);
                dt=double(t);
            }

            // Now check if we are within tolerances:
            if (fabs(mem->currentPos-mem->vdl)<=0.00001)
                movementFinished=true;

            // Set the new configuration of the object:
            double ll=mem->currentPos/mem->vdl;
            if (ll>1.0)
                ll=1.0;
            if ((App::currentWorld->sceneObjects->getObjectFromHandle(mem->objID)==mem->object)&&(App::currentWorld->sceneObjects->getObjectFromHandle(mem->targetObjID)==mem->targetObject)) // make sure the objects are still valid (running in a thread)
            {
                C7Vector targetTr(mem->targetObject->getCumulativeTransformation());
                bool goOn=true;
                if (mem->relativeDistanceOnPath>=0.0)
                { // we should have a path here
                    if (mem->targetObject->getObjectType()==sim_object_path_type)
                    {
                        C7Vector pathLoc;
                        if ( ((CPath_old*)mem->targetObject)->pathContainer->getTransformationOnBezierCurveAtNormalizedVirtualDistance(mem->relativeDistanceOnPath,pathLoc))
                            targetTr*=pathLoc;
                        else
                            mem->relativeDistanceOnPath=-1.0; // the path is empty!
                    }
                    else
                        goOn=false;
                }
                if (goOn)
                {
                    C7Vector newAbs;
                    newAbs.buildInterpolation(mem->startTr,targetTr,(ll-mem->previousLL)/(1.0-mem->previousLL));
                    mem->startTr=newAbs;
                    C7Vector parentInv(mem->object->getFullParentCumulativeTransformation().getInverse());
                    C7Vector currentTr(mem->object->getCumulativeTransformation());
                    if ((mem->positionAndOrOrientation&1)==0)
                        newAbs.X=currentTr.X;
                    if ((mem->positionAndOrOrientation&2)==0)
                        newAbs.Q=currentTr.Q;
                    mem->object->setLocalTransformation(parentInv*newAbs);
                }
                else
                    movementFinished=true; // the target object is not a path anymore!!

                mem->previousLL=ll;
                if (!movementFinished)
                {
                    luaWrap_lua_pushinteger(L,0); // mov. not yet finished
                    LUA_END(1);
                }
                luaWrap_lua_pushinteger(L,1); // mov. finished
                luaWrap_lua_pushnumber(L,dt); // success (deltaTime left)
                LUA_END(2);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED();
    LUA_END(0);
}

int _simCheckIkGroup(luaWrap_lua_State* L)
{ // deprecated on 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.checkIkGroup");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        if (luaWrap_lua_istable(L,2))
        {
            int jointCnt=(int)luaWrap_lua_rawlen(L,2);
            int* handles=new int[jointCnt];
            getIntsFromTable(L,2,jointCnt,handles);
            double* values=new double[jointCnt];

            int res=checkOneGeneralInputArgument(L,3,lua_arg_number,jointCnt,true,true,&errorString);
            if (res>=0)
            {
                int* jointOptionsP=nullptr;
                std::vector<int> jointOptions;
                if (res==2)
                {
                    jointOptions.resize(jointCnt);
                    getIntsFromTable(L,3,jointCnt,&jointOptions[0]);
                    jointOptionsP=&jointOptions[0];
                }

                int retVal=simCheckIkGroup_internal(luaToInt(L,1),jointCnt,handles,values,jointOptionsP);
                luaWrap_lua_pushinteger(L,retVal);
                pushDoubleTableOntoStack(L,jointCnt,values);
                delete[] values;
                delete[] handles;
                LUA_END(2);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,-1);
    LUA_END(1);
}

int _simCreateIkGroup(luaWrap_lua_State* L)
{ // DEPRECATED ON 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.createIkGroup");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int options=luaToInt(L,1);
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,2,true,true,&errorString);
        if (res>=0)
        {
            int intParams[2];
            int* intP=nullptr;
            if (res==2)
            {
                getIntsFromTable(L,2,2,intParams);
                intP=intParams;
            }
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,4,true,true,&errorString);
            if (res>=0)
            {
                double floatParams[4];
                double* floatP=nullptr;
                if (res==2)
                {
                    getDoublesFromTable(L,3,4,floatParams);
                    floatP=floatParams;
                }
                retVal=simCreateIkGroup_internal(options,intP,floatP,nullptr);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simRemoveIkGroup(luaWrap_lua_State* L)
{ // DEPRECATED ON 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.removeIkGroup");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        retVal=simRemoveIkGroup_internal(handle);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCreateIkElement(luaWrap_lua_State* L)
{ // DEPRECATED ON 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.createIkElement");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,4))
    {
        int ikGroup=luaToInt(L,1);
        int options=luaToInt(L,2);
        int intParams[4];
        getIntsFromTable(L,3,4,intParams);

        int res=checkOneGeneralInputArgument(L,4,lua_arg_number,4,true,true,&errorString);
        if (res>=0)
        {
            double floatParams[4];
            double* floatP=nullptr;
            if (res==2)
            {
                getDoublesFromTable(L,4,4,floatParams);
                floatP=floatParams;
            }
            retVal=simCreateIkElement_internal(ikGroup,options,intParams,floatP,nullptr);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simExportIk(luaWrap_lua_State* L)
{ // deprecated on 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.exportIk");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string pathAndFilename(luaWrap_lua_tostring(L,1));
        retVal=simExportIk_internal(pathAndFilename.c_str(),0,nullptr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simComputeJacobian(luaWrap_lua_State* L)
{ // deprecated on 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.computeJacobian");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simComputeJacobian_internal(luaToInt(L,1),luaToInt(L,2),nullptr);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetConfigForTipPose(luaWrap_lua_State* L)
{ // deprecated on 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.getConfigForTipPose");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,1,lua_arg_number,0,lua_arg_number,0))
    {
        int ikGroupHandle=luaToInt(L,1);
        std::vector<int> jointHandles;
        int jointCnt=int(luaWrap_lua_rawlen(L,2));
        jointHandles.resize(jointCnt);
        getIntsFromTable(L,2,jointCnt,&jointHandles[0]);
        double thresholdDist=luaToDouble(L,3);
        int maxTimeInMs=luaToInt(L,4);
        double metric[4]={1.0,1.0,1.0,0.1};
        int res=checkOneGeneralInputArgument(L,5,lua_arg_number,4,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
                getDoublesFromTable(L,5,4,metric);
            int collisionPairCnt=0;
            std::vector<int> _collisionPairs;
            int* collisionPairs=nullptr;
            res=checkOneGeneralInputArgument(L,6,lua_arg_number,-1,true,true,&errorString);
            if (res>=0)
            {
                if (res==2)
                {
                    collisionPairCnt=int(luaWrap_lua_rawlen(L,6))/2;
                    if (collisionPairCnt>0)
                    {
                        _collisionPairs.resize(collisionPairCnt*2);
                        getIntsFromTable(L,6,collisionPairCnt*2,&_collisionPairs[0]);
                        collisionPairs=&_collisionPairs[0];
                    }
                }
                std::vector<int> _jointOptions;
                int* jointOptions=nullptr;
                res=checkOneGeneralInputArgument(L,7,lua_arg_number,jointCnt,true,true,&errorString);
                if (res>=0)
                {
                    if (res==2)
                    {
                        _jointOptions.resize(jointCnt);
                        getIntsFromTable(L,7,jointCnt,&_jointOptions[0]);
                        jointOptions=&_jointOptions[0];
                    }

                    res=checkOneGeneralInputArgument(L,8,lua_arg_number,jointCnt,true,true,&errorString);
                    if (res>=0)
                    {
                        std::vector<double> _lowLimits;
                        _lowLimits.resize(jointCnt);
                        std::vector<double> _ranges;
                        _ranges.resize(jointCnt);
                        double* lowLimits=nullptr;
                        double* ranges=nullptr;
                        if (res==2)
                        {
                            getDoublesFromTable(L,8,jointCnt,&_lowLimits[0]);
                            lowLimits=&_lowLimits[0];
                        }
                        res=checkOneGeneralInputArgument(L,9,lua_arg_number,jointCnt,lowLimits==nullptr,lowLimits==nullptr,&errorString);
                        if (res>=0)
                        {
                            if (res==2)
                            {
                                getDoublesFromTable(L,9,jointCnt,&_ranges[0]);
                                ranges=&_ranges[0];
                            }
                            std::vector<double> foundConfig;
                            foundConfig.resize(jointCnt);
                            res=simGetConfigForTipPose_internal(ikGroupHandle,jointCnt,&jointHandles[0],thresholdDist,maxTimeInMs,&foundConfig[0],metric,collisionPairCnt,collisionPairs,jointOptions,lowLimits,ranges,nullptr);
                            if (res>0)
                            {
                                pushDoubleTableOntoStack(L,jointCnt,&foundConfig[0]);
                                LUA_END(1);
                            }
                        }
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGenerateIkPath(luaWrap_lua_State* L)
{ // deprecated on 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.generateIkPath");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,1,lua_arg_number,0))
    {
        int ikGroupHandle=luaToInt(L,1);
        std::vector<int> jointHandles;
        int jointCnt=int(luaWrap_lua_rawlen(L,2));
        jointHandles.resize(jointCnt);
        getIntsFromTable(L,2,jointCnt,&jointHandles[0]);
        int ptCnt=luaToInt(L,3);
        int res=checkOneGeneralInputArgument(L,4,lua_arg_number,-1,true,true,&errorString);
        if (res>=0)
        {
            int collisionPairCnt=0;
            std::vector<int> _collisionPairs;
            int* collisionPairs=nullptr;
            if (res==2)
            {
                collisionPairCnt=int(luaWrap_lua_rawlen(L,4))/2;
                if (collisionPairCnt>0)
                {
                    _collisionPairs.resize(collisionPairCnt*2);
                    getIntsFromTable(L,4,collisionPairCnt*2,&_collisionPairs[0]);
                    collisionPairs=&_collisionPairs[0];
                }
            }
            res=checkOneGeneralInputArgument(L,5,lua_arg_number,jointCnt,true,true,&errorString);
            if (res>=0)
            {
                std::vector<int> _jointOptions;
                int* jointOptions=nullptr;
                if (res==2)
                {
                    _jointOptions.resize(jointCnt);
                    getIntsFromTable(L,5,jointCnt,&_jointOptions[0]);
                    jointOptions=&_jointOptions[0];
                }
                double* path=simGenerateIkPath_internal(ikGroupHandle,jointCnt,&jointHandles[0],ptCnt,collisionPairCnt,collisionPairs,jointOptions,nullptr);
                if (path!=nullptr)
                {
                    pushDoubleTableOntoStack(L,jointCnt*ptCnt,path);
                    simReleaseBuffer_internal((char*)path);
                    LUA_END(1);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetIkGroupHandle(luaWrap_lua_State* L)
{ // deprecated on 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.getIkGroupHandle");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string name(luaWrap_lua_tostring(L,1));
        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
        retVal=simGetIkGroupHandle_internal(name.c_str());
        setCurrentScriptInfo_cSide(-1,-1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetIkGroupMatrix(luaWrap_lua_State* L)
{ // deprecated on 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.getIkGroupMatrix");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int ikGroupHandle=luaToInt(L,1);
        int options=luaToInt(L,2);
        int matrixSize[2];
        double* data=simGetIkGroupMatrix_internal(ikGroupHandle,options,matrixSize);
        if (data!=nullptr)
        {
            pushDoubleTableOntoStack(L,matrixSize[0]*matrixSize[1],data);
            pushIntTableOntoStack(L,2,matrixSize);
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simHandleIkGroup(luaWrap_lua_State* L)
{ // deprecated on 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.handleIkGroup");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simHandleIkGroup_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetIkGroupProperties(luaWrap_lua_State* L)
{ // deprecated on 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.setIkGroupProperties");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetIkGroupProperties_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3),luaToDouble(L,4),nullptr);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetIkElementProperties(luaWrap_lua_State* L)
{ // deprecated on 29.09.2020
    TRACE_LUA_API;
    LUA_START("sim.setIkElementProperties");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int res=checkOneGeneralInputArgument(L,4,lua_arg_number,2,true,true,&errorString);
        if (res>=0)
        {
            double* precision=nullptr;
            double prec[2];
            if (res==2)
            {
                getDoublesFromTable(L,4,2,prec);
                precision=prec;
            }
            res=checkOneGeneralInputArgument(L,5,lua_arg_number,2,true,true,&errorString);
            if (res>=0)
            {
                double* weight=nullptr;
                double w[2];
                if (res==2)
                {
                    getDoublesFromTable(L,5,2,w);
                    weight=w;
                }
                retVal=simSetIkElementProperties_internal(luaToInt(L,1),luaToInt(L,2),luaToInt(L,3),precision,weight,nullptr);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetThreadIsFree(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.setThreadIsFree");

    int retVal=0;
    /*
    int retVal=-1;
    if (!VThread::isCurrentThreadTheMainSimulationThread())
    {
        bool result=false;
        if (checkInputArguments(L,nullptr,lua_arg_bool,0))
        {
            result=CThreadPool_old::setThreadFreeMode(luaToBool(L,1));
            if (result)
                retVal=1;
            else
                retVal=0;
        }
        else
            errorString=SIM_ERROR_INVALID_ARGUMENT;
    }
    else
        errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_A_THREAD;
    */
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simTubeRead(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim._tubeRead");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int dataLength;
        char* data=simTubeRead_internal(luaToInt(L,1),&dataLength);
        if (data)
        {
            luaWrap_lua_pushlstring(L,(const char*)data,dataLength);
            delete[] data;
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simTubeOpen(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.tubeOpen");

    int retVal=-1; // Error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0,lua_arg_number,0))
    {
        std::string strTmp=luaWrap_lua_tostring(L,2);
        int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
        retVal=App::currentWorld->commTubeContainer->openTube(luaToInt(L,1),strTmp.c_str(),(it->getScriptType()==sim_scripttype_mainscript)||(it->getScriptType()==sim_scripttype_childscript),luaToInt(L,3));
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simTubeClose(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.tubeClose");

    int retVal=-1; // Error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simTubeClose_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simTubeWrite(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.tubeWrite");

    int retVal=-1; // Error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
    {
        size_t dataLength;
        char* data=(char*)luaWrap_lua_tolstring(L,2,&dataLength);
        retVal=simTubeWrite_internal(luaToInt(L,1),data,(int)dataLength);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simTubeStatus(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.tubeStatus");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int readSize;
        int writeSize;
        int status=simTubeStatus_internal(luaToInt(L,1),&readSize,&writeSize);
        if (status>=0)
        {
            luaWrap_lua_pushinteger(L,status);
            luaWrap_lua_pushinteger(L,readSize);
            luaWrap_lua_pushinteger(L,writeSize);
            LUA_END(3);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSendData(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.sendData");

    int retVal=-1;
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    if ( (it->getScriptType()==sim_scripttype_mainscript)||(it->getScriptType()==sim_scripttype_childscript) )
    {
        if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_string,0,lua_arg_string,0))
        {
            int targetID=luaToInt(L,1);
            if ( (targetID<=0)&&(targetID!=sim_handle_all)&&(targetID!=sim_handle_tree)&&(targetID!=sim_handle_chain) )
                errorString=SIM_ERROR_INVALID_TARGET_HANDLE;
            else
            {
                int dataHeader=luaToInt(L,2);
                if (dataHeader<0)
                    errorString=SIM_ERROR_INVALID_DATA_HEADER;
                else
                {
                    std::string dataName(luaWrap_lua_tostring(L,3));
                    if ( (dataName.length()==0)||(dataName.find(char(0))!=std::string::npos) )
                        errorString=SIM_ERROR_INVALID_DATA_NAME;
                    else
                    {
                        size_t dataLength;
                        char* data=(char*)luaWrap_lua_tolstring(L,4,&dataLength);
                        if (dataLength<1)
                            errorString=SIM_ERROR_INVALID_DATA;
                        else
                        {
                            // Following are default values:
                            int antennaHandle=sim_handle_self;
                            double actionRadius=100.0;
                            double emissionAngle1=piValue;
                            double emissionAngle2=piValT2;
                            double persistence=0.0;
                            bool err=false;
                            int res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,true,false,&errorString);
                            if (res==2)
                                antennaHandle=luaToInt(L,5);
                            err=err||((res!=0)&&(res!=2));
                            if (!err)
                            {
                                if ( (antennaHandle<0)&&(antennaHandle!=sim_handle_default)&&((antennaHandle!=sim_handle_self)||(it->getScriptType()!=sim_scripttype_childscript)) )
                                {
                                    errorString=SIM_ERROR_INVALID_ANTENNA_HANDLE;
                                    err=true;
                                }
                                else
                                {
                                    if (antennaHandle==sim_handle_self)
                                        antennaHandle=it->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_childscript);
                                    if (antennaHandle!=sim_handle_default)
                                    {
                                        CSceneObject* ant=App::currentWorld->sceneObjects->getObjectFromHandle(antennaHandle);
                                        if (ant==nullptr)
                                        {
                                            errorString=SIM_ERROR_INVALID_ANTENNA_HANDLE;
                                            err=true;
                                        }
                                    }
                                }
                            }
                            if (!err)
                            {
                                int res=checkOneGeneralInputArgument(L,6,lua_arg_number,0,true,false,&errorString);
                                if (res==2)
                                    actionRadius=luaToDouble(L,6);
                                err=err||((res!=0)&&(res!=2));
                            }
                            if (!err)
                            {
                                int res=checkOneGeneralInputArgument(L,7,lua_arg_number,0,true,false,&errorString);
                                if (res==2)
                                    emissionAngle1=luaToDouble(L,7);
                                err=err||((res!=0)&&(res!=2));
                            }
                            if (!err)
                            {
                                int res=checkOneGeneralInputArgument(L,8,lua_arg_number,0,true,false,&errorString);
                                if (res==2)
                                    emissionAngle2=luaToDouble(L,8);
                                err=err||((res!=0)&&(res!=2));
                            }
                            if (!err)
                            {
                                int res=checkOneGeneralInputArgument(L,9,lua_arg_number,0,true,false,&errorString);
                                if (res==2)
                                    persistence=luaToDouble(L,9);
                                err=err||((res!=0)&&(res!=2));
                            }
                            if (!err)
                            {
                                actionRadius=tt::getLimitedFloat(0.0,FLOAT_MAX,actionRadius);
                                emissionAngle1=tt::getLimitedFloat(0.0,piValue,emissionAngle1);
                                emissionAngle2=tt::getLimitedFloat(0.0,piValT2,emissionAngle2);
                                persistence=tt::getLimitedFloat(0.0,99999999999999.9,persistence);
                                if (persistence==0.0)
                                    persistence=App::currentWorld->simulation->getTimeStep()*1.5;

                                App::currentWorld->embeddedScriptContainer->broadcastDataContainer.broadcastData(currentScriptID,targetID,dataHeader,dataName,
                                    App::currentWorld->simulation->getSimulationTime()+persistence,actionRadius,antennaHandle,
                                    emissionAngle1,emissionAngle2,data,(int)dataLength);
                                retVal=1;
                            }
                        }
                    }
                }
            }
        }
    }
    else
        errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT_OR_CHILD_SCRIPT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simReceiveData(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.receiveData");

    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    if ( (it->getScriptType()==sim_scripttype_mainscript)||(it->getScriptType()==sim_scripttype_childscript) )
    {
        int dataHeader=-1;
        std::string dataName;
        int antennaHandle=sim_handle_self;
        int index=-1;
        bool err=false;
        int res=checkOneGeneralInputArgument(L,1,lua_arg_number,0,true,false,&errorString);
        if (res==2)
        {
            dataHeader=luaToInt(L,1);
            if (dataHeader<0)
                dataHeader=-1;
        }
        err=err||((res!=0)&&(res!=2));
        if (!err)
        {
            int res=checkOneGeneralInputArgument(L,2,lua_arg_string,0,true,true,&errorString);
            if (res==2)
            {
                dataName=luaWrap_lua_tostring(L,2);
                if (dataName.length()<1)
                {
                    errorString=SIM_ERROR_INVALID_DATA_NAME;
                    err=true;
                }
            }
            err=err||(res<0);
        }
        if (!err)
        {
            int res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if (res==2)
            {
                antennaHandle=luaToInt(L,3);
                if ( (antennaHandle<0)&&(antennaHandle!=sim_handle_default)&&((antennaHandle!=sim_handle_self)||(it->getScriptType()!=sim_scripttype_childscript)) )
                {
                    errorString=SIM_ERROR_INVALID_ANTENNA_HANDLE;
                    err=true;
                }
            }
            err=err||((res!=0)&&(res!=2));
            if (!err)
            {
                if (antennaHandle==sim_handle_self)
                    antennaHandle=it->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_childscript);
                if (antennaHandle!=sim_handle_default)
                {
                    CSceneObject* ant=App::currentWorld->sceneObjects->getObjectFromHandle(antennaHandle);
                    if (ant==nullptr)
                    {
                        errorString=SIM_ERROR_INVALID_ANTENNA_HANDLE;
                        err=true;
                    }
                }
            }
        }
        if (!err)
        {
            int res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
            if (res==2)
            {
                index=luaToInt(L,4);
                if (index<0)
                    index=-1;
            }
            err=err||((res!=0)&&(res!=2));
        }
        if (!err)
        {
            int theDataHeader;
            int theDataLength;
            int theSenderID;
            std::string theDataName;
            char* data0=App::currentWorld->embeddedScriptContainer->broadcastDataContainer.receiveData(currentScriptID,App::currentWorld->simulation->getSimulationTime(),
                    dataHeader,dataName,antennaHandle,theDataLength,index,theSenderID,theDataHeader,theDataName);
            if (data0!=nullptr)
            {
                luaWrap_lua_pushlstring(L,data0,theDataLength);
                luaWrap_lua_pushinteger(L,theSenderID);
                luaWrap_lua_pushinteger(L,theDataHeader);
                luaWrap_lua_pushstring(L,theDataName.c_str());
                LUA_END(4);
            }
        }
    }
    else
        errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT_OR_CHILD_SCRIPT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

typedef struct{
    int objID;
    CSceneObject* object;
    int pathID;
    CPath_old* path;
    double lastTime;
    double accel;
    double pos;
    double vel;
    double maxVelocity;
    double bezierPathLength;
    int positionAndOrOrientation;
} simFollowPath_old;

int _sim_followPath_1(luaWrap_lua_State* L)
{ // for backward compatibility with simFollowPath on old threaded scripts
    TRACE_LUA_API;
    LUA_START("sim._followPath_1");
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    { // Those are the arguments that are always required! (the rest can be ignored or set to nil!
        int objID=luaToInt(L,1);
        int pathID=luaToInt(L,2);
        double posOnPath=luaToDouble(L,4);
        int positionAndOrOrientation=abs(luaToInt(L,3));
        if (positionAndOrOrientation==0)
            positionAndOrOrientation=1;
        double maxVelocity=luaToDouble(L,5);
        CSceneObject* object=App::currentWorld->sceneObjects->getObjectFromHandle(objID);
        CPath_old* path=App::currentWorld->sceneObjects->getPathFromHandle(pathID);
        double accel=0.0; // means infinite accel!! (default value)
        bool foundError=false;
        if ((!foundError)&&(object==nullptr))
        {
            errorString=SIM_ERROR_OBJECT_INEXISTANT;
            foundError=true;
        }
        if ((!foundError)&&(path==nullptr))
        {
            errorString=SIM_ERROR_PATH_INEXISTANT;
            foundError=true;
        }
        if (!foundError)
        {
            if (path->pathContainer->getAttributes()&sim_pathproperty_closed_path)
            {
                if (posOnPath<0.0)
                    posOnPath=0.0;
            }
            else
                posOnPath=tt::getLimitedFloat(0.0,1.0,posOnPath);
        }

        // Now check the optional arguments:
        int res;
        if (!foundError) // Accel argument:
        {
            res=checkOneGeneralInputArgument(L,6,lua_arg_number,0,true,true,&errorString);
            if (res==2)
            { // get the data
                accel=fabs(luaToDouble(L,6));
            }
            foundError=(res==-1);
        }
        if (!foundError)
        { // do the job here!
            double bezierPathLength=path->pathContainer->getBezierVirtualPathLength();
            double pos=posOnPath*bezierPathLength;
            double vel=0.0;
            double lastTime=App::currentWorld->simulation->getSimulationTime();
            bool movementFinished=(bezierPathLength==0.0);
            if (movementFinished)
                luaWrap_lua_pushinteger(L,-1);
            else
            {
                _memHandles_old.push_back(_nextMemHandle_old);
                simFollowPath_old* mem=new simFollowPath_old();
                mem->objID=objID;
                mem->object=object;
                mem->pathID=pathID;
                mem->path=path;
                mem->lastTime=lastTime;
                mem->accel=accel;
                mem->pos=pos;
                mem->vel=vel;
                mem->maxVelocity=maxVelocity;
                mem->bezierPathLength=bezierPathLength;
                mem->positionAndOrOrientation=positionAndOrOrientation;
                _memBuffers_old.push_back(mem);
                luaWrap_lua_pushinteger(L,_nextMemHandle_old);
                _nextMemHandle_old++;
            }
            LUA_END(1);
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED();
    LUA_END(0);
}

int _sim_followPath_2(luaWrap_lua_State* L)
{ // for backward compatibility with simFollowPath on old threaded scripts
    TRACE_LUA_API;
    LUA_START("sim._followPath_2");
    if (checkInputArguments(L,&errorString,lua_arg_integer,0))
    {
        int h=luaToInt(L,1);
        simFollowPath_old* mem=nullptr;
        for (size_t i=0;i<_memHandles_old.size();i++)
        {
            if (_memHandles_old[i]==h)
            {
                mem=(simFollowPath_old*)_memBuffers_old[i];
                break;
            }
        }
        if (mem!=nullptr)
        {
            if ( (App::currentWorld->sceneObjects->getObjectFromHandle(mem->objID)==mem->object)&&(App::currentWorld->sceneObjects->getPathFromHandle(mem->pathID)==mem->path) ) // make sure the objects are still valid (running in a thread)
            {
                double dt=App::currentWorld->simulation->getTimeStep(); // this is the time left if we leave here
                bool movementFinished=false;
                double currentTime=App::currentWorld->simulation->getSimulationTime()+App::currentWorld->simulation->getTimeStep();
                dt=currentTime-mem->lastTime;
                mem->lastTime=currentTime;
                if (mem->accel==0.0)
                { // Means infinite acceleration
                    mem->path->pathContainer->handlePath_keepObjectUnchanged(dt,mem->pos,mem->vel,mem->maxVelocity,1.0,false,true);
                }
                else
                {
                    mem->path->pathContainer->handlePath_keepObjectUnchanged(dt,mem->pos,mem->vel,mem->maxVelocity,mem->accel,false,false);
                }

                // Now check if we are within tolerances:
                if ( ((fabs(mem->pos-mem->bezierPathLength)<=0.00001)&&(mem->maxVelocity>=0.0)) || ((fabs(mem->pos-0.0)<=0.00001)&&(mem->maxVelocity<=0.0)) )
                    movementFinished=true;

                // Set the new configuration of the object:
                double ll=double(mem->pos/mem->bezierPathLength);
                C7Vector newAbs;
                if (mem->path->pathContainer->getTransformationOnBezierCurveAtNormalizedVirtualDistance(ll,newAbs))
                {
                    newAbs=mem->path->getCumulativeTransformation()*newAbs;
                    C7Vector parentInv(mem->object->getFullParentCumulativeTransformation().getInverse());
                    C7Vector currAbs(mem->object->getCumulativeTransformation());
                    if ((mem->positionAndOrOrientation&1)==0)
                        newAbs.X=currAbs.X;
                    if ((mem->positionAndOrOrientation&2)==0)
                        newAbs.Q=currAbs.Q;
                    mem->object->setLocalTransformation(parentInv*newAbs);
                }
                else
                    movementFinished=true;

                if (!movementFinished)
                {
                    luaWrap_lua_pushinteger(L,0); // mov. not yet finished
                    LUA_END(1);
                }
                luaWrap_lua_pushinteger(L,1); // mov. finished
                luaWrap_lua_pushnumber(L,dt); // success (deltaTime left)
                LUA_END(2);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED();
    LUA_END(0);
}

int _simGetDataOnPath(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.getDataOnPath");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int auxFlags;
        double auxChannels[4];
        if (simGetDataOnPath_internal(luaToInt(L,1),luaToDouble(L,2),0,&auxFlags,auxChannels)==1)
        {
            luaWrap_lua_pushinteger(L,auxFlags);
            pushDoubleTableOntoStack(L,4,auxChannels);
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetPositionOnPath(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.getPositionOnPath");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        double coord[3];
        if (simGetPositionOnPath_internal(luaToInt(L,1),luaToDouble(L,2),coord)==1)
        {
            pushDoubleTableOntoStack(L,3,coord);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetOrientationOnPath(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.getOrientationOnPath");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        double coord[3];
        if (simGetOrientationOnPath_internal(luaToInt(L,1),luaToDouble(L,2),coord)==1)
        {
            pushDoubleTableOntoStack(L,3,coord);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetClosestPositionOnPath(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.getClosestPositionOnPath");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,3))
    {
        double coord[3];
        getDoublesFromTable(L,2,3,coord);
        double dist=0.0;
        if (simGetClosestPositionOnPath_internal(luaToInt(L,1),coord,&dist)!=-1)
        {
            luaWrap_lua_pushnumber(L,dist);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetPathPosition(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.getPathPosition");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double pathVal[1];
        if (simGetPathPosition_internal(luaToInt(L,1),pathVal)!=-1)
        {
            luaWrap_lua_pushnumber(L,pathVal[0]);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetPathPosition(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.setPathPosition");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetPathPosition_internal(luaToInt(L,1),luaToDouble(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetPathLength(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.getPathLength");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double pathLen[1];
        if (simGetPathLength_internal(luaToInt(L,1),pathLen)!=-1)
        {
            luaWrap_lua_pushnumber(L,pathLen[0]);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simInsertPathCtrlPoints(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.insertPathCtrlPoints");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int pathHandle=luaToInt(L,1);
        int options=luaToInt(L,2);
        int startIndex=luaToInt(L,3);
        int ptCnt=luaToInt(L,4);
        int floatOrIntCountPerPoint=11;
        if (options&2)
            floatOrIntCountPerPoint=16;

        int res=checkOneGeneralInputArgument(L,5,lua_arg_number,ptCnt*floatOrIntCountPerPoint,false,false,&errorString);
        if (res==2)
        {
            double* data=new double[ptCnt*floatOrIntCountPerPoint];
            getDoublesFromTable(L,5,ptCnt*floatOrIntCountPerPoint,data);
            for (int i=0;i<ptCnt;i++)
                ((int*)(data+floatOrIntCountPerPoint*i+8))[0]=int(data[floatOrIntCountPerPoint*i+8]+0.5);
            if (options&2)
            {
                for (int i=0;i<ptCnt;i++)
                    ((int*)(data+floatOrIntCountPerPoint*i+11))[0]=int(data[floatOrIntCountPerPoint*i+11]+0.5);
            }
            retVal=simInsertPathCtrlPoints_internal(pathHandle,options,startIndex,ptCnt,data);
            delete[] data;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCutPathCtrlPoints(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.cutPathCtrlPoints");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int pathHandle=luaToInt(L,1);
        int startIndex=luaToInt(L,2);
        int ptCnt=luaToInt(L,3);
        retVal=simCutPathCtrlPoints_internal(pathHandle,startIndex,ptCnt);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _sim_CreatePath(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim._createPath");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int attribs=luaToInt(L,1);
        int* intP=nullptr;
        double* floatP=nullptr;
        int intParams[3];
        double floatParams[3];
        float* color=nullptr;
        float c[12];
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,3,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getIntsFromTable(L,2,3,intParams);
                intP=intParams;
            }
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,3,true,true,&errorString);
            if (res>=0)
            {
                if (res==2)
                {
                    getDoublesFromTable(L,3,3,floatParams);
                    floatP=floatParams;
                }
                res=checkOneGeneralInputArgument(L,4,lua_arg_number,12,true,true,&errorString);
                if (res>=0)
                {
                    if (res==2)
                    {
                        getFloatsFromTable(L,4,12,c);
                        color=c;
                    }
                    retVal=simCreatePath_internal(attribs,intP,floatP,color);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetScriptExecutionCount(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.getScriptExecutionCount");

    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,it->getNumberOfPasses());
    LUA_END(1);
}

int _simIsScriptExecutionThreaded(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.isScriptExecutionThreaded");

    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    int retVal=0;
    if ((it!=nullptr)&&it->getThreadedExecution_oldThreads())
        retVal=1;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simIsScriptRunningInThread(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.isScriptRunningInThread");

    int retVal=1;
    if (VThread::isCurrentThreadTheMainSimulationThread())
        retVal=0;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetThreadResumeLocation(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.setThreadResumeLocation");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        if (CThreadPool_old::setThreadResumeLocation(luaToInt(L,1),luaToInt(L,2)))
            retVal=1;
        else
            retVal=0;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simResumeThreads(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.resumeThreads");

    int retVal=-1;
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (it->getScriptType()==sim_scripttype_mainscript)
    {
        if (checkInputArguments(L,&errorString,lua_arg_number,0))
        {
            int loc=luaToInt(L,1);

            int startT=(int)VDateTime::getTimeInMs();
            retVal=App::currentWorld->embeddedScriptContainer->callChildAndEmbeddedScripts(sim_scripttype_childscript|sim_scripttype_threaded_old,loc,nullptr,nullptr);
            // Following line important: when erasing a running threaded script object, with above cascaded
            // call, the thread will never resume nor be able to end. Next line basically runs all
            // that were not yet ran:
            retVal+=CThreadPool_old::handleAllThreads_withResumeLocation(loc);
        }
    }
    else
        errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simLaunchThreadedChildScripts(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.launchThreadedChildScripts");

    int retVal=-1; // means error
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (it!=nullptr)
    {
        if (it->getScriptType()==sim_scripttype_mainscript)
        {
            int startT=(int)VDateTime::getTimeInMs();
            retVal=App::currentWorld->embeddedScriptContainer->callChildAndEmbeddedScripts(sim_scripttype_childscript|sim_scripttype_threaded_old,sim_scriptthreadresume_launch,nullptr,nullptr);
        }
        else
            errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetThreadId(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.getThreadId");

    int retVal=simGetThreadId_internal();

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetUserParameter(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.getUserParameter");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
    {
        int handle=luaToInt(L,1);
        if (handle==sim_handle_self)
        {
            handle=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
            CScriptObject* it=App::worldContainer->getScriptFromHandle(handle);
            handle=it->getObjectHandleThatScriptIsAttachedTo(-1);
        }
        bool returnString=false;
        int ret=checkOneGeneralInputArgument(L,3,lua_arg_bool,0,true,false,&errorString);
        if ((ret==0)||(ret==2))
        {
            if (ret==2)
                returnString=luaToBool(L,3);
            std::string parameterName(luaWrap_lua_tostring(L,2));
            int l;
            char* p=simGetUserParameter_internal(handle,parameterName.c_str(),&l);
            if (p!=nullptr)
            {
                std::string a;
                a.assign(p,l);
                if (returnString)
                    luaWrap_lua_pushlstring(L,a.c_str(),a.length());
                else
                    pushCorrectTypeOntoLuaStack_old(L,a);
                simReleaseBuffer_internal(p);
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetUserParameter(luaWrap_lua_State* L)
{ // deprecated on 01.10.2020
    TRACE_LUA_API;
    LUA_START("sim.setUserParameter");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0,lua_arg_string,0))
    {
        int handle=luaToInt(L,1);
        if (handle==sim_handle_self)
        {
            handle=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
            CScriptObject* it=App::worldContainer->getScriptFromHandle(handle);
            handle=it->getObjectHandleThatScriptIsAttachedTo(-1);
        }
        std::string parameterName(luaWrap_lua_tostring(L,2));
        size_t parameterValueLength;
        char* parameterValue=(char*)luaWrap_lua_tolstring(L,3,&parameterValueLength);
        retVal=simSetUserParameter_internal(handle,parameterName.c_str(),parameterValue,(int)parameterValueLength);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _genericFunctionHandler_old(luaWrap_lua_State* L,CScriptCustomFunction* func)
{ // deprecated around 2015
    TRACE_LUA_API;
    // We first read all arguments from the stack
    std::vector<char> inBoolVector;
    std::vector<int> inIntVector;
    std::vector<float> inFloatVector;
    std::vector<double> inDoubleVector;
    std::vector<std::string> inStringVector;
    std::vector<std::string> inCharVector;
    std::vector<int> inInfoVector;
    for (int i=0;i<int(func->inputArgTypes.size());i++)
    {
        if (!readCustomFunctionDataFromStack_old(L,i+1,func->inputArgTypes[i],inBoolVector,inIntVector,inFloatVector,inDoubleVector,inStringVector,inCharVector,inInfoVector))
            break;
    }

    // Now we retrieve the object ID this script might be attached to:
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* itObj=App::worldContainer->getScriptFromHandle(currentScriptID);
    int linkedObject=-1;
    if (itObj->getScriptType()==sim_scripttype_childscript)
    {
        CSceneObject* obj=App::currentWorld->sceneObjects->getObjectFromHandle(itObj->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_childscript));
        if (obj!=nullptr)
            linkedObject=obj->getObjectHandle();
    }
    if (itObj->getScriptType()==sim_scripttype_customizationscript)
    {
        CSceneObject* obj=App::currentWorld->sceneObjects->getObjectFromHandle(itObj->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_customizationscript));
        if (obj!=nullptr)
            linkedObject=obj->getObjectHandle();
    }
    // We prepare the callback structure:
    SLuaCallBack* p=new SLuaCallBack;
    p->objectID=linkedObject;
    p->scriptID=currentScriptID;
    p->inputBool=nullptr;
    p->inputInt=nullptr;
    p->inputFloat=nullptr;
    p->inputDouble=nullptr;
    p->inputChar=nullptr;
    p->inputCharBuff=nullptr;
    p->inputArgCount=0;
    p->inputArgTypeAndSize=nullptr;
    p->outputBool=nullptr;
    p->outputInt=nullptr;
    p->outputFloat=nullptr;
    p->outputDouble=nullptr;
    p->outputChar=nullptr;
    p->outputCharBuff=nullptr;
    p->outputArgCount=0;
    p->outputArgTypeAndSize=nullptr;
    p->waitUntilZero=0;
    // Now we prepare the input buffers:
    p->inputBool=new bool[inBoolVector.size()];
    p->inputInt=new int[inIntVector.size()];
    p->inputFloat=new float[inFloatVector.size()];
    p->inputDouble=new double[inDoubleVector.size()];
    int charCnt=0;
    for (size_t k=0;k<inStringVector.size();k++)
        charCnt+=(int)inStringVector[k].length()+1; // terminal 0
    p->inputChar=new char[charCnt];

    int charBuffCnt=0;
    for (size_t k=0;k<inCharVector.size();k++)
        charBuffCnt+=(int)inCharVector[k].length();
    p->inputCharBuff=new char[charBuffCnt];

    p->inputArgCount=(int)inInfoVector.size()/2;
    p->inputArgTypeAndSize=new int[inInfoVector.size()];
    // We fill the input buffers:
    for (int k=0;k<int(inBoolVector.size());k++)
        p->inputBool[k]=inBoolVector[k];
    for (int k=0;k<int(inIntVector.size());k++)
        p->inputInt[k]=inIntVector[k];
    for (int k=0;k<int(inFloatVector.size());k++)
        p->inputFloat[k]=inFloatVector[k];
    for (int k=0;k<int(inDoubleVector.size());k++)
        p->inputDouble[k]=inDoubleVector[k];
    charCnt=0;
    for (int k=0;k<int(inStringVector.size());k++)
    {
        for (int l=0;l<int(inStringVector[k].length());l++)
            p->inputChar[charCnt+l]=inStringVector[k][l];
        charCnt+=(int)inStringVector[k].length();
        // terminal 0:
        p->inputChar[charCnt]=0;
        charCnt++;
    }

    charBuffCnt=0;
    for (int k=0;k<int(inCharVector.size());k++)
    {
        for (int l=0;l<int(inCharVector[k].length());l++)
            p->inputCharBuff[charBuffCnt+l]=inCharVector[k][l];
        charBuffCnt+=(int)inCharVector[k].length();
    }

    for (int k=0;k<int(inInfoVector.size());k++)
        p->inputArgTypeAndSize[k]=inInfoVector[k];

    // Now we can call the callback:
    func->callBackFunction_old(p);

    bool dontDeleteStructureYet=false;
    while (p->waitUntilZero!=0)
    { // backward compatibility (for real threads)
        if (!CThreadPool_old::switchBackToPreviousThread())
            break;
        if (CThreadPool_old::getSimulationStopRequestedAndActivated())
        { // give a chance to the c app to set the waitUntilZero to zero! (above turns true only 1-2 secs after the stop request arrived)
            // Following: the extension module might still write 0 into that position to signal "no more waiting" in
            // case this while loop got interrupted by a stop request.
            dontDeleteStructureYet=true;
            break;
        }
    }

    // We first delete the input buffers:
    delete[] p->inputBool;
    delete[] p->inputInt;
    delete[] p->inputFloat;
    delete[] p->inputDouble;
    delete[] p->inputChar;
    delete[] p->inputCharBuff;
    delete[] p->inputArgTypeAndSize;
    // Now we have to build the returned data onto the stack:
    if (p->outputArgCount!=0)
    {
        int boolPt=0;
        int intPt=0;
        int floatPt=0;
        int doublePt=0;
        int stringPt=0;
        int stringBuffPt=0;
        for (int i=0;i<p->outputArgCount;i++)
        {
            writeCustomFunctionDataOntoStack_old(L,p->outputArgTypeAndSize[2*i+0],p->outputArgTypeAndSize[2*i+1],
                p->outputBool,boolPt,
                p->outputInt,intPt,
                p->outputFloat,floatPt,
                p->outputDouble,doublePt,
                p->outputChar,stringPt,
                p->outputCharBuff,stringBuffPt);
        }
    }
    // We now delete the output buffers:
    delete[] p->outputBool;
    delete[] p->outputInt;
    delete[] p->outputFloat;
    delete[] p->outputDouble;
    delete[] p->outputChar;
    delete[] p->outputCharBuff;
    delete[] p->outputArgTypeAndSize;
    // And we return the number of arguments:
    int outputArgCount=p->outputArgCount;
    if (dontDeleteStructureYet)
    {   // We cannot yet delete the structure because an extension module might still write '0' into
        // p->waitUntilZero!! We delete the structure at the end of the simulation.
        App::currentWorld->embeddedScriptContainer->addCallbackStructureObjectToDestroyAtTheEndOfSimulation_old(p);
    }
    else
        delete p;
    if (outputArgCount==0)
    {
        luaWrap_lua_pushnil(L);
        outputArgCount=1;
    }
    return(outputArgCount);
}

int _simSetCollectionName(luaWrap_lua_State* L)
{ // deprecated on 17.11.2020
    TRACE_LUA_API;
    LUA_START("sim.setCollectionName");
    int retVal=-1;// error

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
        retVal=simSetCollectionName_internal(luaToInt(L,1),luaWrap_lua_tostring(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _sim_CreateCollection(luaWrap_lua_State* L)
{ // deprecated on 17.11.2020
    TRACE_LUA_API;
    LUA_START("sim._createCollection");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0))
    {
        std::string collName(luaWrap_lua_tostring(L,1));
        int options=luaToInt(L,2);
        retVal=simCreateCollection_internal(collName.c_str(),options);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAddObjectToCollection(luaWrap_lua_State* L)
{ // deprecated on 17.11.2020
    TRACE_LUA_API;
    LUA_START("sim.addObjectToCollection");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int collHandle=luaToInt(L,1);
        int objHandle=luaToInt(L,2);
        int what=luaToInt(L,3);
        int options=luaToInt(L,4);
        retVal=simAddObjectToCollection_internal(collHandle,objHandle,what,options);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetCollectionHandle(luaWrap_lua_State* L)
{ // deprecated on 17.11.2020
    TRACE_LUA_API;
    LUA_START("sim.getCollectionHandle");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string name(luaWrap_lua_tostring(L,1));
        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
        retVal=simGetCollectionHandle_internal(name.c_str());
        setCurrentScriptInfo_cSide(-1,-1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simRemoveCollection(luaWrap_lua_State* L)
{ // deprecated on 17.11.2020
    TRACE_LUA_API;
    LUA_START("sim.removeCollection");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simRemoveCollection_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simEmptyCollection(luaWrap_lua_State* L)
{ // deprecated on 17.11.2020
    TRACE_LUA_API;
    LUA_START("sim.emptyCollection");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simEmptyCollection_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetCollectionName(luaWrap_lua_State* L)
{ // deprecated on 17.11.2020
    TRACE_LUA_API;
    LUA_START("sim.getCollectionName");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        char* name=simGetCollectionName_internal(luaToInt(L,1));
        if (name!=nullptr)
        {
            luaWrap_lua_pushstring(L,name);
            simReleaseBuffer_internal(name);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simHandleCollision(luaWrap_lua_State* L)
{ // deprecated on 20.11.2020
    TRACE_LUA_API;
    LUA_START("sim.handleCollision");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int objHandle=luaToInt(L,1);
        retVal=simHandleCollision_internal(objHandle);
        if ( (retVal>0)&&(objHandle>=0) )
        {
            int collObjHandles[2];
            CCollisionObject_old* it=App::currentWorld->collisions->getObjectFromHandle(objHandle);
            if (it!=nullptr)
            {
                it->readCollision(collObjHandles);
                luaWrap_lua_pushinteger(L,retVal);
                pushIntTableOntoStack(L,2,collObjHandles);
                LUA_END(2);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simReadCollision(luaWrap_lua_State* L)
{ // deprecated on 20.11.2020
    TRACE_LUA_API;
    LUA_START("sim.readCollision");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int objHandle=luaToInt(L,1);
        retVal=simReadCollision_internal(objHandle);
        if (retVal>0)
        {
            int collObjHandles[2];
            CCollisionObject_old* it=App::currentWorld->collisions->getObjectFromHandle(objHandle);
            if (it!=nullptr)
            {
                it->readCollision(collObjHandles);
                luaWrap_lua_pushinteger(L,retVal);
                pushIntTableOntoStack(L,2,collObjHandles);
                LUA_END(2);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simHandleDistance(luaWrap_lua_State* L)
{ // deprecated on 20.11.2020
    TRACE_LUA_API;
    LUA_START("sim.handleDistance");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double d;
        retVal=simHandleDistance_internal(luaToInt(L,1),&d);
        if (retVal==1)
        {
            luaWrap_lua_pushinteger(L,retVal);
            luaWrap_lua_pushnumber(L,d);
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simReadDistance(luaWrap_lua_State* L)
{ // deprecated on 20.11.2020
    TRACE_LUA_API;
    LUA_START("sim.readDistance");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double d;
        retVal=simReadDistance_internal(luaToInt(L,1),&d);
        if (retVal==1)
        {
            luaWrap_lua_pushinteger(L,retVal);
            luaWrap_lua_pushnumber(L,d);
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetCollisionHandle(luaWrap_lua_State* L)
{ // deprecated on 20.11.2020
    TRACE_LUA_API;
    LUA_START("sim.getCollisionHandle");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string name(luaWrap_lua_tostring(L,1));
        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
        retVal=simGetCollisionHandle_internal(name.c_str());
        setCurrentScriptInfo_cSide(-1,-1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetDistanceHandle(luaWrap_lua_State* L)
{ // deprecated on 20.11.2020
    TRACE_LUA_API;
    LUA_START("sim.getDistanceHandle");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        std::string name(luaWrap_lua_tostring(L,1));
        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
        retVal=simGetDistanceHandle_internal(name.c_str());
        setCurrentScriptInfo_cSide(-1,-1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simResetCollision(luaWrap_lua_State* L)
{ // deprecated on 20.11.2020
    TRACE_LUA_API;
    LUA_START("sim.resetCollision");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simResetCollision_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simResetDistance(luaWrap_lua_State* L)
{ // deprecated on 20.11.2020
    TRACE_LUA_API;
    LUA_START("sim.resetDistance");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simResetDistance_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAddBanner(luaWrap_lua_State* L)
{ // deprecated on 23.11.2020
    TRACE_LUA_API;
    LUA_START("sim.addBanner");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0,lua_arg_number,0))
    {
        std::string label(luaWrap_lua_tostring(L,1));
        double size=luaToDouble(L,2);
        int options=luaToInt(L,3);

        double* positionAndEulerAngles=nullptr;
        int parentObjectHandle=-1;
        float* labelColors=nullptr;
        float* backgroundColors=nullptr;

        int res=checkOneGeneralInputArgument(L,4,lua_arg_number,6,true,true,&errorString);
        int okToGo=(res!=-1);
        if (okToGo)
        {
            double positionAndEulerAnglesC[6];
            if (res>0)
            {
                if (res==2)
                {
                    getDoublesFromTable(L,4,6,positionAndEulerAnglesC);
                    positionAndEulerAngles=positionAndEulerAnglesC;
                }
                res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,true,true,&errorString);
                okToGo=(res!=-1);
                if (okToGo)
                {
                    if (res>0)
                    {
                        if (res==2)
                            parentObjectHandle=luaToInt(L,5);
                        res=checkOneGeneralInputArgument(L,6,lua_arg_number,12,true,true,&errorString);
                        okToGo=(res!=-1);
                        if (okToGo)
                        {
                            float labelColorsC[12];
                            if (res>0)
                            {
                                if (res==2)
                                {
                                    getFloatsFromTable(L,6,12,labelColorsC);
                                    labelColors=labelColorsC;
                                }
                                res=checkOneGeneralInputArgument(L,7,lua_arg_number,12,true,true,&errorString);
                                okToGo=(res!=-1);
                                if (okToGo)
                                {
                                    float backgroundColorsC[12];
                                    if (res>0)
                                    {
                                        if (res==2)
                                        {
                                            getFloatsFromTable(L,7,12,backgroundColorsC);
                                            backgroundColors=backgroundColorsC;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (okToGo)
            {
                retVal=simAddBanner_internal(label.c_str(),size,options,positionAndEulerAngles,parentObjectHandle,labelColors,backgroundColors);
                if (retVal!=-1)
                { // following condition added on 2011/01/06 so as to not remove objects created from the c/c++ interface or an add-on:
                    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
                    CScriptObject* itScrObj=App::worldContainer->getScriptFromHandle(currentScriptID);
                    CBannerObject* anObj=App::currentWorld->bannerCont->getObject(retVal);
                    if (anObj!=nullptr)
                        anObj->setCreatedFromScript((itScrObj->getScriptType()==sim_scripttype_mainscript)||(itScrObj->getScriptType()==sim_scripttype_childscript));
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simRemoveBanner(luaWrap_lua_State* L)
{ // deprecated on 23.11.2020
    TRACE_LUA_API;
    LUA_START("sim.removeBanner");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int objectHandle=luaToInt(L,1);
        if (objectHandle==sim_handle_all)
        { // following condition added here on 2011/01/06 so as not to remove objects created from a C/c++ call
            App::currentWorld->bannerCont->eraseAllObjects(true);
            retVal=1;
        }
        else
            retVal=simRemoveBanner_internal(objectHandle);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAddGhost(luaWrap_lua_State* L)
{ // deprecated on 23.11.2020
    TRACE_LUA_API;
    LUA_START("sim.addGhost");

    int retVal=-1; // error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int ghostGroup=luaToInt(L,1);
        int objectHandle=luaToInt(L,2);
        int options=luaToInt(L,3);
        double startTime=luaToDouble(L,4);
        double endTime=luaToDouble(L,5);
        int res=checkOneGeneralInputArgument(L,6,lua_arg_number,12,true,true,&errorString);
        if (res>=0)
        {
            bool defaultColors=true;
            float color[12];
            if (res==2)
            {
                defaultColors=false;
                getFloatsFromTable(L,6,12,color);
            }
            if (defaultColors)
                retVal=simAddGhost_internal(ghostGroup,objectHandle,options,startTime,endTime,nullptr);
            else
                retVal=simAddGhost_internal(ghostGroup,objectHandle,options,startTime,endTime,color);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simModifyGhost(luaWrap_lua_State* L)
{ // deprecated on 23.11.2020
    TRACE_LUA_API;
    LUA_START("sim.modifyGhost");

    int retVal=-1; // error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0))
    {
        int ghostGroup=luaToInt(L,1);
        int ghostId=luaToInt(L,2);
        int operation=luaToInt(L,3);
        double floatValue=luaToDouble(L,4);

        int options=0;
        int optionsMask=0;
        int res=0;
        if (operation==10)
            res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,false,false,&errorString);
        if (res>=0)
        {
            if ((res==2)&&(operation==10))
            {
                options=luaToInt(L,5);
                res=checkOneGeneralInputArgument(L,6,lua_arg_number,0,false,false,&errorString);
                if (res==2)
                    optionsMask=luaToInt(L,6);
                else
                    res=-1;
            }
            if (res>=0)
            {
                int floatCnt=7;
                if (operation==13)
                    floatCnt=12;
                res=0;
                if ((operation>=11)&&(operation<=13))
                    res=checkOneGeneralInputArgument(L,7,lua_arg_number,floatCnt,false,false,&errorString);
                if (res>=0)
                {
                    double colorOrTransfData[12];
                    if ((res==2)&&(operation>=11)&&(operation<=13))
                        getDoublesFromTable(L,7,floatCnt,colorOrTransfData);
                    retVal=simModifyGhost_internal(ghostGroup,ghostId,operation,floatValue,options,optionsMask,colorOrTransfData);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetGraphUserData(luaWrap_lua_State* L)
{ // deprecated on 23.11.2020
    TRACE_LUA_API;
    LUA_START("sim.setGraphUserData");

    int retVal=-1; // for error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0,lua_arg_number,0))
    {
        int graphHandle=luaToInt(L,1);
        std::string dataName(luaWrap_lua_tostring(L,2));
        double data=luaToDouble(L,3);
        retVal=simSetGraphUserData_internal(graphHandle,dataName.c_str(),data);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAddPointCloud(luaWrap_lua_State* L)
{ // deprecated on 23.11.2020
    TRACE_LUA_API;
    LUA_START("sim.addPointCloud");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,0,lua_arg_number,3))
    {
        int pageMask=luaToInt(L,1);
        int layerMask=luaToInt(L,2);
        int objectHandle=luaToInt(L,3);
        int options=luaToInt(L,4);
        int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
        if ( (it->getScriptType()==sim_scripttype_mainscript)||(it->getScriptType()==sim_scripttype_childscript) )
            options=(options|1)-1; // cloud is automatically removed at the end of the simulation (i.e. is not persistent)
        double pointSize=luaToDouble(L,5);
        int pointCnt=(int)luaWrap_lua_rawlen(L,6)/3;
        std::vector<double> pointCoordinates(pointCnt*3,0.0);
        getDoublesFromTable(L,6,pointCnt*3,&pointCoordinates[0]);
        int res;
        res=checkOneGeneralInputArgument(L,7,lua_arg_number,12,true,true,&errorString);
        if (res>=0)
        {
            unsigned char* defaultColors=nullptr;
            std::vector<unsigned char> _defCols(12,0);
            if (res==2)
            {
                std::vector<int> _defCols_(12,0);
                getIntsFromTable(L,7,12,&_defCols_[0]);
                for (int i=0;i<12;i++)
                    _defCols[i]=(unsigned char)_defCols_[i];
                defaultColors=&_defCols[0];
            }
            res=checkOneGeneralInputArgument(L,8,lua_arg_number,pointCnt*3,true,true,&errorString);
            if (res>=0)
            {
                unsigned char* pointColors=nullptr;
                std::vector<unsigned char> _pointCols;
                if (res==2)
                {
                    _pointCols.resize(pointCnt*3,0);
                    std::vector<int> _pointCols_(pointCnt*3,0);
                    getIntsFromTable(L,8,pointCnt*3,&_pointCols_[0]);
                    for (int i=0;i<pointCnt*3;i++)
                        _pointCols[i]=(unsigned char)_pointCols_[i];
                    pointColors=&_pointCols[0];
                }
                res=checkOneGeneralInputArgument(L,9,lua_arg_number,pointCnt*3,true,true,&errorString);
                if (res>=0)
                {
                    double* pointNormals=nullptr;
                    std::vector<double> _pointNormals;
                    if (res==2)
                    {
                        _pointNormals.resize(pointCnt*3,0);
                        getDoublesFromTable(L,9,pointCnt*3,&_pointNormals[0]);
                        pointNormals=&_pointNormals[0];
                    }
                    retVal=simAddPointCloud_internal(pageMask,layerMask,objectHandle,options,pointSize,(int)pointCoordinates.size()/3,&pointCoordinates[0],(char*)defaultColors,(char*)pointColors,pointNormals);
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simModifyPointCloud(luaWrap_lua_State* L)
{ // deprecated on 23.11.2020
    TRACE_LUA_API;
    LUA_START("sim.modifyPointCloud");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int pointCloudHandle=luaToInt(L,1);
        int operation=luaToInt(L,2);
        retVal=simModifyPointCloud_internal(pointCloudHandle,operation,nullptr,nullptr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simCopyMatrix(luaWrap_lua_State* L)
{ // deprecated on 23.11.2020
    TRACE_LUA_API;
    LUA_START("sim.copyMatrix");

    if (checkInputArguments(L,&errorString,lua_arg_number,12))
    {
        double arr[12];
        getDoublesFromTable(L,1,12,arr);
        pushDoubleTableOntoStack(L,12,arr);
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetObjectInt32Parameter(luaWrap_lua_State* L)
{ // deprecated on 22.04.2021
    TRACE_LUA_API;
    LUA_START("sim.getObjectInt32Parameter");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int param;
        retVal=simGetObjectInt32Param_internal(luaToInt(L,1),luaToInt(L,2),&param);
        if (retVal>0)
        {
            luaWrap_lua_pushinteger(L,retVal);
            luaWrap_lua_pushinteger(L,param);
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectFloatParameter(luaWrap_lua_State* L)
{ // deprecated on 22.04.2021
    TRACE_LUA_API;
    LUA_START("sim.getObjectFloatParameter");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        double param;
        retVal=simGetObjectFloatParam_internal(luaToInt(L,1),luaToInt(L,2),&param);
        if (retVal>0)
        {
            luaWrap_lua_pushinteger(L,retVal);
            luaWrap_lua_pushnumber(L,param);
            LUA_END(2);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simIsHandleValid(luaWrap_lua_State* L)
{ // deprecated on 23.04.2021
    TRACE_LUA_API;
    LUA_START("sim.isHandleValid");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int objType=-1;
        int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                objType=luaToInt(L,2);
            retVal=simIsHandle_internal(luaToInt(L,1),objType);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectName(luaWrap_lua_State* L)
{ // deprecated on 08.06.2021
    TRACE_LUA_API;
    LUA_START("sim.getObjectName");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        char* name=simGetObjectName_internal(luaToInt(L,1));
        if (name!=nullptr)
        {
            luaWrap_lua_pushstring(L,name);
            simReleaseBuffer_internal(name);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectName(luaWrap_lua_State* L)
{ // deprecated on 08.06.2021
    TRACE_LUA_API;
    LUA_START("sim.setObjectName");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
        retVal=simSetObjectName_internal(luaToInt(L,1),luaWrap_lua_tostring(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetScriptName(luaWrap_lua_State* L)
{ // deprecated on 08.06.2021
    TRACE_LUA_API;
    LUA_START("sim.getScriptName");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int a=luaToInt(L,1);
        if (a==sim_handle_self)
            a=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        char* name=simGetScriptName_internal(a);
        if (name!=nullptr)
        {
            luaWrap_lua_pushstring(L,name);
            simReleaseBuffer_internal(name);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetScriptVariable(luaWrap_lua_State* L)
{ // deprecated on 16.06.2021
    TRACE_LUA_API;
    LUA_START("sim.setScriptVariable");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0))
    {
        std::string varAndScriptName(luaWrap_lua_tostring(L,1));
        int scriptHandleOrType=luaToInt(L,2);
        int numberOfArguments=luaWrap_lua_gettop(L);
        if (numberOfArguments>=3)
        {
            CInterfaceStack* stack=App::worldContainer->interfaceStackContainer->createStack();
            CScriptObject::buildFromInterpreterStack_lua(L,stack,3,1);
            retVal=simSetScriptVariable_internal(scriptHandleOrType,varAndScriptName.c_str(),stack->getId());
            App::worldContainer->interfaceStackContainer->destroyStack(stack);
        }
        else
            errorString=SIM_ERROR_FUNCTION_REQUIRES_MORE_ARGUMENTS;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectAssociatedWithScript(luaWrap_lua_State* L)
{ // deprecated on 18.06.2021
    TRACE_LUA_API;
    LUA_START("sim.getObjectAssociatedWithScript");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int a=luaToInt(L,1);
        if (a==sim_handle_self)
            a=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        retVal=simGetObjectAssociatedWithScript_internal(a);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetScriptAssociatedWithObject(luaWrap_lua_State* L)
{ // deprecated on 18.06.2021
    TRACE_LUA_API;
    LUA_START("sim.getScriptAssociatedWithObject");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetScriptAssociatedWithObject_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetCustomizationScriptAssociatedWithObject(luaWrap_lua_State* L)
{ // deprecated on 18.06.2021
    TRACE_LUA_API;
    LUA_START("sim.getCustomizationScriptAssociatedWithObject");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simGetCustomizationScriptAssociatedWithObject_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectConfiguration(luaWrap_lua_State* L)
{ // deprecated on 18.06.2021
    TRACE_LUA_API;
    LUA_START("sim.getObjectConfiguration");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        char* data=simGetObjectConfiguration_internal(luaToInt(L,1));
        if (data!=nullptr)
        {
            int dataSize=((int*)data)[0]+4;
            luaWrap_lua_pushlstring(L,data,dataSize);
            delete[] data;
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetObjectConfiguration(luaWrap_lua_State* L)
{ // deprecated on 18.06.2021
    TRACE_LUA_API;
    LUA_START("sim.setObjectConfiguration");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        size_t l;
        const char* data=luaWrap_lua_tolstring(L,1,&l);
        retVal=simSetObjectConfiguration_internal(data);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetConfigurationTree(luaWrap_lua_State* L)
{ // deprecated on 18.06.2021
    TRACE_LUA_API;
    LUA_START("sim.getConfigurationTree");

    CScriptObject* it=App::worldContainer->getScriptFromHandle(CScriptObject::getScriptHandleFromInterpreterState_lua(L));
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int id=luaToInt(L,1);
        if (id==sim_handle_self)
        {
            int objID=it->getObjectHandleThatScriptIsAttachedTo(sim_scripttype_childscript);
            id=objID;
            if (id==-1)
                errorString=SIM_ERROR_ARGUMENT_VALID_ONLY_WITH_CHILD_SCRIPTS;
        }
        if (id!=-1)
        {
            char* data=simGetConfigurationTree_internal(id);
            if (data!=nullptr)
            {
                int dataSize=((int*)data)[0]+4;
                luaWrap_lua_pushlstring(L,data,dataSize);
                delete[] data;
                LUA_END(1);
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetConfigurationTree(luaWrap_lua_State* L)
{ // deprecated on 18.06.2021
    TRACE_LUA_API;
    LUA_START("sim.setConfigurationTree");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        size_t l;
        const char* data=luaWrap_lua_tolstring(L,1,&l);
        retVal=simSetConfigurationTree_internal(data);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetObjectSizeValues(luaWrap_lua_State* L)
{ // deprecated on 28.06.2021
    TRACE_LUA_API;
    LUA_START("sim.setObjectSizeValues");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,3))
    {
        int handle=luaToInt(L,1);
        double s[3];
        getDoublesFromTable(L,2,3,s);
        retVal=simSetObjectSizeValues_internal(handle,s);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectSizeValues(luaWrap_lua_State* L)
{ // deprecated on 28.06.2021
    TRACE_LUA_API;
    LUA_START("sim.getObjectSizeValues");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        double s[3];
        if (simGetObjectSizeValues_internal(handle,s)!=-1)
        {
            pushDoubleTableOntoStack(L,3,s);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simOpenModule(luaWrap_lua_State* L)
{ // deprecated in 2019-2020 sometimes
    TRACE_LUA_API;
    LUA_START("sim.openModule");

    moduleCommonPart_old(L,sim_message_eventcallback_moduleopen,&errorString);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(1);
}
int _simCloseModule(luaWrap_lua_State* L)
{ // deprecated in 2019-2020 sometimes
    TRACE_LUA_API;
    LUA_START("sim.closeModule");

    moduleCommonPart_old(L,sim_message_eventcallback_moduleclose,&errorString);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(1);
}
int _simHandleModule(luaWrap_lua_State* L)
{ // deprecated in 2019-2020 sometimes
    TRACE_LUA_API;
    LUA_START_NO_CSIDE_ERROR("sim.handleModule");
    bool sensingPart=false;
    int res=checkOneGeneralInputArgument(L,2,lua_arg_bool,0,true,false,&errorString);

    if ( (res==0)||(res==2) )
    {
        if (res==2)
            sensingPart=luaToBool(L,2);
        if (sensingPart)
            moduleCommonPart_old(L,sim_message_eventcallback_modulehandleinsensingpart,&errorString);
        else
            moduleCommonPart_old(L,sim_message_eventcallback_modulehandle,&errorString);
    }
    else
        luaWrap_lua_pushinteger(L,-1);

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(1);
}
void moduleCommonPart_old(luaWrap_lua_State* L,int action,std::string* errorString)
{ // deprecated in 2019-2020 sometimes
    TRACE_LUA_API;
    std::string functionName;
    if (action==sim_message_eventcallback_moduleopen)
        functionName="sim.openModule";
    if (action==sim_message_eventcallback_moduleclose)
        functionName="sim.closeModule";
    if ( (action==sim_message_eventcallback_modulehandle)||(action==sim_message_eventcallback_modulehandleinsensingpart) )
        functionName="sim.handleModule";
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (it->getScriptType()!=sim_scripttype_mainscript)
    {
        if (errorString!=nullptr)
            errorString->assign(SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT);
        luaWrap_lua_pushinteger(L,-1);
    }
    else
    {
        bool handleAll=false;
        if (luaWrap_lua_isnumber(L,1))
        { // We try to check whether we have sim_handle_all as a number:
            if (luaToInt(L,1)==sim_handle_all)
            {
                handleAll=true;
                void* retVal=CPluginContainer::sendEventCallbackMessageToAllPlugins(action,nullptr,nullptr,nullptr);
                delete[] ((char*)retVal);
                luaWrap_lua_pushinteger(L,1);
            }
        }
        if (!handleAll)
        {
            if (checkInputArguments(L,errorString,lua_arg_string,0))
            {
                std::string modName(luaWrap_lua_tostring(L,1));
                void* retVal=CPluginContainer::sendEventCallbackMessageToAllPlugins(action,nullptr,(char*)modName.c_str(),nullptr);
                delete[] ((char*)retVal);
                luaWrap_lua_pushinteger(L,1);
            }
            else
                luaWrap_lua_pushinteger(L,-1);
        }
    }
}
int _simGetLastError(luaWrap_lua_State* L)
{ // deprecated on 01.07.2021
    TRACE_LUA_API;
    LUA_START("sim.getLastError");

    int scriptHandle=-1;
    if (luaWrap_lua_gettop(L)!=0)
    {
        if (checkInputArguments(L,&errorString,lua_arg_number,0))
            scriptHandle=luaToInt(L,1);
    }
    else
        scriptHandle=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(scriptHandle);
    if (it!=nullptr)
    {
        luaWrap_lua_pushstring(L,it->getAndClearLastError_old().c_str());
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}
int _simSwitchThread(luaWrap_lua_State* L)
{ // now implemented in Lua, except for old threads. Deprecated since V4.2.0
    TRACE_LUA_API;
    LUA_START("sim._switchThread");

    int retVal=-1;
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
    if ((it!=nullptr)&&(it->canManualYield()))
    {
        it->resetScriptExecutionTime();
        if (CThreadPool_old::switchBackToPreviousThread())
            retVal=1;
        else
            retVal=0;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}
int _simFileDialog(luaWrap_lua_State* L)
{ // deprecated
    TRACE_LUA_API;
    LUA_START("sim.fileDialog");
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0,lua_arg_string,0,lua_arg_string,0,lua_arg_string,0,lua_arg_string,0))
    {
        int mode=luaToInt(L,1);
        std::string title(luaWrap_lua_tostring(L,2));
        std::string startPath(luaWrap_lua_tostring(L,3));
        std::string initName(luaWrap_lua_tostring(L,4));
        std::string extName(luaWrap_lua_tostring(L,5));
        std::string ext(luaWrap_lua_tostring(L,6));
        char* pathAndName=simFileDialog_internal(mode,title.c_str(),startPath.c_str(),initName.c_str(),extName.c_str(),ext.c_str());
        if (pathAndName!=nullptr)
        {
            luaWrap_lua_pushstring(L,pathAndName);
            simReleaseBuffer_internal(pathAndName);
            LUA_END(1);
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}
int _simMsgBox(luaWrap_lua_State* L)
{ // deprecated
    TRACE_LUA_API;
    LUA_START("sim.msgBox");
    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_string,0,lua_arg_string,0))
    {
        int dlgType=luaToInt(L,1);
        int dlgButtons=luaToInt(L,2);
        std::string title(luaWrap_lua_tostring(L,3));
        std::string message(luaWrap_lua_tostring(L,4));
        retVal=simMsgBox_internal(dlgType,dlgButtons,title.c_str(),message.c_str());
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simIsObjectInSelection(luaWrap_lua_State* L)
{ // deprecated since 24.09.2021
    TRACE_LUA_API;
    LUA_START("sim.isObjectInSelection");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simIsObjectInSelection_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simAddObjectToSelection(luaWrap_lua_State* L)
{ // deprecated since 24.09.2021
    TRACE_LUA_API;
    LUA_START("sim.addObjectToSelection");

    int retVal=-1;// error
    // We check if argument 1 is nil (special case):
    if (checkOneGeneralInputArgument(L,1,lua_arg_nil,0,false,true,nullptr)==1) // we do not generate an error message!
    {
        retVal=1; // nothing happens
    }
    else
    {
        // We check if we have a table at position 1:
        if (!luaWrap_lua_istable(L,1))
        { // It is not a table!
            if (checkInputArguments(L,nullptr,lua_arg_number,0,lua_arg_number,0)) // we don't generate an error
                retVal=simAddObjectToSelection_internal(luaToInt(L,1),luaToInt(L,2));
            else
            { // Maybe we have a special case with one argument only?
                // nil is a valid argument!
                if (checkInputArguments(L,nullptr,lua_arg_nil,0)) // we don't generate an error
                    retVal=1;
                else
                {
                    if (checkInputArguments(L,&errorString,lua_arg_number,0))
                    {
                        if (luaToInt(L,1)==sim_handle_all)
                            retVal=simAddObjectToSelection_internal(luaToInt(L,1),-1);
                        else
                            checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0); // we just generate an error
                    }
                }
            }
        }
        else
        { // Ok we have a table. Now what size is it?
            int tableLen=int(luaWrap_lua_rawlen(L,1));
            int* buffer=new int[tableLen];
            if (getIntsFromTable(L,1,tableLen,buffer))
            {
                for (int i=0;i<tableLen;i++)
                {
                    if (App::currentWorld->sceneObjects->getObjectFromHandle(buffer[i])!=nullptr)
                        App::currentWorld->sceneObjects->addObjectToSelection(buffer[i]);
                }
                retVal=1;
            }
            else
                errorString=SIM_ERROR_TABLE_CONTAINS_INVALID_TYPES;
            delete[] buffer;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simRemoveObjectFromSelection(luaWrap_lua_State* L)
{ // deprecated since 24.09.2021
    TRACE_LUA_API;
    LUA_START("sim.removeObjectFromSelection");

    int retVal=-1;// error
    // We check if argument 1 is nil (special case):
    if (checkOneGeneralInputArgument(L,1,lua_arg_nil,0,false,true,nullptr)==1) // we do not generate an error message!
        retVal=1; // nothing happens
    else
    {
        // We check if we have a table at position 1:
        if (!luaWrap_lua_istable(L,1))
        { // It is not a table!
            if (checkInputArguments(L,nullptr,lua_arg_number,0,lua_arg_number,0)) // we don't generate an error
                retVal=simRemoveObjectFromSelection_internal(luaToInt(L,1),luaToInt(L,2));
            else
            {
                if (checkInputArguments(L,&errorString,lua_arg_number,0))
                {
                    if (luaToInt(L,1)==sim_handle_all)
                        retVal=simRemoveObjectFromSelection_internal(luaToInt(L,1),-1);
                    else
                        checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0); // we just generate an error
                }
            }
        }
        else
        { // Ok we have a table. Now what size is it?
            int tableLen=int(luaWrap_lua_rawlen(L,1));
            int* buffer=new int[tableLen];
            if (getIntsFromTable(L,1,tableLen,buffer))
            {
                for (int i=0;i<tableLen;i++)
                    retVal=simRemoveObjectFromSelection_internal(sim_handle_single,buffer[i]);
            }
            else
                errorString=SIM_ERROR_TABLE_CONTAINS_INVALID_TYPES;
            delete[] buffer;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetObjectUniqueIdentifier(luaWrap_lua_State* L)
{ // deprecated since 08.10.2021
    TRACE_LUA_API;
    LUA_START("sim.getObjectUniqueIdentifier");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int handle=luaToInt(L,1);
        if (handle!=sim_handle_all)
        {
            int retVal;
            if (simGetObjectUniqueIdentifier_internal(handle,&retVal)!=-1)
            {
                luaWrap_lua_pushinteger(L,retVal);
                LUA_END(1);
            }
        }
        else
        { // for backward compatibility
            int cnt=int(App::currentWorld->sceneObjects->getObjectCount());
            int* buffer=new int[cnt];
            if (simGetObjectUniqueIdentifier_internal(handle,buffer)!=-1)
            {
                pushIntTableOntoStack(L,cnt,buffer);
                delete[] buffer;
                LUA_END(1);
            }
            delete[] buffer;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simBreakForceSensor(luaWrap_lua_State* L)
{ // deprecated since 08.11.2021
    TRACE_LUA_API;
    LUA_START("sim.breakForceSensor");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
        retVal=simBreakForceSensor_internal(luaToInt(L,1));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetSphericalJointMatrix(luaWrap_lua_State* L)
{ // deprecated since 09.11.2021
    TRACE_LUA_API;
    LUA_START("sim.setSphericalJointMatrix");

    int retVal=-1; // error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,12))
    {
        double arr[12];
        getDoublesFromTable(L,2,12,arr);
        retVal=simSetSphericalJointMatrix_internal(luaToInt(L,1),arr);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetJointMatrix(luaWrap_lua_State* L)
{ // deprecated since 09.11.2021
    TRACE_LUA_API;
    LUA_START("sim.getJointMatrix");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double arr[12];
        if (simGetJointMatrix_internal(luaToInt(L,1),arr)==1)
        {
            pushDoubleTableOntoStack(L,12,arr); // Success
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _sim_getObjectHandle(luaWrap_lua_State* L)
{ // deprecated on 03.12.2021
    TRACE_LUA_API;
    LUA_START("sim._getObjectHandle");

    int retVal=-1; // means error

    bool checkWithString=true;
    if (checkInputArguments(L,nullptr,lua_arg_integer,0)) // do not output error if not string
    { // argument sim.handle_self
        if (luaToInt(L,1)==sim_handle_self)
        {
            checkWithString=false;
            int a=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
            retVal=simGetObjectAssociatedWithScript_internal(a);
        }
    }
    if (checkWithString)
    {
        if (checkInputArguments(L,&errorString,lua_arg_string,0))
        {
            int index=-1;
            int res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
            if (res>=0)
            {
                if (res==2)
                    index=luaToInt(L,2);
                int proxyForSearch=-1;
                res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
                if (res>=0)
                {
                    if (res==2)
                        proxyForSearch=luaToInt(L,3);

                    int options=0;
                    res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
                    if (res>=0)
                    {
                        if (res==2)
                            options=luaToInt(L,4);
                        std::string name(luaWrap_lua_tostring(L,1));
                        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
                        retVal=simGetObjectHandleEx_internal(name.c_str(),index,proxyForSearch,options);
                        setCurrentScriptInfo_cSide(-1,-1);
                    }
                }
            }
        }
    }
    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetScriptAttribute(luaWrap_lua_State* L)
{ // deprecated on 05.01.2022
    TRACE_LUA_API;
    LUA_START("sim.setScriptAttribute");

    int retVal=-1;
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int scriptID=luaToInt(L,1);
        if (scriptID==sim_handle_self)
            scriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        int attribID=luaToInt(L,2);
        int thirdArgType=lua_arg_number;
        if ( (attribID==sim_customizationscriptattribute_activeduringsimulation)||(attribID==sim_childscriptattribute_automaticcascadingcalls)||(attribID==sim_scriptattribute_enabled)||(attribID==sim_customizationscriptattribute_cleanupbeforesave) )
            thirdArgType=lua_arg_bool;


        if ( (attribID==sim_scriptattribute_executionorder)||(attribID==sim_scriptattribute_executioncount)||(attribID==sim_scriptattribute_debuglevel) )
            thirdArgType=lua_arg_number;
        int res=checkOneGeneralInputArgument(L,3,thirdArgType,0,false,false,&errorString);
        if (res==2)
        {
            if ( (attribID==sim_customizationscriptattribute_activeduringsimulation)||(attribID==sim_childscriptattribute_automaticcascadingcalls)||(attribID==sim_scriptattribute_enabled)||(attribID==sim_customizationscriptattribute_cleanupbeforesave) )
                retVal=simSetScriptAttribute_internal(scriptID,attribID,0.0,luaToBool(L,3));
            if ( (attribID==sim_scriptattribute_executionorder)||(attribID==sim_scriptattribute_executioncount)||(attribID==sim_scriptattribute_debuglevel) )
                retVal=simSetScriptAttribute_internal(scriptID,attribID,0.0,luaToInt(L,3));
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetScriptAttribute(luaWrap_lua_State* L)
{ // deprecated on 05.01.2022
    TRACE_LUA_API;
    LUA_START("sim.getScriptAttribute");

    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
    {
        int scriptID=luaToInt(L,1);
        if (scriptID==sim_handle_self)
            scriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        int attribID=luaToInt(L,2);
        int intVal;
        double floatVal;
        int result=simGetScriptAttribute_internal(scriptID,attribID,&floatVal,&intVal);
        if (result!=-1)
        {
            if ( (attribID==sim_customizationscriptattribute_activeduringsimulation)||(attribID==sim_childscriptattribute_automaticcascadingcalls)||(attribID==sim_scriptattribute_enabled)||(attribID==sim_customizationscriptattribute_cleanupbeforesave) )
                luaWrap_lua_pushboolean(L,intVal);
            if ( (attribID==sim_scriptattribute_executionorder)||(attribID==sim_scriptattribute_executioncount)||(attribID==sim_scriptattribute_scripttype)||(attribID==sim_scriptattribute_scripthandle) )
                luaWrap_lua_pushinteger(L,intVal);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetScriptText(luaWrap_lua_State* L)
{ // deprecated on 04.02.2022
    TRACE_LUA_API;
    LUA_START("sim.setScriptText");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_string,0))
    {
        int scriptHandle=luaToInt(L,1);
        std::string scriptText(luaWrap_lua_tostring(L,2));
        retVal=simSetScriptText_internal(scriptHandle,scriptText.c_str());
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetJointMaxForce(luaWrap_lua_State* L)
{ // deprecated on 24.02.2022
    TRACE_LUA_API;
    LUA_START("sim.getJointMaxForce");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        double jointF[1];
        if (simGetJointMaxForce_internal(luaToInt(L,1),jointF)>0)
        {
            luaWrap_lua_pushnumber(L,jointF[0]);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetJointMaxForce(luaWrap_lua_State* L)
{ // deprecated on 24.02.2022
    TRACE_LUA_API;
    LUA_START("sim.setJointMaxForce");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0))
        retVal=simSetJointMaxForce_internal(luaToInt(L,1),luaToDouble(L,2));

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simRemoveObject(luaWrap_lua_State* L)
{ // deprecated on 07.03.2022
    TRACE_LUA_API;
    LUA_START("sim.removeObject");

    int retVal=-1;// error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int objId=luaToInt(L,1);
        int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
        CScriptObject* it=App::worldContainer->getScriptFromHandle(currentScriptID);
        if (!it->getThreadedExecution_oldThreads())
            retVal=simRemoveObject_internal(objId);
        else
        { // this script runs threaded and wants to destroy another object (than itself probably). We need to make sure that it will only destroy objects that do not have any scripts attached with a non-nullptr lua state:
            std::vector<CScriptObject*> scripts;
            App::currentWorld->embeddedScriptContainer->getScriptsFromObjectAttachedTo(objId,scripts);
            bool ok=true;
            for (size_t i=0;i<scripts.size();i++)
            {
                if ( (it!=scripts[i])&&scripts[i]->hasInterpreterState() )
                    ok=false;
            }
            if (ok)
                retVal=simRemoveObject_internal(objId);
            else
                errorString=SIM_ERROR_THREADED_SCRIPT_DESTROYING_OBJECTS_WITH_ACTIVE_SCRIPTS;
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetSystemTimeInMs(luaWrap_lua_State* L)
{ // deprecated on 01.04.2022
    TRACE_LUA_API;
    LUA_START("sim.getSystemTimeInMs");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int lastTime=luaToInt(L,1);
        luaWrap_lua_pushinteger(L,simGetSystemTimeInMs_internal(lastTime));
        LUA_END(1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetVisionSensorResolution(luaWrap_lua_State* L)
{ // deprecated on 11.04.2022
    TRACE_LUA_API;
    LUA_START("sim.getVisionSensorResolution");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int resolution[2];
        if (simGetVisionSensorRes_internal(luaToInt(L,1),resolution)==1)
        {
            pushIntTableOntoStack(L,2,resolution);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simGetVisionSensorImage(luaWrap_lua_State* L)
{ // deprecated on 11.04.2022
    TRACE_LUA_API;
    LUA_START("sim.getVisionSensorImage");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int arg1=luaToInt(L,1);
        int handleFlags=arg1&0xff00000;
        int objectHandle=arg1&0xfffff;
        int valPerPix=3;
        int rgbOrGreyOrDepth=0;
        if ((handleFlags&sim_handleflag_greyscale)!=0)
        {
            valPerPix=1;
            rgbOrGreyOrDepth=1;
        }
        int posX=0;
        int posY=0;
        int sizeX=0;
        int sizeY=0;
        int retType=0;
        // Now check the optional arguments:
        int res;
        res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                posX=luaToInt(L,2);
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    posY=luaToInt(L,3);
                res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        sizeX=luaToInt(L,4);
                    res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,true,false,&errorString);
                    if ((res==0)||(res==2))
                    {
                        if (res==2)
                            sizeY=luaToInt(L,5);
                        res=checkOneGeneralInputArgument(L,6,lua_arg_number,0,true,false,&errorString);
                        if ((res==0)||(res==2))
                        {
                            if (res==2)
                                retType=luaToInt(L,6);
                            CVisionSensor* rs=App::currentWorld->sceneObjects->getVisionSensorFromHandle(objectHandle);
                            if (rs!=nullptr)
                            {
                                if ( (sizeX==0)&&(sizeY==0) )
                                { // we have default values here (the whole picture)
                                    int reso[2];
                                    rs->getResolution(reso);
                                    sizeX=reso[0];
                                    sizeY=reso[1];
                                }
                                float* buffer=rs->readPortionOfImage(posX,posY,sizeX,sizeY,rgbOrGreyOrDepth);
                                if (buffer!=nullptr)
                                {
                                    if (retType==0)
                                        pushFloatTableOntoStack(L,sizeX*sizeY*valPerPix,buffer);
                                    else
                                    { // here we return RGB data in a string
                                        char* str=new char[sizeX*sizeY*valPerPix];
                                        int vvv=sizeX*sizeY*valPerPix;
                                        for (int i=0;i<vvv;i++)
                                            str[i]=char(buffer[i]*255.0001);
                                        luaWrap_lua_pushlstring(L,(const char*)str,vvv);
                                        delete[] ((char*)str);
                                    }
                                    delete[] ((char*)buffer);
                                    LUA_END(1);
                                }
                                else
                                    errorString=SIM_ERROR_INVALID_ARGUMENTS;
                            }
                            else
                                errorString=SIM_ERROR_VISION_SENSOR_INEXISTANT;
                        }
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetVisionSensorImage(luaWrap_lua_State* L)
{ // deprecated on 11.04.2022
    TRACE_LUA_API;
    LUA_START("sim.setVisionSensorImage");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int arg1=luaToInt(L,1);
        int handleFlags=arg1&0xff00000;
        int sensHandle=arg1&0xfffff;
        int valPerPix=3;
        bool noProcessing=false;
        bool setDepthBufferInstead=false;
        if ((handleFlags&sim_handleflag_greyscale)!=0)
            valPerPix=1;
        if ((handleFlags&sim_handleflag_rawvalue)!=0)
            noProcessing=true;
        if ((handleFlags&sim_handleflag_depthbuffer)!=0)
            setDepthBufferInstead=true;
        CSceneObject* it=App::currentWorld->sceneObjects->getObjectFromHandle(sensHandle);
        if (it!=nullptr)
        { // Ok we have a valid object
            if (it->getObjectType()==sim_object_visionsensor_type)
            { // ok we have a valid vision sensor
                int res[2];
                CVisionSensor* rendSens=(CVisionSensor*)it;
                rendSens->getResolution(res);
                // We check if we have a table or string at position 2:
                bool notTableNorString=true;
                if (luaWrap_lua_istable(L,2))
                { // Ok we have a table. Now what size is it?
                    notTableNorString=false;
                    if (setDepthBufferInstead)
                    {
                        // Now we check if the provided table has correct size:
                        if (int(luaWrap_lua_rawlen(L,2))>=res[0]*res[1])
                        {
                            float* img=new float[res[0]*res[1]];
                            getFloatsFromTable(L,2,res[0]*res[1],img);
                            rendSens->setDepthBuffer(img);
                            retVal=1;
                            delete[] img;
                        }
                        else
                            errorString=SIM_ERROR_ONE_TABLE_SIZE_IS_WRONG;
                    }
                    else
                    {
                        // Now we check if the provided table has correct size:
                        if (int(luaWrap_lua_rawlen(L,2))>=res[0]*res[1]*valPerPix)
                        {
                            float* img=new float[res[0]*res[1]*valPerPix];
                            getFloatsFromTable(L,2,res[0]*res[1]*valPerPix,img); // we do the operation directly without going through the c-api
                            if (rendSens->setExternalImage_old(img,valPerPix==1,noProcessing))
                                retVal=1;
                            delete[] img;
                        }
                        else
                            errorString=SIM_ERROR_ONE_TABLE_SIZE_IS_WRONG;
                    }
                }
                if (luaWrap_lua_isstring(L,2))
                { // Ok we have a string. Now what size is it?
                    notTableNorString=false;
                    // Now we check if the provided string has correct size:
                    size_t dataLength;
                    char* data=(char*)luaWrap_lua_tolstring(L,2,&dataLength);
                    if (setDepthBufferInstead)
                    {
                        if (int(dataLength)>=res[0]*res[1]*sizeof(float))
                        {
                            rendSens->setDepthBuffer((float*)data);
                            retVal=1;
                        }
                        else
                            errorString=SIM_ERROR_INCORRECT_BUFFER_SIZE;
                    }
                    else
                    {
                        if (int(dataLength)>=res[0]*res[1]*valPerPix)
                        {
                            float* img=new float[res[0]*res[1]*valPerPix];
                            for (int i=0;i<res[0]*res[1]*valPerPix;i++)
                                img[i]=float(data[i])/255.0;
                            if (rendSens->setExternalImage_old(img,valPerPix==1,noProcessing))
                                retVal=1;
                            delete[] img;
                        }
                        else
                            errorString=SIM_ERROR_INCORRECT_BUFFER_SIZE;
                    }
                }
                if (notTableNorString)
                    errorString=SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG;
            }
            else
                errorString=SIM_ERROR_OBJECT_NOT_VISION_SENSOR;
        }
        else
            errorString=SIM_ERROR_OBJECT_INEXISTANT;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetVisionSensorCharImage(luaWrap_lua_State* L)
{ // deprecated on 11.04.2022
    TRACE_LUA_API;
    LUA_START("sim.getVisionSensorCharImage");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int arg1=luaToInt(L,1);
        int handleFlags=arg1&0xff00000;
        int objectHandle=arg1&0xfffff;
        int valPerPix=3;
        if ((handleFlags&sim_handleflag_greyscale)!=0)
            valPerPix=1;
        int posX=0;
        int posY=0;
        int sizeX=0;
        int sizeY=0;
        double rgbaCutOff=0.0;
        // Now check the optional arguments:
        int res;
        res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                posX=luaToInt(L,2);
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    posY=luaToInt(L,3);
                res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        sizeX=luaToInt(L,4);
                    res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,true,false,&errorString);
                    if ((res==0)||(res==2))
                    {
                        if (res==2)
                            sizeY=luaToInt(L,5);
                        res=checkOneGeneralInputArgument(L,6,lua_arg_number,0,true,false,&errorString);
                        if ((res==0)||(res==2))
                        {
                            if (res==2)
                                rgbaCutOff=luaToDouble(L,6);
                            CVisionSensor* rs=App::currentWorld->sceneObjects->getVisionSensorFromHandle(objectHandle);
                            if (rs!=nullptr)
                            {
                                int reso[2];
                                rs->getResolution(reso);
                                if ( (sizeX==0)&&(sizeY==0) )
                                { // we have default values here (the whole picture)
                                    sizeX=reso[0];
                                    sizeY=reso[1];
                                }
                                int options=0;
                                if (valPerPix==1)
                                    options=options|1;
                                if (rgbaCutOff>0.0)
                                    options=options|2;
                                unsigned char* buffer=rs->readPortionOfCharImage(posX,posY,sizeX,sizeY,rgbaCutOff,valPerPix==1);
                                if (buffer!=nullptr)
                                {
                                    int vvv=sizeX*sizeY*valPerPix;
                                    if (rgbaCutOff>0.0)
                                    {
                                        if (valPerPix==1)
                                            vvv=sizeX*sizeY*2;
                                        else
                                            vvv=sizeX*sizeY*4;
                                    }
                                    luaWrap_lua_pushlstring(L,(const char*)buffer,vvv);
                                    delete[] ((char*)buffer);
                                    luaWrap_lua_pushinteger(L,reso[0]);
                                    luaWrap_lua_pushinteger(L,reso[1]);
                                    LUA_END(3);
                                }
                                else
                                    errorString=SIM_ERROR_INVALID_ARGUMENTS;
                            }
                            else
                                errorString=SIM_ERROR_VISION_SENSOR_INEXISTANT;
                        }
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simSetVisionSensorCharImage(luaWrap_lua_State* L)
{ // deprecated on 11.04.2022
    TRACE_LUA_API;
    LUA_START("sim.setVisionSensorCharImage");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int arg1=luaToInt(L,1);
        int handleFlags=arg1&0xff00000;
        int sensHandle=arg1&0xfffff;
        int valPerPix=3;
        if ((handleFlags&sim_handleflag_greyscale)!=0)
            valPerPix=1;
        bool noProcessing=false;
        if ((handleFlags&sim_handleflag_rawvalue)!=0)
            noProcessing=true;
        CSceneObject* it=App::currentWorld->sceneObjects->getObjectFromHandle(sensHandle);
        if (it!=nullptr)
        { // Ok we have a valid object
            if (it->getObjectType()==sim_object_visionsensor_type)
            { // ok we have a valid vision sensor
                int res[2];
                CVisionSensor* rendSens=(CVisionSensor*)it;
                rendSens->getResolution(res);
                // We check if we have a string at position 2:
                if (luaWrap_lua_isstring(L,2))
                { // Ok we have a string. Now what size is it?
                    // Now we check if the provided string has correct size:
                    size_t dataLength;
                    char* data=(char*)luaWrap_lua_tolstring(L,2,&dataLength);
                    if (int(dataLength)>=res[0]*res[1]*valPerPix)
                    {
                        if (rendSens->setExternalCharImage_old((unsigned char*)data,valPerPix==1,noProcessing))
                            retVal=1;
                    }
                    else
                        errorString=SIM_ERROR_INCORRECT_BUFFER_SIZE;
                }
                else
                    errorString=SIM_ERROR_ONE_ARGUMENT_TYPE_IS_WRONG;
            }
            else
                errorString=SIM_ERROR_OBJECT_NOT_VISION_SENSOR;
        }
        else
            errorString=SIM_ERROR_OBJECT_INEXISTANT;
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetVisionSensorDepthBuffer(luaWrap_lua_State* L)
{ // deprecated on 11.04.2022
    TRACE_LUA_API;
    LUA_START("sim.getVisionSensorDepthBuffer");

    if (checkInputArguments(L,&errorString,lua_arg_number,0))
    {
        int posX=0;
        int posY=0;
        int sizeX=0;
        int sizeY=0;
        // Now check the optional arguments:
        int res;
        res=checkOneGeneralInputArgument(L,2,lua_arg_number,0,true,false,&errorString);
        if ((res==0)||(res==2))
        {
            if (res==2)
                posX=luaToInt(L,2);
            res=checkOneGeneralInputArgument(L,3,lua_arg_number,0,true,false,&errorString);
            if ((res==0)||(res==2))
            {
                if (res==2)
                    posY=luaToInt(L,3);
                res=checkOneGeneralInputArgument(L,4,lua_arg_number,0,true,false,&errorString);
                if ((res==0)||(res==2))
                {
                    if (res==2)
                        sizeX=luaToInt(L,4);
                    res=checkOneGeneralInputArgument(L,5,lua_arg_number,0,true,false,&errorString);
                    if ((res==0)||(res==2))
                    {
                        if (res==2)
                            sizeY=luaToInt(L,5);
                        int sensHandle=luaToInt(L,1);
                        bool returnString=(sensHandle&sim_handleflag_codedstring)!=0;
                        bool toMeters=(sensHandle&sim_handleflag_depthbuffermeters)!=0;
                        sensHandle=sensHandle&0xfffff;
                        CVisionSensor* rs=App::currentWorld->sceneObjects->getVisionSensorFromHandle(sensHandle);
                        if (rs!=nullptr)
                        {
                            if ( (sizeX==0)&&(sizeY==0) )
                            { // we have default values here (the whole picture)
                                int reso[2];
                                rs->getResolution(reso);
                                sizeX=reso[0];
                                sizeY=reso[1];
                            }
                            float* buffer=rs->readPortionOfImage(posX,posY,sizeX,sizeY,2);
                            if (buffer!=nullptr)
                            {
                                if (toMeters)
                                { // Here we need to convert values to distances in meters:
                                    float n=(float)rs->getNearClippingPlane();
                                    float f=(float)rs->getFarClippingPlane();
                                    float fmn=f-n;
                                    for (int i=0;i<sizeX*sizeY;i++)
                                        buffer[i]=n+fmn*buffer[i];
                                }
                                if (returnString)
                                    luaWrap_lua_pushlstring(L,(char*)buffer,sizeX*sizeY*sizeof(float));
                                else
                                    pushFloatTableOntoStack(L,sizeX*sizeY,buffer);
                                delete[] ((char*)buffer);
                                LUA_END(1);
                            }
                            else
                                errorString=SIM_ERROR_INVALID_ARGUMENTS;
                        }
                        else
                            errorString=SIM_ERROR_VISION_SENSOR_INEXISTANT;
                    }
                }
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simCreatePureShape(luaWrap_lua_State* L)
{ // deprecated on 27.04.2022
    TRACE_LUA_API;
    LUA_START("sim.createPureShape");

    int retVal=-1; // means error
    if (checkInputArguments(L,&errorString,lua_arg_number,0,lua_arg_number,0,lua_arg_number,3,lua_arg_number,0))
    {
        int primitiveType=luaToInt(L,1);
        int options=luaToInt(L,2);
        double sizes[3];
        getDoublesFromTable(L,3,3,sizes);
        double mass=luaToDouble(L,4);
        int* precision=nullptr;
        int prec[2];
        int res=checkOneGeneralInputArgument(L,5,lua_arg_number,2,true,true,&errorString);
        if (res>=0)
        {
            if (res==2)
            {
                getIntsFromTable(L,5,2,prec);
                precision=prec;
            }
            retVal=simCreatePureShape_internal(primitiveType,options,sizes,mass,precision);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetScriptHandle(luaWrap_lua_State* L)
{ // deprecated on 06.05.2022
    TRACE_LUA_API;
    LUA_START("sim.getScriptHandle");

    int retVal=-1; // means error
    if (luaWrap_lua_gettop(L)==0)
        retVal=CScriptObject::getScriptHandleFromInterpreterState_lua(L); // no arguments, return itself
    else
    {
        if (checkInputArguments(L,nullptr,lua_arg_nil,0))
            retVal=CScriptObject::getScriptHandleFromInterpreterState_lua(L); // nil arg, return itself (back. compatibility)
        else
        {
            if (checkInputArguments(L,nullptr,lua_arg_integer,0))
            { // script type arg.
                int scriptType=luaToInt(L,1);
                if (scriptType!=sim_handle_self)
                {
                    int objectHandle=-1;
                    std::string scriptName;
                    if (scriptType==sim_scripttype_addonscript)
                    {
                        if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_string,0))
                            scriptName=luaWrap_lua_tostring(L,2);
                    }
                    if ( (scriptType==sim_scripttype_childscript)||(scriptType==sim_scripttype_customizationscript) )
                    {
                        if (checkInputArguments(L,nullptr,lua_arg_integer,0,lua_arg_integer,0))
                            objectHandle=luaToInt(L,2); // back compatibility actually
                        else
                        {
                            if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_string,0))
                            {
                                scriptName=luaWrap_lua_tostring(L,2);
                                if (scriptName.size()==0)
                                {
                                    if (checkInputArguments(L,&errorString,lua_arg_integer,0,lua_arg_string,0,lua_arg_integer,0))
                                        objectHandle=luaToInt(L,3);
                                }
                            }
                        }
                    }
                    if ( ( (scriptName.size()>0)||(objectHandle>=0) )||((scriptType==sim_scripttype_mainscript)||(scriptType==sim_scripttype_sandboxscript)) )
                    {
                        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
                        retVal=simGetScriptHandleEx_internal(scriptType,objectHandle,scriptName.c_str());
                        setCurrentScriptInfo_cSide(-1,-1);
                    }
                }
                else
                    retVal=CScriptObject::getScriptHandleFromInterpreterState_lua(L); // for backward compatibility
            }
            else
            { // string argument, for backward compatibility:
                if (checkInputArguments(L,nullptr,lua_arg_string,0))
                {
                    std::string name(luaWrap_lua_tostring(L,1));
                    setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
                    retVal=simGetScriptHandle_internal(name.c_str()); // deprecated func.
                    setCurrentScriptInfo_cSide(-1,-1);
                }
                else
                    checkInputArguments(L,&errorString,lua_arg_integer,0); // just generate an error
            }
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simHandleCustomizationScripts(luaWrap_lua_State* L)
{ // deprecated on 19.09.2022
    TRACE_LUA_API;
    LUA_START("sim.handleCustomizationScripts");

    int retVal=-1;
    int currentScriptID=CScriptObject::getScriptHandleFromInterpreterState_lua(L);
    CScriptObject* itScrObj=App::worldContainer->getScriptFromHandle(currentScriptID);
    if (itScrObj->getScriptType()==sim_scripttype_mainscript)
    {
        if (checkInputArguments(L,&errorString,lua_arg_number,0))
        {
            int callType=luaToInt(L,1);
            retVal=0;
            if (App::getEditModeType()==NO_EDIT_MODE)
            {
                retVal=App::currentWorld->embeddedScriptContainer->callChildAndEmbeddedScripts(sim_scripttype_customizationscript,callType,nullptr,nullptr);
                App::currentWorld->embeddedScriptContainer->removeDestroyedScripts(sim_scripttype_customizationscript);
            }
        }
    }
    else
        errorString=SIM_ERROR_CAN_ONLY_BE_CALLED_FROM_MAIN_SCRIPT;

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simSetDoubleSignal(luaWrap_lua_State* L)
{ // deprecated on 13.10.2022
    TRACE_LUA_API;
    LUA_START("sim.setDoubleSignal");

    int retVal=-1; //error
    if (checkInputArguments(L,&errorString,lua_arg_string,0,lua_arg_number,0))
    {
        setCurrentScriptInfo_cSide(CScriptObject::getScriptHandleFromInterpreterState_lua(L),CScriptObject::getScriptNameIndexFromInterpreterState_lua_old(L)); // for transmitting to the master function additional info (e.g.for autom. name adjustment, or for autom. object deletion when script ends)
        retVal=simSetDoubleSignalOld_internal(luaWrap_lua_tostring(L,1),luaToDouble(L,2));
        setCurrentScriptInfo_cSide(-1,-1);
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}

int _simGetDoubleSignal(luaWrap_lua_State* L)
{ // deprecated on 13.10.2022
    TRACE_LUA_API;
    LUA_START("sim.getDoubleSignal");

    if (checkInputArguments(L,&errorString,lua_arg_string,0))
    {
        double doubleVal;
        if (simGetDoubleSignalOld_internal(std::string(luaWrap_lua_tostring(L,1)).c_str(),&doubleVal)==1)
        {
            luaWrap_lua_pushnumber(L,doubleVal);
            LUA_END(1);
        }
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    LUA_END(0);
}

int _simClearDoubleSignal(luaWrap_lua_State* L)
{ // deprecated on 13.10.2022
    TRACE_LUA_API;
    LUA_START("sim.clearDoubleSignal");

    int retVal=-1; //error
    int res=checkOneGeneralInputArgument(L,1,lua_arg_string,0,true,true,&errorString);
    if (res>=0)
    {
        if (res!=2)
            retVal=simClearDoubleSignalOld_internal(nullptr); // actually deprecated. No need for that
        else
            retVal=simClearDoubleSignalOld_internal(luaWrap_lua_tostring(L,1));
    }

    LUA_RAISE_ERROR_OR_YIELD_IF_NEEDED(); // we might never return from this!
    luaWrap_lua_pushinteger(L,retVal);
    LUA_END(1);
}
