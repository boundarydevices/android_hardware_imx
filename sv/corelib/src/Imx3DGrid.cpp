/*
*
* Copyright © 2017-2020 NXP
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice (including the next
* paragraph) shall be included in all copies or substantial portions of the
* Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <math.h>
#include "Imx3DGrid.hpp"
#include "Imx3DView.hpp"

#define SV_INVALID_INDEX (-1)

using namespace std;

namespace imx {
/**************************************************************************************************************
 *
 * @brief  			CurvilinearGrid class constructor.
 *
 * @param  in 		uint angles 	  -	every half of circle will be divided into this number of arcs.
 * 										The start_angle sets a circular sector for which the grid will be generated.
 * 					uint nop_z 		  -	number of points in z axis.
 * 					double step_x	  -	grid step in polar coordinate system. The value is also used to define
 * 										grid points in z axis.
 * 										Step in z axis: step_z[i] = (i * step_x)^2, i = 1, 2, ... - number of point.
 *
 * @return 			The function create the CurvilinearGrid object.
 *
 * @remarks 		The function sets main property of new CurvilinearGrid object.
 *
 **************************************************************************************************************/
CurvilinearGrid::CurvilinearGrid(uint angles, uint nop_z, double step_x,
	                            uint32_t width, uint32_t height,
	                            vector<Vector3d> &evsRotations, vector<Vector3d> &evsTransforms,
	                            vector<Matrix<double, 3, 3>> &Ks, vector<Matrix<double, 1, 4>> &Ds) {
	mParameters.angles = angles;
	mParameters.nop_z = nop_z;
	mParameters.step_x = step_x;
	mNoP = 0;
	mWidth = width;
	mHeight = height;
	mEvsRotations = evsRotations;
	mEvsTransforms = evsTransforms;
	mKs = Ks;
	mDs = Ds;
}

/**************************************************************************************************************
 *
 * @brief  			Generate 3D grid of texels/vertices for the input Camera object.
 *
 * @param
 * 		   in		double radius - radius of base circle. The radius must be defined relative to template width.
 * 		   			The template width (in pixels) is considered as 1.0.
 *
 * @return 			-
 *
 * @remarks 		The function defines 3D grid, generates triangles from it and saves the triangles into file.
 *
 **************************************************************************************************************/
bool CurvilinearGrid::createGrid(float radius)
{
	mGlobalP3d.clear();
	mRadius = radius;
	int pnum = radius / mParameters.step_x;	// Number of grid rows of flat bowl bottom
	int startpnum = 2;	// The first row of grid
	mNoP = (pnum - startpnum) + mParameters.nop_z; // Number of grid points for one grid sector (angle)

	for(uint angle = 0; angle < mParameters.angles * 2; angle++)
	{
		double angle_start = angle * (M_PI / mParameters.angles); // Start angle of the current circular sector

		// Flat bottom points
		for(int i = startpnum; i < pnum; i++)
		{
			double hptn = i * mParameters.step_x;
			mGlobalP3d.push_back(Vector3d(hptn * cos(angle_start), hptn * sin(angle_start), 0));
		}

		// Points on bowl side
		for(uint i = 1; i <= mParameters.nop_z; i++)
		{
			double hptn = radius + i * mParameters.step_x;
			mGlobalP3d.push_back(Vector3d(hptn * cos(angle_start), hptn * sin(angle_start),
	                    pow(i * mParameters.step_x, 2)));
		}
	}
	ALOGI("3D point size=%u", mGlobalP3d.size());
	shared_ptr<PixelMap> LUT_ptr(new PixelMap[mGlobalP3d.size()],
	            std::default_delete<PixelMap[]>());
	if(LUT_ptr != nullptr) {
	    PixelMap *LUT = LUT_ptr.get();
	    memset((void *)LUT, SV_INVALID_INDEX, sizeof(PixelMap) * mGlobalP3d.size());
	    mLookupPtr = LUT_ptr;
	}
	else {
	    ALOGE("Cannot allocate Lookup mapping buffer!");
	    return false;
	}

	PixelMap *LUT = mLookupPtr.get();
	int iMaxNoP = 0;
	int iMinNoP = mNoP;
	for(uint32_t pointIndex = 0; pointIndex < mGlobalP3d.size(); pointIndex ++) {
	    auto &worldPoint = mGlobalP3d[pointIndex];
	    PixelMap *pMap = LUT + pointIndex;

	    int iNoP = pointIndex % mNoP;
	    if(iNoP == 0)
	        iMaxNoP = 0;
	    if(iNoP == mNoP - 1)
	        if(iMaxNoP < iMinNoP)
	            iMinNoP = iMaxNoP;

	    for(int index = 0; index < mEvsRotations.size(); index ++) {
	        auto &R = mEvsRotations[index];
	        auto &T = mEvsTransforms[index];
	        auto &K = mKs[index];
	        auto &D = mDs[index];
	        double k1 = D(0,0);
	        double k2 = D(0,1);
	        double k3 = D(0,2);
	        double k4 = D(0,3);
	        double fx = K(0,0);
	        double fy = K(1,1);
	        double cx = K(0,2);
	        double cy = K(1,2);
	        double normal = R.norm();
	        AngleAxisd rotation_vector(normal, R/normal);
	        auto rotation_matrix = rotation_vector.toRotationMatrix();

	        Vector3d cameraPoint = rotation_matrix * worldPoint + T;
	        Vector3d normalizedCamera = cameraPoint/cameraPoint[2];
	        Vector3d cameraZ {0, 0, 1};
	        auto dot = cameraZ.dot(cameraPoint);
	        auto fov = acos(dot/cameraPoint.norm());
	        auto threshhold = M_PI * 3/8;
	        // if the worldPoint beyond the camera FOV, ignore it.
	        // assume we only care FOV < threshold point
	        // a dot b = a.norm()*b.norm*cos(angle)
	        // here angle should be less than 90 degree.
	        if(dot/cameraPoint.norm() < cos(threshhold)) {
	            continue;
	        }
	        if(cameraPoint[2] < 0)
	            ALOGI("******Error with camera z %f", cameraPoint[2]);

	        Vector3d undistortPoint = K * normalizedCamera;
	        auto xc = (undistortPoint[0]/undistortPoint[2] - cx)/fx;
	        auto yc = (undistortPoint[1]/undistortPoint[2] - cy)/fy;

	        auto r = sqrt(xc*xc + yc*yc);
	        double theta = atan(r);
	        double theta_4 = k4*pow(theta, 9);
	        double theta_3 = k3*pow(theta, 7);
	        double theta_2 = k2*pow(theta, 5);
	        double theta_1 = k1*pow(theta, 3);
	        double theta_d = theta + theta_1 + theta_2 + theta_3 + theta_4;
	        double x_distorted = xc*(theta_d/r);
	        double y_distorted = yc*(theta_d/r);

	        double u_distorted = fx * x_distorted + cx;
	        double v_distorted = fy * y_distorted + cy;
	        if(u_distorted >= 0 && v_distorted >=0 && \
	                u_distorted < mWidth && v_distorted < mHeight) {
	            if(iMaxNoP < iNoP)
	                iMaxNoP = iNoP;
	            //Get fisyeye image 2d pixel based on p3d point
	            if(pMap->index0 == -1) {
	                pMap->index0 = index;
	                pMap->u0 = u_distorted;
	                pMap->v0 = v_distorted;
	                pMap->fov0 = fov;
	                pMap->alpha0 = 1;
	            }
	            else if((index != pMap->index0)&&(pMap->index1 == -1)) {
	                pMap->index1 = index;
	                pMap->u1 = u_distorted;
	                pMap->v1 = v_distorted;
	                pMap->fov1 = fov;
	                auto fov0 = pMap->fov0;
	                //alpha value is set based the ratio of distance the border
	                //of two camera FOV
	                auto alpha0 = (threshhold - fov0)/(threshhold - fov0 + threshhold - fov);
	                pMap->alpha1 = 1 - alpha0;
	                pMap->alpha0 = alpha0;
	            }
	            else if((index != pMap->index0) &&
	                   (index != pMap->index1)) {
	                ALOGW("********Point %d overlapped with 3 cameras!!!", pointIndex);
	            }
	        }
	        else {
	            //No valid mapping
	        }
	    }
	}
	ALOGI("Grids %d of points on one line, minmum %d points with valid mapping on one line", mNoP, iMinNoP);
	mMinNoP = iMinNoP;
	return true;
}

bool CurvilinearGrid::valid3DPoint(uint32_t index) {
	if(index >= mGlobalP3d.size())
	    return false;
	else
	    return true;
}

int CurvilinearGrid::getGrids(float** points) {
	int size = 0;

	if (mGlobalP3d.size() == 0) return (0);

	(*points) = (float*)malloc(8 * SV_VERTEX_NUM * mGlobalP3d.size() * sizeof(float));
	if ((*points) == NULL) {
		ALOGE("Memory allocation did not complete successfully");
		return(0);
	}
	memset((void *)*points, 0, 8 * SV_VERTEX_NUM * mGlobalP3d.size() * sizeof(float));
	/**************************** Get grids line  **********************************
	 *   						  p  _  p+2
	 *   Grids orientation: 		| |		(p - p+1 - p+3 - p+2 - p)
	 *   							|_|
	 *   						 p+1    p+3
	 *******************************************************************************************************/
	ALOGI("Valid Grid point size %u, angles %d NoP %d",
	        mGlobalP3d.size(), mParameters.angles, mNoP);

	auto norXY = mRadius + mParameters.nop_z * mParameters.step_x;
	auto total = mParameters.angles * 2 * mNoP;
	for(uint angle = 0; angle < mParameters.angles * 2; angle++) {
		for(int n = 0; n < mNoP - 1; n++) {
	        int i = angle * mNoP + n;
	        int inext = (i + mNoP)%total;
	        if(!valid3DPoint(i) || !valid3DPoint(i + 1)
	            || !valid3DPoint(inext) || !valid3DPoint(inext))
	            continue;
	        auto fn = [&](unsigned int point) {
	            (*points)[size] = mGlobalP3d[point][0]/norXY;
	            (*points)[size + 1] = mGlobalP3d[point][1]/norXY;
	            (*points)[size + 2] = mGlobalP3d[point][2]/norXY;
	            size += 3;
	        };

	        //line p - p1
	        //P
	        fn(i);
	        //P1
	        fn(i + 1);

	        //line p1 - p3
	        //P1
	        fn(i + 1);
	        //P3
	        fn(inext + 1);

	        //line p3 - p2
	        //P3
	        fn(inext + 1);
	        //P2
	        fn(inext);

	        //line p2 - p
	        //P2
	        fn(inext);
	        //P
	        fn(i);
	    }
	}

	return (size);
}

static int findCommonIndex(PixelMap *LUT0, PixelMap *LUT1,
	                                PixelMap *LUT2, PixelMap *LUT3)
{
	int index0 = SV_INVALID_INDEX;
	int index1 = SV_INVALID_INDEX;
	for(int i = 0; i < 4; i ++){
	    if((i != LUT0->index0)&&(i != LUT0->index1))
	        continue;
	    if((i != LUT1->index0)&&(i != LUT1->index1))
	        continue;
	    if((i != LUT2->index0)&&(i != LUT2->index1))
	        continue;
	    if((i != LUT3->index0)&&(i != LUT3->index1))
	        continue;
	    if(index0 == SV_INVALID_INDEX)
	        index0 = i;
	    else if(index1 == SV_INVALID_INDEX) {
	        index1 = i;
	    }
	}
	return index0;
}

int CurvilinearGrid::getMashes(float** points, int camera) {
	int size = 0;

	if (mGlobalP3d.size() == 0) return (0);
	float x_norm = 1.0 / mWidth;
	float y_norm = 1.0 / mHeight;

	//(*points) = (float*)malloc(6 * SV_ATTRIBUTE_NUM * mP2d.size() * sizeof(float));
	(*points) = (float*)malloc(6 * SV_ATTRIBUTE_NUM * mGlobalP3d.size() * sizeof(float));
	if ((*points) == NULL) {
		ALOGE("Memory allocation did not complete successfully");
		return(0);
	}
	memset(*points, 0, 6 * SV_ATTRIBUTE_NUM * mGlobalP3d.size() * sizeof(float));

	/**************************** Get triangles for I quadrant of template **********************************
	 *   							  p  _  p+2
	 *   Triangles orientation: 		| /|		1st triangle (p - p+1 - p+2)
	 *   								|/_|		2nd triangle (p+1 - p+2 - p+3)
	 *   							 p+1    p+3
	 *******************************************************************************************************/
	auto total = mParameters.angles * 2 * mNoP;
	auto norXY = mRadius + mParameters.nop_z * mParameters.step_x;
	PixelMap *LUT = mLookupPtr.get();
	for(uint angle = 0; angle < mParameters.angles * 2; angle++) {
		for(int n = 0; n < mMinNoP; n++) {
	        int i = angle * mNoP + n;
	        int inext = (i + mNoP)%total;
	        if(!valid3DPoint(i) || !valid3DPoint(i + 1)
	            || !valid3DPoint(inext) || !valid3DPoint(inext))
	            continue;
	        uint32_t index = findCommonIndex(LUT + i, LUT + i + 1,
	                                         LUT + inext, LUT + inext + 1);
	        if(index == SV_INVALID_INDEX)
	            continue;

	        if(index != camera)
	            continue;

	        auto fn = [&](unsigned int point){
	            (*points)[size ++] = mGlobalP3d[point][0]/norXY;
	            (*points)[size ++] = mGlobalP3d[point][1]/norXY;
	            (*points)[size ++] = mGlobalP3d[point][2]/norXY;
	            //texture corridnate should be top - x*normal?
	            if(index == (LUT + point)->index0) {
	                    (*points)[size ++] = (LUT + point)->u0 * x_norm;
	                    (*points)[size ++] = (LUT + point)->v0 * y_norm;
	            } else {
	                    (*points)[size ++] = (LUT + point)->u1 * x_norm;
	                    (*points)[size ++] = (LUT + point)->v1 * y_norm;
	            }
	        };

	        //first triangle
	        //P
	        fn(i);
	        //P1
	        fn(i + 1);
	        //P2
	        fn(inext);

	        //second triangle
	        //P3
	        fn(inext + 1);
	        //P2
	        fn(inext);
	        //P1
	        fn(i + 1);
	    }
	}

	return (size);
}

}
