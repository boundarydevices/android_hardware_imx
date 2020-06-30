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


#ifndef SRC_GRID_HPP_
#define SRC_GRID_HPP_

/*******************************************************************************************
 * Includes
 *******************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <vector>

#include "ImxSurroundViewTypes.hpp"

using namespace std;

namespace imx {
/*******************************************************************************************
 * Types
 *******************************************************************************************/
struct GridParam {		/* Parameters of grid */
	uint angles;		/* Every quadrant of circle will be divided into this number of arcs */
	uint nop_z;			/* Number of points in z axis */
	double step_x;		/* Step in x axis which is used to define grid points in z axis.
	 	 	 	 	 	 * Step in z axis: step_z[i] = (i * step_x)^2, i = 1, 2, ... - number of point */
};


/*******************************************************************************************
 * Classes
 *******************************************************************************************/
/* CurvilinearGrid class - the grid is denser at the middle of bowl bottom and more sparse at the bowl bottom edge. */
class CurvilinearGrid {
public:
	void setAngles(uint val) {parameters.angles = val; }
	void setNopZ(uint val) {parameters.nop_z = val; }
	void setStepX(double val) {parameters.step_x = val; }
	
	
		/**************************************************************************************************************
		 *
		 * @brief  			CurvilinearGrid class constructor.
		 *
		 * @param  in 		uint angles 	  -	every quadrant of circle will be divided into this number of arcs.
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
		CurvilinearGrid(uint angles, uint nop_z, double step_x);

		/**************************************************************************************************************
		 *
		 * @brief  			Generate 3D grid of texels/vertices for the input Camera object.
		 *
		 * @param  in		Camera* camera - pointer to the Camera object
		 * 		   in		double radius - radius of base circle. The radius must be defined relative to template width.
		 * 		   			The template width (in pixels) is considered as 1.0.
		 *
		 * @return 			-
		 *
		 * @remarks 		The function defines 3D grid, generates triangles from it and saves the triangles into file.
		 *
		 **************************************************************************************************************/
		void createGrid(double radius);

private:
		int NoP;				// Number of grid points for one grid sector (angle)
		vector<Size3dFloat> p3d;	// 3D grid points (template points)
		GridParam parameters;	// Parameters of grid
};

} //namespace imx
#endif /* SRC_GRID_HPP_ */
