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
CurvilinearGrid::CurvilinearGrid(uint angles, uint nop_z, double step_x)
{
	parameters.angles = angles;
	parameters.nop_z = nop_z;
	parameters.step_x = step_x;
	NoP = 0;
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
void CurvilinearGrid::createGrid(double radius)
{
	p3d.clear();
		
	int pnum = radius / parameters.step_x;	// Number of grid rows of flat bowl bottom
	int startpnum = 1;	// The first row of grid
	NoP = 2 * ((pnum - startpnum) + parameters.nop_z); // Number of grid points for one grid sector (angle)

	for(uint angle = 1; angle <= parameters.angles * 2; angle++)
	{
		double angle_start = (angle - 1) * (M_PI / parameters.angles); // Start angle of the current circular sector
		double angle_end = angle * (M_PI / parameters.angles); // End angle of the current circular sector

		// Flat bottom points
		for(int i = startpnum; i < pnum; i++)
		{
			double hptn = i * parameters.step_x;
			p3d.push_back(Size3dFloat(hptn * cos(angle_end), - hptn * sin(angle_end), 0));
			p3d.push_back(Size3dFloat(hptn * cos(angle_start), - hptn * sin(angle_start), 0));
		}

		// Points on bowl side
		for(uint i = 1; i <= parameters.nop_z; i++)
		{
			double hptn = radius + i * parameters.step_x;
			p3d.push_back(Size3dFloat(hptn * cos(angle_end), - hptn * sin(angle_end), - pow(i * parameters.step_x, 2)));
			p3d.push_back(Size3dFloat(hptn * cos(angle_start), - hptn * sin(angle_start), - pow(i * parameters.step_x, 2)));
		}
	}
}

}
