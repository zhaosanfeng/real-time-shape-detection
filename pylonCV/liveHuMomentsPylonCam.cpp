// Include files to use OpenCV API.
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include "opencv2/opencv.hpp"

// Include files to use the PYLON API.
#include "pylon/PylonIncludes.h"

// Use sstream to create image names including integer
#include <sstream>

// Namespace for using pylon objects.
using namespace Pylon;

// Namespace for using GenApi objects
using namespace GenApi;

// Namespace for using opencv objects.
using namespace cv;

// Namespace for using cout.
using namespace std;

// Number of images to be grabbed.
static const uint32_t c_countOfImagesToGrab = 40000; // test fps
const float huScale = 4e9;

// Function declarations - TODO: move to file which will be #included
double preprocessAndComputeOrientation(Mat& src, const int thresh = 100);
void drawAxis(Mat&, Point, Point, Scalar, const float);
double getOrientation(const vector<Point> &, Mat&);
int setCameraParams(INodeMap& nodemap, int64_t newWidth, int64_t newHeight);

int main(int argc, char* argv[])
{
    // The exit code of the sample application.
    int exitCode = 0;

    // Automagically call PylonInitialize and PylonTerminate to ensure the pylon runtime system
    // is initialized during the lifetime of this object.
    Pylon::PylonAutoInitTerm autoInitTerm;

    try
    {
        // Create an instant camera object with the camera device found first.
        cout << "Creating Camera..." << endl;
        CInstantCamera camera( CTlFactory::GetInstance().CreateFirstDevice());
        cout << "Camera Created." << endl;
        // Print the model name of the camera.
        cout << "Using device " << camera.GetDeviceInfo().GetModelName() << endl;

        INodeMap& nodemap = camera.GetNodeMap();
        // Open the camera for accessing the parameters.
        camera.Open();
        // set acquisition resolution and offsets
        if( setCameraParams(nodemap, 600, 800) ) // TODO: if camera is zoomed enough, take whole picture!
            cout << "CAMERA PARAMS NOT SET";

        // The parameter MaxNumBuffer can be used to control the count of buffers
        // allocated for grabbing. The default value of this parameter is 10.
        camera.MaxNumBuffer = 2;

        // create pylon image format converter and pylon image
        CImageFormatConverter formatConverter;
        formatConverter.OutputPixelFormat= PixelType_BGR8packed;
        CPylonImage pylonImage;
#if 0
        cout << "Create an OpenCV video creator\n";
        VideoWriter cvVideoCreator;
        // Create an OpenCV image
        Mat openCvImage;
        
        cout << "define the video frame size.\n";
        //cv::Size frameSize = Size((int)width->GetValue(), (int)height->GetValue());
        cv::Size frameSize = Size(600, 800);
        //cout << "Video frame size: " << (int)width->GetValue() << ", " << (int)height->GetValue() << endl;
        
        cout << "set the codec and frame rate\n";
        if(recordVideo)
            cvVideoCreator.open("NutsVideo.avi", CV_FOURCC('D', 'I', 'V', 'X'), 20, frameSize, true);
#endif   

        // Create an OpenCV image
        Mat openCvImage;

        // Start the grabbing of c_countOfImagesToGrab images.
        // The camera device is parameterized with a default configuration which
        // sets up free-running continuous acquisition.
        camera.StartGrabbing( c_countOfImagesToGrab);

        // This smart pointer will receive the grab result data.
        CGrabResultPtr ptrGrabResult;

        // --- Benchmark FPS rate of Camera + algorithm chain ---
        // Start and end times
        time_t start, end;
        // Start time
        time(&start);
    
        // Camera.StopGrabbing() is called automatically by the RetrieveResult() method
        // when c_countOfImagesToGrab images have been retrieved.
        while ( camera.IsGrabbing())
        {
            // Wait for an image and then retrieve it. A timeout of 5000 ms is used.
            camera.RetrieveResult( 5000, ptrGrabResult, TimeoutHandling_ThrowException);

            // Image grabbed successfully?
            if (ptrGrabResult->GrabSucceeded())
            {
                // Access the image data.
                // cout << "SizeX: " << ptrGrabResult->GetWidth() << endl;
                // cout << "SizeY: " << ptrGrabResult->GetHeight() << endl;
                const uint8_t *pImageBuffer = (uint8_t *) ptrGrabResult->GetBuffer();
                // cout << "Gray value of first pixel: " << (uint32_t) pImageBuffer[0] << endl << endl;

                // Convert the grabbed buffer to pylon imag
                formatConverter.Convert(pylonImage, ptrGrabResult);
                // Create an OpenCV image out of pylon image
                openCvImage= cv::Mat(ptrGrabResult->GetHeight(), ptrGrabResult->GetWidth(), CV_8UC3, (uint8_t *) pylonImage.GetBuffer());

                /*
                 * Hu Moments computation
                 */
                preprocessAndComputeOrientation(openCvImage);
                /*
                 * Moments computation end
                 */

                // Create a display window
                namedWindow( "OpenCV Display Window", CV_WINDOW_NORMAL);//AUTOSIZE //FREERATIO
                // Display the current image with opencv
                imshow( "OpenCV Display Window", openCvImage);
                // Define a timeout for customer's input in ms.
                // '0' means indefinite, i.e. the next image will be displayed after closing the window
                // '1' means live stream
                waitKey(1);

            }
            
            else
            {
                //cout << "Error: " << ptrGrabResult->GetErrorCode() << " " << ptrGrabResult->GetErrorDescription() << endl;
            }
        }

        // --- FPS benchmark ---
        // End Time
        time(&end);

        // Time elapsed
        double seconds = difftime (end, start);
        cout << "Time taken : " << seconds << " seconds" << endl;
         
        // Calculate frames per second
        double fps  = c_countOfImagesToGrab / seconds;
        cout << "Estimated frames per second : " << fps << endl;
        // --- End FPS Benchmark ---
    }
    catch (GenICam::GenericException &e)
    {
        // Error handling.
        cerr << "An exception occurred." << endl
        << e.GetDescription() << endl;
        exitCode = 1;
    }

    // Comment the following two lines to disable waiting on exit.
    cerr << endl << "Press Enter to exit." << endl;
    while( cin.get() != '\n');

    return exitCode;
}



double preprocessAndComputeOrientation(Mat& src, const int thresh)
{
    // Return value
    double angle;

    Mat gray;
    cvtColor(src, gray, COLOR_BGR2GRAY);

    /// Convert image to binary
    Mat bw;
    //threshold(gray, bw, 50, 255, CV_THRESH_BINARY | CV_THRESH_OTSU);
    /// Detect edges using canny
    Canny( gray, bw, thresh, thresh*2, 3 );
    
    namedWindow( "Edges", CV_WINDOW_NORMAL);//AUTOSIZE
    imshow("Edges", bw);
    //! [contours]
    // Find all the contours in the thresholded image
    vector<Vec4i> hierarchy;
    vector<vector<Point> > contours;
    findContours(bw, contours, hierarchy, CV_RETR_LIST, CV_CHAIN_APPROX_NONE);

    Rect boundingBox;
    Mat roi;

    for (size_t i = 0; i < contours.size(); ++i)
    {
        // Calculate the area of each contour
        double area = contourArea(contours[i]);
        // Ignore contours that are too small or too large
        if (area < 1e3 || area > 1e6) continue;

        // Region of interest is around object - actual contour
        boundingBox = boundingRect(contours[i]);
        // Take the subset of bw image
        roi = bw(boundingBox);

        // Pad the ROI to multiple of 8 columns for OpenCL
        int numOfColumnsNeeded = 0;
        if(roi.cols % 8 != 0)
        {
            numOfColumnsNeeded = 8 - (roi.cols % 8);
            printf("Unable to perform openCL computations - image width must be a multiplication of 8 and needs additional %d columns!\n", numOfColumnsNeeded);
            Mat pad = Mat::zeros(roi.rows, numOfColumnsNeeded, CV_8UC1);
            hconcat(roi, pad, roi);
        }

        // Draw a box
        rectangle(src, boundingBox, Scalar(0, 255, 255), 2);

        // Show ROI
        imshow("ROI", roi);

        printf("area = %f for contour %lu\n", area, i);

        // Compute Hu moments of a single contour
        Moments mu = moments( roi, false );
        double hu[7];
        HuMoments(mu, hu);
        printf("Hu invariants for contour %zu:\n", i);
        for( int j = 0; j < 7; j++ )
            printf("[%d]=%.4e ", j+1, hu[j]);
        printf("\n");

        /// Show 7th Hu moment as an arrow from the mass center
        Point2f mc = Point2f( mu.m10/mu.m00 , mu.m01/mu.m00 );  
        Point hu_orient = Point(static_cast<int>(mc.x) , static_cast<int>(mc.y+huScale*(hu[6]))); // 7th Hu moment as a vertical arrow
        drawAxis(src, mc, hu_orient, Scalar(255, 255, 0), 5);


        // Draw each contour only for visualisation purposes
        drawContours(src, contours, static_cast<int>(i), Scalar(0, 0, 255), 2, 8, hierarchy, 0);
        // Find the orientation of each shape
        angle = getOrientation(contours[i], src);
    }
    //! [contours]

    return angle;
}


/**
 * @function drawAxis
 */
void drawAxis(Mat& img, Point p, Point q, Scalar colour, const float scale = 200)
{
//! [visualization1]
    double angle;
    double hypotenuse;
    angle = atan2( (double) p.y - q.y, (double) p.x - q.x ); // angle in radians
    hypotenuse = sqrt( (double) (p.y - q.y) * (p.y - q.y) + (p.x - q.x) * (p.x - q.x));
//    double degrees = angle * 180 / CV_PI; // convert radians to degrees (0-180 range)
//    cout << "Degrees: " << abs(degrees - 180) << endl; // angle in 0-360 degrees range

    // Here we lengthen the arrow by a factor of scale
    q.x = (int) (p.x - scale * hypotenuse * cos(angle));
    q.y = (int) (p.y - scale * hypotenuse * sin(angle));
    line(img, p, q, colour, 1, CV_AA);

    // create the arrow hooks
    p.x = (int) (q.x + 9 * cos(angle + CV_PI / 4));
    p.y = (int) (q.y + 9 * sin(angle + CV_PI / 4));
    line(img, p, q, colour, 1, CV_AA);

    p.x = (int) (q.x + 9 * cos(angle - CV_PI / 4));
    p.y = (int) (q.y + 9 * sin(angle - CV_PI / 4));
    line(img, p, q, colour, 1, CV_AA);
//! [visualization1]
}

/**
 * @function getOrientation
 */
double getOrientation(const vector<Point> &pts, Mat &img)
{
//! [pca]
    //Construct a buffer used by the pca analysis
    int sz = static_cast<int>(pts.size());
    Mat data_pts = Mat(sz, 2, CV_64FC1);
    for (int i = 0; i < data_pts.rows; ++i)
    {
        data_pts.at<double>(i, 0) = pts[i].x;
        data_pts.at<double>(i, 1) = pts[i].y;
    }

    //Perform PCA analysis
    PCA pca_analysis(data_pts, Mat(), CV_PCA_DATA_AS_ROW);

    //Store the center of the object
    Point cntr = Point(static_cast<int>(pca_analysis.mean.at<double>(0, 0)),
                      static_cast<int>(pca_analysis.mean.at<double>(0, 1)));

    //Store the eigenvalues and eigenvectors
    vector<Point2d> eigen_vecs(2);
    vector<double> eigen_val(2);
    for (int i = 0; i < 2; ++i)
    {
        eigen_vecs[i] = Point2d(pca_analysis.eigenvectors.at<double>(i, 0),
                                pca_analysis.eigenvectors.at<double>(i, 1));

        eigen_val[i] = pca_analysis.eigenvalues.at<double>(0, i);
    }

//! [pca]
//! [visualization]
    // Draw the principal components
    circle(img, cntr, 3, Scalar(255, 0, 255), 2);
    Point p1 = cntr + 0.02 * Point(static_cast<int>(eigen_vecs[0].x * eigen_val[0]), static_cast<int>(eigen_vecs[0].y * eigen_val[0]));
    Point p2 = cntr - 0.02 * Point(static_cast<int>(eigen_vecs[1].x * eigen_val[1]), static_cast<int>(eigen_vecs[1].y * eigen_val[1]));
    drawAxis(img, cntr, p1, Scalar(0, 255, 0), 1);
    drawAxis(img, cntr, p2, Scalar(255, 255, 0), 5);

    double angle = atan2(eigen_vecs[0].y, eigen_vecs[0].x); // orientation in radians
//! [visualization]

    return angle;
}

int setCameraParams(INodeMap& nodemap, int64_t newWidth, int64_t newHeight)
{
    // Get the integer nodes describing the AOI.
    CIntegerPtr offsetX( nodemap.GetNode( "OffsetX"));
    CIntegerPtr offsetY( nodemap.GetNode( "OffsetY"));
    CIntegerPtr width( nodemap.GetNode( "Width"));
    CIntegerPtr height( nodemap.GetNode( "Height"));

    // On Basler acA2000-165umNIR and acA2000-165uc cameras the offsets are read-only.

    //TODO: Some properties have restrictions. Use GetInc/GetMin/GetMax to make sure you set a valid value.
    width->SetValue(newWidth);
    height->SetValue(newHeight);

    cout << "OffsetX          : " << offsetX->GetValue() << endl;
    cout << "OffsetY          : " << offsetY->GetValue() << endl;

    cout << "Width            : " << width->GetValue() << endl;
    cout << "Height           : " << height->GetValue() << endl;

    // Test is the value is set correctly
    if (width->GetValue() == newWidth && height->GetValue() == newHeight)
        return 0;
    else
        return 1;
}
