/*
FirePick.cpp https://github.com/firepick1/FirePick/wiki

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

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "FirePick.h"
#include "FireLog.h"
#include "FirePiCam.h"
#include <opencv/cv.h>
#include <opencv/highgui.h>
#include "opencv2/features2d/features2d.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#define STATUS_BUFFER_SIZE 1024

char status_buffer[STATUS_BUFFER_SIZE];

static void circles_MSER(cv::Mat &matGray, cv::Mat &matRGB){
	int threshold_value = 64;
  int delta = 5;
	int minArea = 400; // 60;
	int maxArea = 600; //14400;
	double maxVariation = 0.25;
	double minDiversity = 0.2;
	int max_evolution = 200;
	double area_threshold = 1.01;
	double min_margin = .003;
	int edge_blur_size = 5;
	cv::vector<cv::vector<cv::Point> > contours;
	cv::Mat mask;
	
	//threshold( matGray, matGray, threshold_value, 255, cv::THRESH_TOZERO );
	//matGray.convertTo(matGray, -1, 2, 0); // contrast
	cv::MSER(delta, minArea, maxArea, maxVariation, minDiversity,
		max_evolution, area_threshold, min_margin, edge_blur_size)(matGray, contours, mask);

	int nBlobs = (int) contours.size();
	cv::Scalar mark(255,0,255);
	LOGINFO1("circles_MSER %d blobs", nBlobs);
	for( int i = 0; i < nBlobs; i++) {
		cv::vector<cv::Point> pts = contours[i];
		int nPts = pts.size();
		int red = (i & 1) ? 128 : 192;
		int green = (i & 4) ? 128 : 192;
		int blue = (i & 2) ? 128 : 192;
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
		LOGINFO3("circles_MSER (%d,%d) %d pts", (int)(avgX * 10+.5), (int)(avgY*10 +.5), nPts);
		if (maxX - minX < 40 && maxY - minY < 40) {
			red = 255; green = 0; blue = 255;
		}
		for (int j = 0; j < nPts; j++) {
			matRGB.at<cv::Vec3b>(pts[j])[0] = red;
			matRGB.at<cv::Vec3b>(pts[j])[1] = green;
			matRGB.at<cv::Vec3b>(pts[j])[2] = blue;
		}
	}
	//circle( matRGB, cv::Point(400,100), 50, mark, 3, 8, 0 ); 
}

static void circles_Hough(cv::Mat matGray){
  //GaussianBlur( matGray, matGray, cv::Size(9, 9), 2, 2 );
	int threshold_value = 64;
	int max_BINARY_value = 255;
	
	//matGray.convertTo(matGray, -1, .5, 0); // contrast
	//threshold( matGray, matGray, threshold_value, max_BINARY_value, cv::THRESH_BINARY );
  cv::vector<cv::Vec3f> circles;
	HoughCircles(matGray, circles, CV_HOUGH_GRADIENT, 2, 16, 200, 100, 4, 65 );

	for( size_t i = 0; i < circles.size(); i++ ) {
		int x = cvRound(circles[i][0]);
		int y = cvRound(circles[i][1]);
		int radius = cvRound(circles[i][2]);
		cv::Point center(x, y);
		LOGINFO3("firepick_circles (%d,%d,%d)", x, y, radius);
		circle( matGray, center, 3, cv::Scalar(0,255,0), -1, 8, 0 ); // draw the circle center
		circle( matGray, center, radius, cv::Scalar(0,0,255), 3, 8, 0 ); // draw the circle outline
	}
}

const void* firepick_circles(JPG *pJPG) {
	CvMat* cvJpg = cvCreateMatHeader(1, pJPG->length, CV_8UC1);
	cvSetData(cvJpg, pJPG->pData, pJPG->length);
	IplImage *pCvImg = cvDecodeImage(cvJpg, CV_LOAD_IMAGE_COLOR);
	cv::Mat matRGB(pCvImg);
	cv::Mat matGray;
	cvtColor(matRGB, matGray, CV_RGB2GRAY);

  //circles_Hough(matGray);
  circles_MSER(matGray, matRGB);

	imwrite("/home/pi/camcv.bmp", matRGB);
	cvReleaseMatHeader(&cvJpg);

  return pJPG;
}

const char* firepick_status() {
  time_t current_time = time(NULL);
  char timebuf[70];
  strcpy(timebuf, ctime(&current_time));
  timebuf[strlen(timebuf)-1] = 0;

	const char *errorOrWarn = firelog_lastMessage(FIRELOG_WARN);
	if (strlen(errorOrWarn)) {
		return errorOrWarn;
	}

  sprintf(status_buffer, 
    "{\n"
    " 'timestamp':'%s'\n"
    " 'message':'FirePick OK!',\n"
    " 'version':'FireFuse version %d.%d'\n"
    "}\n",
    timebuf,
    FireFuse_VERSION_MAJOR, FireFuse_VERSION_MINOR);
  return status_buffer;
}

int firepick_camera_daemon(JPG *pJPG) {
  int status = firepicam_create(0, NULL);

  LOGINFO1("firepick_camera_daemon start -> %d", status);

  for (;;) {
    JPG_Buffer buffer;
    buffer.pData = NULL;
    buffer.length = 0;
    
    status = firepicam_acquireImage(&buffer);
    pJPG->pData = buffer.pData;
    pJPG->length = buffer.length;
  }

  LOGINFO1("firepick_camera_daemon exit -> %d", status);
  firepicam_destroy(status);
}


