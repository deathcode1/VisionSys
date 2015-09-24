 /*
  * Scratch.cpp
  *
  *  Created on: Sep 4, 2015
  *      Author: ubuntu
  */

#include <opencv2/opencv.hpp>
#include <opencv2/opencv_modules.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <libfreenect.hpp>
#include <libfreenect.h>
#include <iostream>
#include <fstream>
#include <cmath>
#include <vector>
#include <pthread.h>
#include <limits.h>
#include <time.h>
#include <iomanip>

#define Iterations 2

int             DELAY_BLUR = 100;
int             MAX_KERNEL_LENGTH = 31;

pthread_t freenect_thread;
using namespace cv;
using namespace std;

int             display_dst(int delay);

class           myMutex {
  public:
    myMutex() {
	pthread_mutex_init(&m_mutex, NULL);
    } void          lock() {
	pthread_mutex_lock(&m_mutex);
    }
    void            unlock() {
	pthread_mutex_unlock(&m_mutex);
    }
  private:
    pthread_mutex_t m_mutex;
};

class           MyFreenectDevice:public
    Freenect::FreenectDevice {
  public:
    MyFreenectDevice(freenect_context * _ctx,
		     int _index):Freenect::FreenectDevice(_ctx, _index),
                    m_buffer_depth(FREENECT_DEPTH_11BIT),
                    m_buffer_rgb(FREENECT_VIDEO_RGB),
                    m_gamma(2048),
                    m_new_rgb_frame(false),
                    m_new_depth_frame(false),
                    depthMat(Size(640, 480), CV_16UC1),
                    rgbMat(Size(640, 480), CV_8UC3, Scalar(0)),
                    ownMat(Size(640, 480), CV_8UC3, Scalar(0)) {

	for (unsigned int i = 0; i < 2048; i++) {
	    float           v = i / 2048.0;
	                    v = std::pow(v, 3) * 6;
	                    m_gamma[i] = v * 6 * 256;
    }}
    // Do not call directly even in child
	void VideoCallback(void *_rgb, uint32_t timestamp) {
	std::cout << "RGB callback" << std::endl;
	m_rgb_mutex.lock();
	uint8_t        *rgb = static_cast < uint8_t * >(_rgb);
	rgbMat.data = rgb;
	m_new_rgb_frame = true;
	m_rgb_mutex.unlock();
    }
    ;

    // Do not call directly even in child
    void            DepthCallback(void *_depth, uint32_t timestamp) {
	std::cout << "Depth callback" << std::endl;
	m_depth_mutex.lock();
	uint16_t       *depth = static_cast < uint16_t * >(_depth);
	depthMat.data = (uchar *) depth;
	m_new_depth_frame = true;
	m_depth_mutex.unlock();
    }

    bool            getVideo(Mat & output) {
	m_rgb_mutex.lock();
	if (m_new_rgb_frame) {
	    cv::cvtColor(rgbMat, output, CV_RGB2BGR);
	    m_new_rgb_frame = false;
	    m_rgb_mutex.unlock();
	    return true;
	} else {
	    m_rgb_mutex.unlock();
	    return false;
	}
    }

    bool            getDepth(Mat & output) {
	m_depth_mutex.lock();
	if (m_new_depth_frame) {
	    depthMat.copyTo(output);
	    m_new_depth_frame = false;
	    m_depth_mutex.unlock();
	    return true;
	} else {
	    m_depth_mutex.unlock();
	    return false;
	}
    }
  private:
    std::vector < uint8_t > m_buffer_depth;
    std::vector < uint8_t > m_buffer_rgb;
    std::vector < uint16_t > m_gamma;
    Mat             depthMat;
    Mat             rgbMat;
    Mat             ownMat;
    myMutex         m_rgb_mutex;
    myMutex         m_depth_mutex;
    bool            m_new_rgb_frame;
    bool            m_new_depth_frame;
};

// Thanks to Frederic Philips for the code; without you, this thresholding 
// 
// 
// 
// would be a lot harder!

void
morphOps(Mat & thresh)
{
    // Create structuring element that will be used to "dilate" and
    // "erode" image.

    // MORPH_RECT - a rectangular structuring element

    // Element chosen here is a 5px by 5px rectangle for erosion
    Mat             erodeElement =
	getStructuringElement(MORPH_RECT, Size(5, 5));

    // Element chosen here is a 1px by 1px rectangle for dilation
    Mat             dilateElement =
	getStructuringElement(MORPH_RECT, Size(1, 1));

    // erode(thresh, thresh, erodeElement, Point(-1,-1), Iterations);
    erode(thresh, thresh, erodeElement, Point(-1, -1), Iterations);

    dilate(thresh, thresh, dilateElement, Point(-1, -1), Iterations);
}
Freenect::Freenect freenect;
MyFreenectDevice *
    device;


static
                std::string
dbl2str(double d)
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(10) << d;	// convert double
    // to string w
    // fixed notation, 
    // hi precision
    std::string s = ss.str();	// output to std::string
    s.erase(s.find_last_not_of('0') + 1, std::string::npos);	// remove
    // trailing 
    // 000s
    // (123.1200 
    // =>
    // 123.12, 
    // 123.000 
    // =>
    // 123.)
    return (s[s.size() - 1] == '.') ? s.substr(0, s.size() - 1) : s;	// remove 
									// 
    // dangling 
    // decimal 
    // (123. 
    // => 
    // 123)
}

int
main()
{

    initModule_features2d();
    time_t
	start,
	end;
    int
	counter = 0;
    double
	sec;
    double
	fps;
    bool
    die(false);
    int
                    iter(0);
    int
                    i_snap(0);
    string
	text;
    string
    filename("snapshot");
    string
    suffix(".png");
    vector < KeyPoint > Keypoints;
    vector < vector < Point > >contours;
    vector < Vec4i > hierarchy;
    MyFreenectDevice & device =
	freenect.createDevice < MyFreenectDevice > (0);
    freenect_device *f_dev;
    freenect_context *f_ctx;


    //freenect_open_device(f_ctx, f_dev, 1);
    int freenect_angle = 0;
    int freenect_led;
    OrbFeatureDetector
	detector;
    double
    MotorPos;
    Mat
    depthMat(Size(640, 480), CV_16UC1);
    Mat
    bgrMat(Size(640, 480), CV_8UC3, Scalar(0));
    Mat
    labMat(Size(640, 480), CV_8UC3, Scalar(0));
    Mat
    bgrMatEqualized(Size(640, 480), CV_8U, Scalar(0));
    Mat
    depthf(Size(640, 480), CV_8UC1);
    Mat
    ownMat(Size(640, 480), CV_8UC3, Scalar(0));
    Mat
    inFrame(Size(640, 480), CV_8UC3, Scalar(0));
    Mat
    outFrame(Size(640, 480), CV_8UC3, Scalar(0));
    Mat
    postProc(Size(640, 480), CV_8UC3, Scalar(0));
    Mat
    hsvMat(Size(640, 480), CV_8UC3, Scalar(0));
    Mat
    rgbMat(Size(640, 480), CV_8UC3, Scalar(0));
    Mat
    threshMat(Size(640, 480), CV_8U, Scalar(0));
    Mat
    invThreshMat(Size(640, 480), CV_8U, Scalar(0));
    Mat
    invBGRThresh(Size(640, 480), CV_8U, Scalar(0));
    Mat
    maskedThresh(Size(640, 480), CV_8U, Scalar(0));
    Mat
    blackMat(Size(640, 480), CV_8U, Scalar(0));
    Mat
    maskedDepth(Size(640, 480), CV_8UC1);
    Mat
    bgrThresh(Size(640, 480), CV_8UC1, Scalar(0));
    Mat
    greyMat(Size(640, 480), CV_8UC1, Scalar(0));
    Mat
    canny_output(Size(640, 480), CV_8UC1, Scalar(0));

    time(&start);
    // HSV
    // Scalar min(0, 32, 0);
    // Scalar max(30, 255, 255);


    // RGB
    // Scalar min(101, 63, 21);
    // Scalar max(153, 75, 33);


    // BGR
    // Scalar min(0, 0, 0);
    // Scalar max(255, 0, 0);

    int
                    lowThreshold;
    int
                    edgeThresh = 1;

    // namedWindow("Output", CV_WINDOW_AUTOSIZE);
    // namedWindow("depth", CV_WINDOW_AUTOSIZE);
    namedWindow("Post", CV_WINDOW_AUTOSIZE);
    createTrackbar("Min Threshold:", "Post", &lowThreshold, 100);	// , 
									// 
    // 
    // 
    // CannyThreshold);

    device.startVideo();
    device.startDepth();

    while (die == false) {
	device.getVideo(bgrMat);
	// device.getVideo(postProc);

	device.getDepth(depthMat);
	// cv::imshow("Output", bgrMat);

	MotorPos = freenect_get_tilt_degs(freenect_get_tilt_state(f_dev));

	cv::cvtColor(bgrMat, outFrame, CV_BGR2GRAY);
	// detector.detect(outFrame, Keypoints);
	// drawKeypoints(bgrMat, Keypoints, postProc, Scalar(0, 0, 255)/*, 
	// 
	// 
	// 
	// DrawMatchesFlags::DRAW_RICH_KEYPOINTS*/);
	// cvSmooth( bgrMat, bgrMat,CV_GAUSSIAN,9,9, 0);

	// BETA LAB EQH
	cvtColor(bgrMat, labMat, CV_BGR2Lab);

	vector < Mat > labPlanes(3);
	split(labMat, labPlanes);

	Mat
	    dst;
	equalizeHist(labPlanes[0], dst);
	equalizeHist(labPlanes[0], dst);
	equalizeHist(labPlanes[0], dst);
	dst.copyTo(labPlanes[0]);
	merge(labPlanes, labMat);
	cvtColor(labMat, bgrMatEqualized, CV_Lab2BGR);


	// BETA LAB EQH

	// GaussianBlur( threshMat, threshMat, Size(1, 1), 0, 0);
	// equalizeHist( threshMat, threshMatEqualized);
	cvtColor(bgrMatEqualized, hsvMat, CV_BGR2HSV);
	// cvtColor(rgbMat, hsvMat, CV_RGB2HSV);



	inRange(hsvMat, Scalar(100, 0, 0), Scalar(140, 255, 255),
		threshMat);
	// cvtColor(bgrMat, greyMat, CV_BGR2GRAY);
	// erode(threshMat, threshMat2, 0, Point(0, 0), 4,
	// BORDER_CONSTANT);
	// dlate( threshMat2, threshMat3, 0, Point (0, 0));
	// inRange(hsvMat, Scalar(165, 100, 0), Scalar(180, 255, 255),
	// threshMat2);

	// cvtColor(hsvMat, bgrMat, CV_HSV2BGR);



	// medianBlur(threshMat, threshMat, 3);
	morphOps(threshMat);
	// morphOps(threshMat);
	// morphOps(threshMat);

	Canny(threshMat, canny_output, lowThreshold, lowThreshold * 3, 3);
	findContours(canny_output, contours, hierarchy, CV_RETR_TREE,
		     CV_CHAIN_APPROX_SIMPLE, Point(0, 0));

	// adaptiveThreshold( greyMat, bgrThresh, 255,
	// CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 13, 1);
	// adaptiveThreshold( outFrame, bgrThresh, 255,
	// CV_ADAPTIVE_THRESH_GAUSSIAN_C, CV_THRESH_BINARY, 3, 1); 

	// bitwise_not(threshMat, invThreshMat);

	// maskedThresh = invBGRThresh - threshMat;
	postProc = bgrMat;

	detector.detect(threshMat, Keypoints);
	//drawKeypoints(bgrMat, Keypoints, postProc, Scalar(0, 0, 255)	/* ,
	//								 * DrawMatchesFlags::DRAW_RICH_KEYPOINTS */ );
	drawContours(postProc, contours, -1, Scalar(0, 255, 255), 2, 8,
     hierarchy, INT_MAX, Point(0, 0));
	//fillPoly(postProc, contours, 1, -1, Scalar(255, 255, 255), 8, 0, Point(0,0));
	// BEGIN FPS CODE
	time(&end);
	counter++;
	sec = difftime(end, start);
	fps = counter / sec;
	if (counter == (INT_MAX - 1000))
	    counter = 0;
	// if (counter >30) {
	text = dbl2str(fps);
	putText(postProc, text, Point(30, 30), FONT_HERSHEY_PLAIN, 1,
		Scalar(255, 255, 255), 2, 8, false);
	putText(postProc, dbl2str(MotorPos), Point(30, 60), FONT_HERSHEY_PLAIN, 1,
			Scalar(255, 255, 255), 2, 8, false);
	// }
	cv::imshow("Post", postProc);
	// cv::imshow("Post 2", bgrMatEqualized);
	// cv::imshow("Post", bgrThresh);
	// cv::imshow("Post", threshMat);
	 //depthMat.convertTo(depthf, CV_8UC1, 255.0 / 2048.0);

	// bitwise_not ( threshMat, invThreshMat);
	// bitwise_not (threshMat, invThreshMat);
	// invThreshMat.convertTo(invThreshMat16, CV_16U);
	// maskedDepth = depthf - invThreshMat;
	// maskedDepth16.convertTo(maskedDepth, CV_8UC1, 255.0 / 2048.0);
	// maskedDepth = depthf - invThreshMat;
	// maskedDepth.setTo(depthf, threshMat);
	// bitwise_and(depthf, blackMat, maskedDepth, threshMat);
	// cv::imshow("depth", maskedDepth);



	// cout << device.getTiltDegs();
	char            k = cvWaitKey(1);

	 //if (k == 119) { freenect_set_tilt_degs( f_dev, MotorPos + 10); }

	if (k == 27) {
	    cvDestroyWindow("rgb");
	    cvDestroyWindow("depth");
	    cvDestroyWindow("Post");
	    break;
	}
	if (k == 8) {
	    std::ostringstream file;
	    file << filename << i_snap << suffix;
	    cv::imwrite(file.str(), bgrMat);
	    i_snap++;
	}

	if (iter >= 10000)
	    break;
	iter++;

    }
    device.stopVideo();
    device.stopDepth();
    return 0;

}
