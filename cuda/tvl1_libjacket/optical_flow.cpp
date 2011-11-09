//
// Chris McClanahan - 2011
//
// Adapted from: http://gpu4vision.icg.tugraz.at/index.php?content=downloads.php
//   "An Improved Algorithm for TV-L1 Optical Flow"
//
// More info: http://mcclanahoochie.com/blog/portfolio/gpu-tv-l1-optical-flow-with-libjacket/
//

#include <iostream>
#include <fstream>
#include <stdio.h>
#include <math.h>
#include <jacket.h>
#include <jacket_gfx.h>
#include <jacket_timing.h>
#include <string.h>

#include <cv.h>
#include <cxcore.h>
#include <highgui.h>

using namespace jkt;
using namespace std;
using namespace cv;

// control
const float pfactor = 0.72;   // scale each pyr level by this amount
const int max_plevels = 5;    // number of pyramid levels
const int max_iters = 5;      // u v w update loop
const float lambda = 40;      // smoothness constraint
const int max_warps = 3;      // warping u v warping

// functions
int  grab_frame(Mat& img, char* filename);
void create_pyramids(f32& im1, f32& im2, f32& pyr1, f32& pyr2);
void process_pyramids(f32& pyr1, f32& pyr2, f32& u, f32& v);
void tv_l1_dual(f32& u, f32& v, f32& p, f32& w, f32& I1, f32& I2, int level);
void optical_flow_tvl1(Mat& img1, Mat& img2, Mat& u, Mat& v);
void display_flow(f32& I2, f32& u, f32& v);
void MatToFloat(const Mat& thing, float* thing2);
void FloatToMat(float const* thing, Mat& thing2);

// misc
int plevels = max_plevels;
const int n_dual_vars = 6;
static int cam_init = 0;
static int pyr_init = 0;
VideoCapture  capture;
int pyr_M[max_plevels + 1];
int pyr_N[max_plevels + 1];
f32 pyr1, pyr2;

// macros
#define MSG(msg,...) do {                                   \
	        fprintf(stdout,__FILE__":%d(%s) " msg "\n",     \
	                __LINE__, __FUNCTION__, ##__VA_ARGS__); \
	        fflush(stdout);                                 \
	    } while (0)


// ===== main =====
void optical_flow_tvl1(Mat& img1, Mat& img2, Mat& mu, Mat& mv) {

    // extract cv image 1
    Mat mi1(img1.rows, img1.cols, CV_8UC1);
    cvtColor(img1.t(), mi1, CV_BGR2GRAY);
    mi1.convertTo(mi1, CV_32FC1);
    float* fi1 = (float*)mi1.data;
    f32 I1 = f32(fi1, img1.rows, img1.cols) / 255.0f;

    // extract cv image 2
    Mat mi2(img2.rows, img2.cols, CV_8UC1);
    cvtColor(img2.t(), mi2, CV_BGR2GRAY);
    mi2.convertTo(mi2, CV_32FC1);
    float* fi2 = (float*)mi2.data;
    f32 I2 = f32(fi2, img2.rows, img2.cols) / 255.0f;

#if 0
    // runs
    int nruns = 4;
    // warmup
    create_pyramids(I1, I2, pyr1, pyr2);
    f32 ou, ov;
    process_pyramids(pyr1, pyr2, ou, ov);
    // timing
    timer::tic();
    for (int i = 0; i < nruns; i++) {
        create_pyramids(I1, I2, pyr1, pyr2);
        process_pyramids(pyr1, pyr2, ou, ov);
    }
    MSG("elapsed time (sec): %f", timer::toc() / (float)nruns);
#else
    // timing
    timer::tic();
    // pyramids
    create_pyramids(I1, I2, pyr1, pyr2);
    // flow
    f32 ou, ov;
    process_pyramids(pyr1, pyr2, ou, ov);
    // timing
    MSG("elapsed time: %f", timer::toc());
#endif

    // output
#if 1
    // to opencv
    FloatToMat(ou.T().host(), mu);
    FloatToMat(ov.T().host(), mv);
#else
    // to libjacket
    display_flow(I2, ou, ov);
#endif
}


void MatToFloat(const Mat& thing, float* thing2) {
    int tmp = 0;
    for (int i = 0; i < thing.rows; i++) {
        const float* fptr = thing.ptr<float>(i);
        for (int j = 0; j < thing.cols; j++)
            { thing2[tmp++] = fptr[j]; }
    }
}


void FloatToMat(float const* thing, Mat& thing2) {
    int tmp = 0;
    for (int i = 0; i < thing2.rows; ++i) {
        float* fptr = thing2.ptr<float>(i);
        for (int j = 0; j < thing2.cols; ++j)
            { fptr[j] = thing[tmp++]; }
    }
}


void display_flow(f32& I2, f32& u, f32& v) {
#if 1
    // show in libjacket
    colormap("bone");
    subplot(2, 2, 1); imagesc(I2);                  title("input");
    subplot(2, 2, 2); imagesc(u);                   title("u");
    subplot(2, 2, 3); imagesc(v);                   title("v");
    subplot(2, 2, 4); imagesc((abs(v) + abs(u)));   title("u+v");
    drawnow();
#else
    // show in opencv
    int M = I2.dims()[0];
    int N = I2.dims()[1];
    Mat mu(M, N, CV_32FC1);
    Mat mv(M, N, CV_32FC1);
    FloatToMat(u.T().host(), mu);
    FloatToMat(v.T().host(), mv);
    imshow("u", mu);
    imshow("v", mv);
#endif
}


void display_flow(const Mat& u, const Mat& v) {
    //calculate angle and magnitude
    cv::Mat magnitude, angle;
    cv::cartToPolar(u, v, magnitude, angle, true);
    //translate magnitude to range [0;1]
    double mag_max, mag_min;
    cv::minMaxLoc(magnitude, &mag_min, &mag_max);
    magnitude.convertTo(magnitude, -1, 1.0 / mag_max);
    //build hsv image
    cv::Mat _hsv[3], hsv;
    _hsv[0] = angle;
    _hsv[1] = Mat::ones(angle.size(), CV_32F);
    _hsv[2] = magnitude;
    cv::merge(_hsv, 3, hsv);
    //convert to BGR and show
    Mat bgr;
    cv::cvtColor(hsv, bgr, CV_HSV2BGR);
    cv::imshow("optical flow", bgr);
}


int grab_frame(Mat& img, char* filename) {

    // camera/image setup
    if (!cam_init) {
        if (filename != NULL) {
            capture.open(filename);
        } else {
            float rescale = 0.54;
            int w = 640 * rescale;
            int h = 480 * rescale;
            capture.open(0); //try to open
            capture.set(CV_CAP_PROP_FRAME_WIDTH, w);  capture.set(CV_CAP_PROP_FRAME_HEIGHT, h);
        }
        if (!capture.isOpened()) { cerr << "open video device fail\n" << endl; return 0; }
        capture >> img; capture >> img;
        if (img.empty()) { cout << "load image fail " << endl; return 0; }
        namedWindow("cam", CV_WINDOW_KEEPRATIO);
        printf(" img = %d x %d \n", img.cols, img.rows);
        cam_init = 1;
    }

    // get frames
    capture.grab();
    capture.retrieve(img);
    imshow("cam", img);

    if (waitKey(10) >= 0) { return 0; }
    else { return 1; }
}


void gen_pyramid_sizes(f32& im1) {
    gdims mnk = im1.dims();
    float sM = mnk[0];
    float sN = mnk[1];
    // store resizing
    for (int level = 0; level <= plevels; ++level) {
        if (level == 0) {
        } else {
            sM *= pfactor;
            sN *= pfactor;
        }
        pyr_M[level] = (int)(sM + 0.5f);
        pyr_N[level] = (int)(sN + 0.5f);
        MSG(" pyr %d: %d x %d ", level, (int)sM, (int)sN);
        if (sM < 21 || sN < 21 || level >= max_plevels) { plevels = level; break; }
    }
}

void create_pyramids(f32& im1, f32& im2, f32& pyr1, f32& pyr2) {

    if (!pyr_init) {
        // list of h,w
        gen_pyramid_sizes(im1);

        // init
        pyr1 = f32::zeros(pyr_M[0], pyr_N[0], plevels);
        pyr2 = f32::zeros(pyr_M[0], pyr_N[0], plevels);
        pyr_init = 1;
    }

    // create
    for (int level = 0; level < plevels; level++) {
        if (level == 0) {
            pyr1(span, span, level) = im1;
            pyr2(span, span, level) = im2;
        } else {
            seq spyi = seq(pyr_M[level - 1]);
            seq spxi = seq(pyr_N[level - 1]);
            f32 small1 = resize(pyr1(spyi, spxi, level - 1), pyr_M[level], pyr_N[level], JKT_RSZ_Bilinear);
            f32 small2 = resize(pyr2(spyi, spxi, level - 1), pyr_M[level], pyr_N[level], JKT_RSZ_Bilinear);
            seq spyo = seq(pyr_M[level]);
            seq spxo = seq(pyr_N[level]);
            pyr1(spyo, spxo, level) = small1;
            pyr2(spyo, spxo, level) = small2;
        }
    }
}


void process_pyramids(f32& pyr1, f32& pyr2, f32& ou, f32& ov) {
    f32 p, u, v, w;

    // pyramid loop
    for (int level = plevels - 1; level >= 0; level--) {
        if (level == plevels - 1) {
            u  = f32::zeros(pyr_M[level], pyr_N[level]);
            v  = f32::zeros(pyr_M[level], pyr_N[level]);
            w  = f32::zeros(pyr_M[level], pyr_N[level]);
            p  = f32::zeros(pyr_M[level], pyr_N[level], n_dual_vars);
        } else {
            // propagate
            f32 u_ =  resize(u, pyr_M[level], pyr_N[level], JKT_RSZ_Nearest);
            f32 v_ =  resize(v, pyr_M[level], pyr_N[level], JKT_RSZ_Nearest);
            f32 w_ =  resize(w, pyr_M[level], pyr_N[level], JKT_RSZ_Nearest);
            f32 p_ = f32::zeros(pyr_M[level], pyr_N[level], n_dual_vars);
            gfor(f32 ndv, n_dual_vars) {
                p_(span, span, ndv) = resize(p(span, span, ndv), pyr_M[level], pyr_N[level], JKT_RSZ_Nearest);
            }
            u = u_;  v = v_;  p = p_;  w = w_;
        }

        // extract
        seq spy = seq(pyr_M[level]);
        seq spx = seq(pyr_N[level]);
        f32 I1 = pyr1(spy, spx, level);
        f32 I2 = pyr2(spy, spx, level);

        // ===== core ====== //
        tv_l1_dual(u, v, p, w, I1, I2, level);
        // ===== ==== ====== //
    }

    // output
    ou = u;
    ov = v;
}


//  5x5 derivative kernels
float dx_kernel[] = {
    -1.0f / 12.0f, 8.0f / 12.0f, 0.0f / 12.0f, -8.0f / 12.0f, 1.0f / 12.0f,
    -1.0f / 12.0f, 8.0f / 12.0f, 0.0f / 12.0f, -8.0f / 12.0f, 1.0f / 12.0f,
    -1.0f / 12.0f, 8.0f / 12.0f, 0.0f / 12.0f, -8.0f / 12.0f, 1.0f / 12.0f,
    -1.0f / 12.0f, 8.0f / 12.0f, 0.0f / 12.0f, -8.0f / 12.0f, 1.0f / 12.0f,
    -1.0f / 12.0f, 8.0f / 12.0f, 0.0f / 12.0f, -8.0f / 12.0f, 1.0f / 12.0f,
};
float dy_kernel[] = {
    -1.0f / 12.0f, -1.0f / 12.0f, -1.0f / 12.0f, -1.0f / 12.0f, -1.0f / 12.0f,
    8.0f / 12.0f,  8.0f / 12.0f,  8.0f / 12.0f,  8.0f / 12.0f,  8.0f / 12.0f,
    0.0f / 12.0f,  0.0f / 12.0f,  0.0f / 12.0f,  0.0f / 12.0f,  0.0f / 12.0f,
    -8.0f / 12.0f, -8.0f / 12.0f, -8.0f / 12.0f, -8.0f / 12.0f - 8.0f / 12.0f,
    1.0f / 12.0f,  1.0f / 12.0f,  1.0f / 12.0f,  1.0f / 12.0f,  1.0f / 12.0f,
};
f32 dx = f32(dx_kernel, gdims(5, 5), jktHostPointer);
f32 dy = f32(dy_kernel, gdims(5, 5), jktHostPointer);
// convolutions
void diffs(f32& Ix, f32& Iy, f32& I1, f32& I2) {
    Ix = conv2(I1, dx, jktConvSame) - conv2(I2, dx, jktConvSame);
    Iy = conv2(I1, dy, jktConvSame) - conv2(I2, dy, jktConvSame);
}


void warping(f32& Ix, f32& Iy, f32& It, f32& I1, f32& I2, f32& u, f32& v) {

    gdims mnk = I2.dims();
    int M = mnk[0];
    int N = mnk[1];
    // f32 idx, idy; meshgrid(idx, idy, f32(seq(N)), f32(seq(M)));
    f32 idx = repmat(f32(seq(N)).T(), M, 1) + 1;
    f32 idy = repmat(f32(seq(M)), 1, N) + 1;

    f32 idxx0 = idx + u;
    f32 idyy0 = idy + v;
    f32 idxx = max(1, min(N, idxx0));
    f32 idyy = max(1, min(M, idyy0));

    // interp2 based warp ()
    It = interp2(idy, idx, I2, idyy, idxx) - I1;

    // display_flow(It, idxx, idyy);
    // waitKey(0);

#if 0
    // convolution based warp
    diffs(Ix, Iy, I1, I2);
#else
    // interp2 based warp ()
    f32 idxm = max(1, min(N, idxx - 0.5f));
    f32 idxp = max(1, min(N, idxx + 0.5f));
    f32 idym = max(1, min(M, idyy - 0.5f));
    f32 idyp = max(1, min(M, idyy + 0.5f));
    Ix = interp2(idy, idx, I2, idyy, idxp) - interp2(idy, idx, I2, idyy, idxm);
    Iy = interp2(idy, idx, I2, idyp, idxx) - interp2(idy, idx, I2, idym, idxx);
#endif

    /* ^ interp2 should be cubic; that may fix things */
}


void dxym(f32& Id, f32 I0x, f32 I0y) {
    // divergence
    gdims mnk = I0x.dims();
    int M = mnk[0];
    int N = mnk[1];

    f32 x0 = f32::zeros(M, N);
    f32 x1 = f32::zeros(M, N);
    x0(seq(N - 1), seq(M)) = I0x(seq(N - 1), seq(M));
    x1(seq(1,  N), seq(M)) = I0x(seq(1,  N), seq(M));

    f32 y0 = f32::zeros(M, N);
    f32 y1 = f32::zeros(M, N);
    y0(seq(N), seq(M - 1)) = I0y(seq(N), seq(M - 1));
    y1(seq(N), seq(1,  M)) = I0y(seq(N), seq(1,  M));

    Id = (x0 - x1) + (y0 - y1);
}

void dxyp(f32& Ix, f32& Iy, f32& I0) {
    // shifts
    gdims mnk = I0.dims();
    int M = mnk[0];
    int N = mnk[1];

    f32 y0 = I0;
    f32 y1 = I0;
    y0(seq(0, M - 2), span) = I0(seq(1, M - 1), span);

    f32 x0 = I0;
    f32 x1 = I0;
    x0(span, seq(0, N - 2)) = I0(span, seq(1, N - 1));

    Ix = (x0 - x1);  Iy = (y0 - y1);

//    Ix = circshift(I0, -1, 0) - I0;
//    Iy = circshift(I0, 0, -1) - I0;
}



void tv_l1_dual(f32& u, f32& v, f32& p, f32& w, f32& I1, f32& I2, int level) {

    float L = sqrtf(8.0f);
    float tau   = 1 / L;
    float sigma = 1 / L;

    float eps_u = 0.00f;
    float eps_w = 0.00f;
    float gamma = 0.02f;

    f32 u_ = u;
    f32 v_ = v;
    f32 w_ = w;

    for (int j = 0; j < max_warps; j++) {

        f32 u0 = u;
        f32 v0 = v;

        // warping
        f32 Ix, Iy, It;   warping(Ix, Iy, It, I1, I2, u0, v0);

        // gradients
        f32 I_grad_sqr = jkt::max(float(1e-6), f32(power(Ix, 2) + power(Iy, 2) + gamma * gamma));

        // inner loop
        for (int k = 0; k < max_iters; ++k) {

            // dual =====

            // shifts
            f32 u_x, u_y;    dxyp(u_x, u_y, u_);
            f32 v_x, v_y;    dxyp(v_x, v_y, v_);
            f32 w_x, w_y;    dxyp(w_x, w_y, w_);

            // update dual
            p(span, span, 0) = (p(span, span, 0) + sigma * u_x) / (1 + sigma * eps_u);
            p(span, span, 1) = (p(span, span, 1) + sigma * u_y) / (1 + sigma * eps_u);
            p(span, span, 2) = (p(span, span, 2) + sigma * v_x) / (1 + sigma * eps_u);
            p(span, span, 3) = (p(span, span, 3) + sigma * v_y) / (1 + sigma * eps_u);

            p(span, span, 4) = (p(span, span, 4) + sigma * w_x) / (1 + sigma * eps_w);
            p(span, span, 5) = (p(span, span, 5) + sigma * w_y) / (1 + sigma * eps_w);

            // normalize
            f32 reprojection = max(1, sqrt(power(p(span, span, 0), 2) + power(p(span, span, 1), 2) +
                                           power(p(span, span, 2), 2) + power(p(span, span, 3), 2)));

            p(span, span, 0) = p(span, span, 0) / reprojection;
            p(span, span, 1) = p(span, span, 1) / reprojection;
            p(span, span, 2) = p(span, span, 2) / reprojection;
            p(span, span, 3) = p(span, span, 3) / reprojection;

            reprojection = max(1, sqrt(power(p(span, span, 4), 2) + power(p(span, span, 5), 2)));

            p(span, span, 4) = p(span, span, 4) / reprojection;
            p(span, span, 5) = p(span, span, 5) / reprojection;

            // primal =====

            // divergence
            f32 div_u;   dxym(div_u, p(span, span, 0), p(span, span, 1));
            f32 div_v;   dxym(div_v, p(span, span, 2), p(span, span, 3));
            f32 div_w;   dxym(div_w, p(span, span, 4), p(span, span, 5));

            // old
            u_ = u;
            v_ = v;
            w_ = w;

            // update
            u = u + tau * div_u;
            v = v + tau * div_v;
            w = w + tau * div_w;

            // indexing
            f32 rho  = It + (u - u0) * Ix + (v - v0) * Iy + gamma * w;
            b8 idx1 = rho      <  -tau * lambda * I_grad_sqr;
            b8 idx2 = rho      >   tau * lambda * I_grad_sqr;
            b8 idx3 = abs(rho) <=  tau * lambda * I_grad_sqr;

            u = u + tau * lambda * (Ix * idx1) ;
            v = v + tau * lambda * (Iy * idx1) ;
            w = w + tau * lambda * gamma * idx1;

            u = u - tau * lambda * (Ix * idx2) ;
            v = v - tau * lambda * (Iy * idx2) ;
            w = w - tau * lambda * gamma * idx2;

            u = u - rho * idx3 * Ix / I_grad_sqr;
            v = v - rho * idx3 * Iy / I_grad_sqr;
            w = w - rho * idx3 * gamma / I_grad_sqr;

            // propagate
            u_ = 2 * u - u_;
            v_ = 2 * v - v_;
            w_ = 2 * w - w_;

        }

        // output
        const unsigned hw[] = {3, 3};
        u = medfilt2(u, hw);
        v = medfilt2(v, hw);

    } /* j < warps */
}


// =======================================

int main(int argc, char* argv[]) {

    // video file or usb camera
    Mat cam_img, prev_img, disp_u, disp_v;
    int is_images = 0;
    if (argc == 2) { grab_frame(prev_img, argv[1]); } // video
    else if (argc == 3) {
        prev_img = imread(argv[1]); cam_img = imread(argv[2]);
        is_images = 1;
    } else { grab_frame(prev_img, NULL); } // usb camera

    // results
    int mm = prev_img.rows;  int nn = prev_img.cols;
    disp_u = Mat::zeros(mm, nn, CV_32FC1);
    disp_v = Mat::zeros(mm, nn, CV_32FC1);
    printf("img %d x %d \n", mm, nn);

    // process main
    if (is_images) {
        // show
        imshow("i", cam_img);
        // process files
        optical_flow_tvl1(prev_img, cam_img, disp_u, disp_v);
        // show
        imshow("u", disp_u);
        imshow("v", disp_v);
        display_flow(disp_u, disp_v);
        waitKey(0);
        // // write
        // writeFlo(disp_u, disp_v);
    } else {
        // process loop
        while (grab_frame(cam_img, NULL)) {
            try {
                // process
                optical_flow_tvl1(prev_img, cam_img, disp_u, disp_v);
                // frames
                prev_img = cam_img.clone();
                // show
                imshow("u", disp_u);
                imshow("v", disp_v);
                display_flow(disp_u, disp_v);
            } catch (gexception& e) {
                cout << e.what() << endl;
                throw;
            }
        }
    }

    return 0;
}
