/*
FireSight.cpp https://github.com/firepick1/FirePick/wiki

Copyright (C) 2013  Karl Lew, <karl@firepick.org>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <string.h>
#include "FireLog.h"
#include "FireSight.hpp"
#include "opencv2/features2d/features2d.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

using namespace cv;
using namespace std;

HoleRecognizer::HoleRecognizer(float minDiameter, float maxDiameter) {
	maxDiam = maxDiameter;
	minDiam = minDiameter;
  delta = 5;
	minArea = (int)(minDiameter*minDiameter*3.141592/4); // 60;
	maxArea = (int)(maxDiameter*maxDiameter*3.141592/4); // 14400;
	maxVariation = 0.25;
	minDiversity = (maxDiam - minDiam)/(float)minDiam; // 0.2;
	LOGDEBUG3("MSER minArea:%d maxArea:%d minDiversity:%d/100", minArea, maxArea, (int)(minDiversity*100+0.5));
	max_evolution = 200;
	area_threshold = 1.01;
	min_margin = .003;
	edge_blur_size = 5;
	mser = MSER(delta, minArea, maxArea, maxVariation, minDiversity,
		max_evolution, area_threshold, min_margin, edge_blur_size);
}

void HoleRecognizer::scan(Mat &matRGB){
	Mat matGray;
	if (matRGB.channels() == 1) {
		matRGB = matGray;
	} else {
		cvtColor(matRGB, matGray, CV_RGB2GRAY);
	}
	
	Mat mask;
	vector<vector<Point> > contours;
	mser(matGray, contours, mask);

	int nBlobs = (int) contours.size();
	Scalar mark(255,0,255);
	LOGDEBUG1("circles_MSER %d blobs", nBlobs);
	for( int i = 0; i < nBlobs; i++) {
		vector<Point> pts = contours[i];
		int nPts = pts.size();
		int red = (i & 1) ? 0 : 255;
		int green = (i & 2) ? 128 : 255 ;
		int blue = (i & 1) ? 255 : 0;
		int minX = 0x7fff;
		int maxX = 0;
		int minY = 0x7fff;
		int maxY = 0;
		float avgX = 0;
		float avgY = 0;
		for (int j = 0; j < nPts; j++) {
			if (pts[j].x < minX) { minX = pts[j].x; }
			if (pts[j].y < minY) { minY = pts[j].y; }
			if (pts[j].x > maxX) { maxX = pts[j].x; }
			if (pts[j].y > maxY) { maxY = pts[j].y; }
			avgX += pts[j].x;
			avgY += pts[j].y;
		}
		avgX = avgX / nPts;
		avgY = avgY / nPts;
		if (maxX - minX < maxDiam && maxY - minY < maxDiam) {
			red = 255; green = 0; blue = 255;
			Vec3f hole(avgX, avgY, nPts);
			LOGDEBUG3("circles_MSER (%d/10,%d/10)mm %d pts MATCH", (int)(hole[0] * 10+.5), (int)(hole[1]*10 +.5), (int)hole[2]);
		} else {
			LOGDEBUG3("circles_MSER (%d/10,%d/10)mm %d pts (other)", (int)(avgX * 10+.5), (int)(avgY*10 +.5), nPts);
		}

		for (int j = 0; j < nPts; j++) {
			matRGB.at<Vec3b>(pts[j])[0] = red;
			matRGB.at<Vec3b>(pts[j])[1] = green;
			matRGB.at<Vec3b>(pts[j])[2] = blue;
		}
	}
}

