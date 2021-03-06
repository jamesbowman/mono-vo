/*

The MIT License

Copyright (c) 2015 Avi Singh

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/features2d/features2d.hpp"
#include "opencv2/calib3d/calib3d.hpp"

#include <iostream>
#include <ctype.h>
#include <algorithm> // for copy
#include <iterator> // for ostream_iterator
#include <vector>
#include <ctime>
#include <sstream>
#include <fstream>
#include <string>

#define CV3 0

using namespace cv;
using namespace std;

void featureTracking(Mat img_1, Mat img_2, vector<Point2f>& points1, vector<Point2f>& points2, vector<uchar>& status)
{
  // this function automatically gets rid of points for which tracking fails

  vector<float> err;
  Size winSize = Size(21, 21);
  TermCriteria termcrit = TermCriteria(TermCriteria::COUNT+TermCriteria::EPS, 30, 0.01);

  calcOpticalFlowPyrLK(img_1, img_2, points1, points2, status, err, winSize, 3, termcrit, 0, 0.001);

  // getting rid of points for which the KLT tracking failed or those who have gone outside the frame
  int indexCorrection = 0;
  for (int i = 0; i < status.size(); i++) {
    Point2f pt = points2.at(i - indexCorrection);
      if ((status.at(i) == 0) || (pt.x < 0) || (pt.y < 0)) {
        if ((pt.x < 0) || (pt.y < 0))
          status.at(i) = 0;
        points1.erase (points1.begin() + (i - indexCorrection));
        points2.erase (points2.begin() + (i - indexCorrection));
        indexCorrection++;
      }
  }
}

void featureDetection(Mat img_1, vector<Point2f>& points1)
{
  // uses FAST as of now, modify parameters as necessary
  vector<KeyPoint> keypoints_1;
  int fast_threshold = 20;
  bool nonmaxSuppression = true;
  FAST(img_1, keypoints_1, fast_threshold, nonmaxSuppression);
  KeyPoint::convert(keypoints_1, points1, vector<int>());
}

using namespace cv;
using namespace std;

#define MIN_NUM_FEAT 2000

double getAbsoluteScale(int frame_id, int sequence_id, double z_cal)
{
  return 1;
  string line;
  int i = 0;
  ifstream myfile ("/home/avisingh/Datasets/KITTI_VO/00.txt");
  double x = 0, y = 0, z = 0;
  double x_prev, y_prev, z_prev;
  if (myfile.is_open()) {
    while ((getline(myfile, line)) && (i <= frame_id)) {
      z_prev = z;
      x_prev = x;
      y_prev = y;
      std::istringstream in(line);
      // cout << line << '\n';
      for (int j = 0; j < 12; j++) {
        in >> z ;
        if (j == 7) y = z;
        if (j == 3)  x = z;
      }
      i++;
    }
    myfile.close();
  } else {
    cout << "Unable to open file";
    return 0;
  }

  return sqrt((x-x_prev)*(x-x_prev) + (y-y_prev)*(y-y_prev) + (z-z_prev)*(z-z_prev)) ;
}

// #define DATASET "/home/james/image_processing/videostab/bags/c15-41b/%04d.png"
// Mat K = (Mat_<double>(3, 3) << 564.85980225, 0., 973.51554108, 0., 664.61627197, 424.90181213, 0, 0, 1);
// #define MAX_FRAME 171

#define DATASET "KITTI_00/%06d.png"
Mat K = (Mat_<double>(3, 3) << 718.8560, 0, 607.1928,  0, 718.8560, 185.2157,  0, 0, 1);
#define MAX_FRAME 325

int main(int argc, char** argv)
{
  Mat img_1, img_2;
  Mat R_f, t_f; // the final rotation and tranlation vectors containing the

  ofstream myfile;
  myfile.open("results1_1.txt");

  double scale = 1.00;
  char filename1[200];
  char filename2[200];
  sprintf(filename1, DATASET, 0);
  sprintf(filename2, DATASET, 1);

  char text[100];
  int fontFace = FONT_HERSHEY_PLAIN;
  double fontScale = 1;
  int thickness = 1;
  cv::Point textOrg(10, 50);

  // read the first two frames from the dataset
  Mat img_1_c = imread(filename1);
  Mat img_2_c = imread(filename2);

  if (!img_1_c.data || !img_2_c.data) {
    std::cout<< " --(!) Error reading images " << std::endl; return -1;
  }

  // we work with grayscale images
  cvtColor(img_1_c, img_1, COLOR_BGR2GRAY);
  cvtColor(img_2_c, img_2, COLOR_BGR2GRAY);

  // feature detection, tracking
  vector<Point2f> points1, points2;        // vectors to store the coordinates of the feature points
  featureDetection(img_1, points1);        // detect features in img_1
  vector<uchar> status;
  featureTracking(img_1, img_2, points1, points2, status); // track those features to img_2

  // recovering the pose and the essential matrix
  Mat E, R, t, mask;
#if CV3
  E = findEssentialMat(points2, points1, K, RANSAC, 0.999, 1.0, mask);
#else
  Mat F = findFundamentalMat(points2, points1, FM_RANSAC, 3., 0.99, mask);
  E = K.t() * F * K;
#endif
  recoverPose(E, points2, points1, K, R, t, mask);
  cout << R << endl;

  Mat prevImage = img_2;
  Mat currImage;
  vector<Point2f> prevFeatures = points2;
  vector<Point2f> currFeatures;

  char filename[100];

  R_f = R.clone();
  t_f = t.clone();

  clock_t begin = clock();

  // namedWindow("Road facing camera", WINDOW_AUTOSIZE);// Create a window for display.
  // namedWindow("Trajectory", WINDOW_AUTOSIZE);// Create a window for display.

  Mat traj = Mat::zeros(600, 600, CV_8UC3);

  double bearing = 0;
  for (int numFrame = 2; numFrame < MAX_FRAME; numFrame++) {
    sprintf(filename, DATASET, numFrame);
    // cout << numFrame << endl;
    Mat currImage_c = imread(filename);
    cvtColor(currImage_c, currImage, COLOR_BGR2GRAY);
    vector<uchar> status;
    featureTracking(prevImage, currImage, prevFeatures, currFeatures, status);

#if CV3
    E = findEssentialMat(currFeatures, prevFeatures, K, RANSAC, 0.999, 1.0, mask);
#else
    Mat F = findFundamentalMat(currFeatures, prevFeatures, FM_RANSAC, 3., 0.99, mask);
    E = K.t() * F * K;
#endif

    recoverPose(E, currFeatures, prevFeatures, K, R, t, mask);
    cv::Mat euler;
    Rodrigues(R, euler);
    bearing += euler.at<double>(1);
    cout << numFrame << " " << 180 * bearing / 3.14159 << endl;

    Mat prevPts(2, prevFeatures.size(), CV_64F), currPts(2, currFeatures.size(), CV_64F);

    // this (x, y) combination makes sense as observed from the source code of triangulatePoints on GitHub
    for (int i = 0; i < prevFeatures.size(); i++) {
      prevPts.at<double>(0, i) = prevFeatures.at(i).x;
      prevPts.at<double>(1, i) = prevFeatures.at(i).y;

      currPts.at<double>(0, i) = currFeatures.at(i).x;
      currPts.at<double>(1, i) = currFeatures.at(i).y;
    }

    scale = getAbsoluteScale(numFrame, 0, t.at<double>(2));

    // cout << "Scale is " << scale << endl;

    if ((scale > 0.1)&&(t.at<double>(2) > t.at<double>(0)) && (t.at<double>(2) > t.at<double>(1))) {
      t_f = t_f + scale * (R_f * t);
      R_f = R * R_f;
    } else {
      // cout << "scale below 0.1, or incorrect translation" << endl;
    }

    // lines for printing results
    // myfile << t_f.at<double>(0) << " " << t_f.at<double>(1) << " " << t_f.at<double>(2) << endl;

    // a redetection is triggered in case the number of features being tracked go below a particular threshold
    if (prevFeatures.size() < MIN_NUM_FEAT) {
      // cout << "Number of tracked features reduced to " << prevFeatures.size() << endl;
      // cout << "trigerring redection" << endl;
      featureDetection(prevImage, prevFeatures);
      featureTracking(prevImage, currImage, prevFeatures, currFeatures, status);
    }

    prevImage = currImage.clone();
    prevFeatures = currFeatures;

    int x = int(t_f.at<double>(0)) + 300;
    int y = int(t_f.at<double>(2)) + 100;
    circle(traj, Point(x, y), 1, CV_RGB(255, 0, 0), 2);

    rectangle(traj, Point(10, 30), Point(550, 50), CV_RGB(0, 0, 0), CV_FILLED);
    sprintf(text, "Coordinates: x = %02fm y = %02fm z = %02fm", t_f.at<double>(0), t_f.at<double>(1), t_f.at<double>(2));
    putText(traj, text, textOrg, fontFace, fontScale, Scalar::all(255), thickness, 8);

    // imshow("Road facing camera", currImage_c);
    // imshow("Trajectory", traj);
    // waitKey(1);
  }

  clock_t end = clock();
  double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
  cout << "Total time taken: " << elapsed_secs << "s" << endl;

  // cout << R_f << endl;
  // cout << t_f << endl;

  return 0;
}
