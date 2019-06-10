#include "HmdDeviceOpenVr.h"
#include "../SearchForDisplay.h"

#include "../../game/q_shared.h"

#include <cstring>
#include <string>
#include <iostream>
#include <cstdio>
#include <cmath>
#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <openvr.h>

#ifdef _WINDOWS
#define _USE_MATH_DEFINES
#include <math.h>
#include <windows.h>
#endif

using namespace std;
using namespace OpenVr;

HmdDeviceOpenVr::HmdDeviceOpenVr()
    :mIsInitialized(false)
    ,mUsingDebugHmd(false)
    ,mPositionTrackingEnabled(false)
	,mUseSeatedPosition(true)
    ,mIsRotated(false)
    ,mpHmd(nullptr)
    ,mTrackerIdHandLeft(k_unTrackedDeviceIndexInvalid)
    ,mTrackerIdHandRight(k_unTrackedDeviceIndexInvalid)
    ,mTrackableDeviceCount(0)
    ,mHeightAdjust(0.0f)
{

}

HmdDeviceOpenVr::~HmdDeviceOpenVr()
{

}

bool HmdDeviceOpenVr::Init(bool allowDummyDevice)
{
    if (mIsInitialized)
    {
        return true;
    }

    bool debugPrint = true;

    if (debugPrint)
    {
        printf("openvr init ...\n");
    }

    
    EVRInitError eError = VRInitError_None;
    mpHmd = VR_Init(&eError, VRApplication_Scene);

    if (eError != VRInitError_None)
    {
        mpHmd = nullptr;

        printf("openvr: could not init runtime, %s\n", VR_GetVRInitErrorAsEnglishDescription(eError));
        return false;
    }
    
	if (!VRCompositor())
	{
		printf("Compositor initialization failed.\n");
		return false;
	}

    if (debugPrint)
    {
        printf("Create device ...\n");
    }

	if (mUseSeatedPosition)
	{
		VRCompositor()->SetTrackingSpace(TrackingUniverseSeated);
		mHeightAdjust = 0;
	}
	else
	{
		VRCompositor()->SetTrackingSpace(TrackingUniverseStanding);
		mHeightAdjust = 1.5f;
	}
		
	mPositionTrackingEnabled = true; // OpenVR doesn't seem to have a way to check this?

    mInfo = "HmdDeviceOpenVr:";

    char manufacturerName[64];
    char headsetName[64];

    mpHmd->GetStringTrackedDeviceProperty(k_unTrackedDeviceIndex_Hmd, Prop_ManufacturerName_String, manufacturerName, sizeof(manufacturerName));
    mpHmd->GetStringTrackedDeviceProperty(k_unTrackedDeviceIndex_Hmd, Prop_ModelNumber_String, headsetName, sizeof(headsetName));

    mInfo = mInfo + " " + std::string(manufacturerName) + " " + std::string(headsetName);

    mIsInitialized = true;

    if (debugPrint)
    {
        printf("openvr init ... done.\n");
        flush(std::cout);
    }

    Recenter();

    return true;
}

void HmdDeviceOpenVr::Shutdown()
{
    if (!mIsInitialized)
    {
        return;
    }

    mInfo = "";


    VR_Shutdown();
    mpHmd = nullptr;

    mIsInitialized = false;
}

std::string HmdDeviceOpenVr::GetInfo()
{
    return mInfo;
}

string HmdDeviceOpenVr::GetAudioDeviceName()
{
    return "";
}

bool HmdDeviceOpenVr::HasDisplay()
{
    if (!mIsInitialized)
    {
        return false;
    }

    return true;
}

std::string HmdDeviceOpenVr::GetDisplayDeviceName()
{
    return "";
}

bool HmdDeviceOpenVr::GetDisplayPos(int& rX, int& rY)
{
    return false;
}

bool HmdDeviceOpenVr::GetDeviceResolution(int& rWidth, int& rHeight, bool& rIsRotated, bool& rIsExtendedMode)
{
    if (!mIsInitialized)
    {
        return false;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    VRExtendedDisplay()->GetWindowBounds(nullptr, nullptr, &width, &height);
    
    rWidth = static_cast<int>(width);
    rHeight = static_cast<int>(height);
    rIsRotated = false;
    rIsExtendedMode = false;

    return true;
}

bool HmdDeviceOpenVr::GetOrientationRad(float& rPitch, float& rYaw, float& rRoll)
{
    if (!mIsInitialized || mpHmd == nullptr)
    {
        return false;
    }

    glm::mat4 hmdPose;
    if (GetHMDMatrix4(hmdPose))
    {
        glm::quat hmdQuat = glm::quat_cast(hmdPose);

        float quat[4];
        quat[0] = hmdQuat.x;
        quat[1] = hmdQuat.y;
        quat[2] = hmdQuat.z;
        quat[3] = hmdQuat.w;

        ConvertQuatToEuler(&quat[0], rYaw, rPitch, rRoll);
        return true;
    }

    return false;

}


bool HmdDeviceOpenVr::GetPosition(float &rX, float &rY, float &rZ)
{
    if (!mIsInitialized || mpHmd == nullptr || !mPositionTrackingEnabled)
    {
        return false;
    }

    if (mrTrackedDevicePose[k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
    {
        HmdMatrix34_t mat = GetPoseWithOffset(mrTrackedDevicePose[k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking);
        rX = mat.m[0][3];
        rY = mat.m[1][3];
        rZ = mat.m[2][3];
        return true;
    }

    return false;
}

bool HmdDeviceOpenVr::GetHMDMatrix4(glm::mat4& mat)
{
    if (!mrTrackedDevicePose[k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
        return false;

    HmdMatrix34_t matPose = GetPoseWithOffset(mrTrackedDevicePose[k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking);
    mat = glm::mat4(matPose.m[0][0], matPose.m[1][0], matPose.m[2][0], 0.0,
                    matPose.m[0][1], matPose.m[1][1], matPose.m[2][1], 0.0,
                    matPose.m[0][2], matPose.m[1][2], matPose.m[2][2], 0.0,
                    matPose.m[0][3], matPose.m[1][3], matPose.m[2][3], 1.0f);
    return true;
}

bool HmdDeviceOpenVr::GetHandOrientationRad(bool rightHand, float& rPitch, float& rYaw, float& rRoll)
{
    if (!mIsInitialized || mpHmd == nullptr || !HasHand(rightHand))
    {
        return false;
    }

    glm::mat4 handPose;
    if (GetHandMatrix4(rightHand, handPose))
    {
        glm::quat hmdQuat = glm::quat_cast(handPose);
        glm::quat adjustQuat = glm::quat(glm::vec3(DEG2RAD(-45.0f), 0.0f, 0.0f));

        hmdQuat *= adjustQuat;

        float quat[4];
        quat[0] = hmdQuat.x;
        quat[1] = hmdQuat.y;
        quat[2] = hmdQuat.z;
        quat[3] = hmdQuat.w;

        ConvertQuatToEuler(&quat[0], rYaw, rPitch, rRoll);

        return true;
    }

    return false;

}

bool HmdDeviceOpenVr::GetHandOrientationGripRad(bool rightHand, float& rPitch, float& rYaw, float& rRoll)
{
    if (!mIsInitialized || mpHmd == nullptr || !HasHand(rightHand))
    {
        return false;
    }

    glm::mat4 handPose;
    if (GetHandMatrix4(rightHand, handPose))
    {
        glm::quat hmdQuat = glm::quat_cast(handPose);
        glm::quat adjustQuat = glm::quat(glm::vec3(DEG2RAD(-90.0f), 0.0f, 0.0f));

        hmdQuat *= adjustQuat;

        float quat[4];
        quat[0] = hmdQuat.x;
        quat[1] = hmdQuat.y;
        quat[2] = hmdQuat.z;
        quat[3] = hmdQuat.w;

        ConvertQuatToEuler(&quat[0], rYaw, rPitch, rRoll);
        return true;
    }

    return false;
}

bool HmdDeviceOpenVr::GetHandPosition(bool rightHand, float &rX, float &rY, float &rZ)
{
    if (!mIsInitialized || mpHmd == nullptr || !mPositionTrackingEnabled || !HasHand(rightHand))
    {
        return false;
    }

    uint32_t id = rightHand ? mTrackerIdHandRight : mTrackerIdHandLeft;
    if (mrTrackedDevicePose[id].bPoseIsValid)
    {
        HmdMatrix34_t mat = GetPoseWithOffset(mrTrackedDevicePose[id].mDeviceToAbsoluteTracking);
        rX = mat.m[0][3];
        rY = mat.m[1][3];
        rZ = mat.m[2][3];
        return true;
    }

    return false;
}

bool HmdDeviceOpenVr::HasHand(bool rightHand)
{
    if (rightHand && mTrackerIdHandRight == k_unTrackedDeviceIndexInvalid)
    {
        return false;
    }
    else if (!rightHand && mTrackerIdHandLeft == k_unTrackedDeviceIndexInvalid)
    {
        return false;
    }

    return true;
}

bool HmdDeviceOpenVr::GetHandMatrix4(bool rightHand, glm::mat4& mat)
{
    uint32_t id = rightHand ? mTrackerIdHandRight : mTrackerIdHandLeft;
    if (!HasHand(rightHand) || !mrTrackedDevicePose[id].bPoseIsValid)
        return false;

    HmdMatrix34_t matPose = GetPoseWithOffset(mrTrackedDevicePose[id].mDeviceToAbsoluteTracking);
    mat = glm::mat4(matPose.m[0][0], matPose.m[1][0], matPose.m[2][0], 0.0,
        matPose.m[0][1], matPose.m[1][1], matPose.m[2][1], 0.0,
        matPose.m[0][2], matPose.m[1][2], matPose.m[2][2], 0.0,
        matPose.m[0][3], matPose.m[1][3], matPose.m[2][3], 1.0f);
    return true;
}

void HmdDeviceOpenVr::GetControllerState(bool rightHand, VRControllerState_t& state)
{
    mpHmd->GetControllerState(rightHand ? mTrackerIdHandRight : mTrackerIdHandLeft, &state, sizeof(state));
}

void HmdDeviceOpenVr::Recenter()
{
	if (mpHmd == nullptr)
	{
		return;
	}

	if (mUseSeatedPosition)
	{
		mpHmd->ResetSeatedZeroPose();
	}
}


HmdMatrix34_t OpenVr::HmdDeviceOpenVr::GetPoseWithOffset(const HmdMatrix34_t & pose)
{
	HmdMatrix34_t poseWithOffset = pose;
	poseWithOffset.m[1][3] -= mHeightAdjust;

	return poseWithOffset;
}

void HmdDeviceOpenVr::ConvertQuatToEuler(const float* quat, float& rYaw, float& rPitch, float& rRoll)
{
    //https://svn.code.sf.net/p/irrlicht/code/trunk/include/quaternion.h
    // modified to get yaw before pitch

    float W = quat[3];
    float X = quat[1];
    float Y = quat[0];
    float Z = quat[2];

    float sqw = W*W;
    float sqx = X*X;
    float sqy = Y*Y;
    float sqz = Z*Z;

    float test = 2.0f * (Y*W - X*Z);

    if (test > (1.0f - 0.000001f))
    {
        // heading = rotation about z-axis
        rRoll = (-2.0f*atan2(X, W));
        // bank = rotation about x-axis
        rYaw = 0;
        // attitude = rotation about y-axis
        rPitch = M_PI/2.0f;
    }
    else if (test < (-1.0f + 0.000001f))
    {
        // heading = rotation about z-axis
        rRoll = (2.0f*atan2(X, W));
        // bank = rotation about x-axis
        rYaw = 0;
        // attitude = rotation about y-axis
        rPitch = M_PI/-2.0f;
    }
    else
    {
        // heading = rotation about z-axis
        rRoll = atan2(2.0f * (X*Y +Z*W),(sqx - sqy - sqz + sqw));
        // bank = rotation about x-axis
        rYaw = atan2(2.0f * (Y*Z +X*W),(-sqx - sqy + sqz + sqw));
        // attitude = rotation about y-axis
        test = max(test, -1.0f);
        test = min(test, 1.0f);
        rPitch = asin(test);
    }
}

void HmdDeviceOpenVr::UpdatePoses()
{
    VRCompositor()->WaitGetPoses(mrTrackedDevicePose, vr::k_unMaxTrackedDeviceCount, nullptr, 0);

    mTrackableDeviceCount = 0;
    for (uint32_t nDevice = 0; nDevice < vr::k_unMaxTrackedDeviceCount; ++nDevice)
    {
        if (mrTrackedDevicePose[nDevice].bPoseIsValid)
        {
            mTrackableDeviceCount++;
            if (mpHmd->GetTrackedDeviceClass(nDevice) == TrackedDeviceClass_HMD)
            {
                //Com_Printf("HMD is %u\n", nDevice);
            }
            else if (mpHmd->GetTrackedDeviceClass(nDevice) == TrackedDeviceClass_Controller)
            {
                //Com_Printf("controller %u\n", nDevice);
            }
}
    }

    mTrackerIdHandLeft = mpHmd->GetTrackedDeviceIndexForControllerRole(TrackedControllerRole_LeftHand);
    mTrackerIdHandRight = mpHmd->GetTrackedDeviceIndexForControllerRole(TrackedControllerRole_RightHand);

}


