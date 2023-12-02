
/*
 * Copyright (C) Photon Vision.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <malloc.h>
#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <span>
#include <stdexcept>
#include <vector>
#include <iostream>

#include "cholmod.h"

#include "mrcal.h"

class CholmodCtx
{
public:
    cholmod_common Common, *cc;
    CholmodCtx()
    {
        cc = &Common;
        cholmod_l_start(cc);
    }

    ~CholmodCtx() { cholmod_l_finish(cc); }
};
static CholmodCtx cctx;

struct mrcal_result
{
    bool success;
    std::vector<double> intrinsics;
    double rms_error;
    std::vector<double> residuals;
    cholmod_sparse *Jt;
    mrcal_calobject_warp_t calobject_warp;
    int Noutliers_board;
    // TODO standard devs

    mrcal_result() = default;
    mrcal_result(bool success_, std::vector<double> intrinsics_,
                 double rms_error_, std::vector<double> residuals_,
                 cholmod_sparse *Jt_, mrcal_calobject_warp_t calobject_warp_,
                 int Noutliers_board_)
        : success{success_}, intrinsics{std::move(intrinsics_)},
          rms_error{rms_error_}, residuals{std::move(residuals_)}, Jt{Jt_},
          calobject_warp{calobject_warp_}, Noutliers_board{Noutliers_board_} {}
    mrcal_result(mrcal_result &&) = delete;
    ~mrcal_result();
};

struct Size
{
    int width, height;
};

// #define BARF(args...) std::fprintf(stderr, ##args)

// forward declarations for functions borrowed from mrcal-pywrap
static mrcal_problem_selections_t construct_problem_selections(
    int do_optimize_intrinsics_core, int do_optimize_intrinsics_distortions,
    int do_optimize_extrinsics, int do_optimize_frames,
    int do_optimize_calobject_warp, int do_apply_regularization,
    int do_apply_outlier_rejection, int Ncameras_intrinsics,
    int Ncameras_extrinsics, int Nframes, int Nobservations_board);

bool lensmodel_one_validate_args(mrcal_lensmodel_t *mrcal_lensmodel,
                                 std::vector<double> intrinsics,
                                 bool do_check_layout);

// Empty vector just to pass in so it's not NULL?
mrcal_point3_t observations_point[1];
mrcal_pose_t
    extrinsics_rt_fromref[1]; // Always zero for single camera, it seems?
mrcal_point3_t points[1];     // Seems to always to be None for single camera?

std::unique_ptr<mrcal_result> mrcal_main(
    // List, depth is ordered array observation[N frames, object_height,
    // object_width] = [x,y, weight] weight<0 means ignored)
    std::span<mrcal_point3_t> observations_board,
    // RT transform from camera to object
    std::span<mrcal_pose_t> frames_rt_toref,
    // Chessboard size, in corners (not squares)
    Size calobjectSize, double calibration_object_spacing,
    // res, pixels
    Size cameraRes)
{
    // Number of board observations we've got. List of boards. in python, it's
    // (number of chessboard pictures) x (rows) x (cos) x (3)
    // hard-coded to 8, since that's what I've got below
    int Nobservations_board = frames_rt_toref.size();

    // Looks like this is hard-coded to 0 in Python for initial single-camera
    // solve?
    int Nobservations_point = 0;

    int calibration_object_width_n =
        calobjectSize.width; // Object width, in # of corners
    int calibration_object_height_n =
        calobjectSize.height; // Object height, in # of corners

    // TODO set sizes and populate
    int imagersize[] = {cameraRes.width, cameraRes.height};

    mrcal_calobject_warp_t calobject_warp = {0, 0};

    // int Nobservations_point_triangulated = 0; // no clue what this is

    int Npoints = 0;       // seems like this is also unused? whack
    int Npoints_fixed = 0; // seems like this is also unused? whack

    int do_optimize_intrinsics_core =
        1;                                      // basically just non-splined should always be true
    int do_optimize_intrinsics_distortions = 1; // can skip intrinsics if we want
    int do_optimize_extrinsics = 1;             // can skip extrinsics if we want
    int do_optimize_frames = 1;
    int do_optimize_calobject_warp = 1;
    int do_apply_regularization = 1;
    int do_apply_outlier_rejection = 1; // can also skip

    mrcal_lensmodel_t mrcal_lensmodel;
    mrcal_lensmodel.type = MRCAL_LENSMODEL_OPENCV8; // TODO expose other models

    // pure pinhole guess for initial solve
    double cx = (cameraRes.width / 2.0) - 0.5;
    double cy = (cameraRes.height / 2.0) - 0.5;
    std::vector<double> intrinsics = {1200, 1200, cx, cy, 0, 0, 0, 0, 0, 0, 0, 0};

    // Number of cameras to solve for intrinsics
    int Ncameras_intrinsics = 1;

    // Hard-coded to match out 8 frames from above (borrowed from python)
    std::vector<mrcal_point3_t> indices_frame_camintrinsics_camextrinsics;
    // Frame index, camera number, (camera number)-1???
    for (int i = 0; i < Nobservations_board; i++)
    {
        indices_frame_camintrinsics_camextrinsics.push_back(
            {static_cast<double>(i), 0, -1});
    }

    // Pool is the raw observation backing array
    mrcal_point3_t *c_observations_board_pool = (observations_board.data());
    // mrcal_point3_t *c_observations_point_pool = observations_point;

    // Copy from board/point pool above, using some code borrowed from
    // mrcal-pywrap
    std::vector<mrcal_observation_board_t> observations_board_vec;
    observations_board_vec.reserve(Nobservations_board);
    mrcal_observation_board_t *c_observations_board = observations_board_vec.data();
    // Try to make sure we don't accidentally make a zero-length array or
    // something stupid
    std::vector<mrcal_observation_point_t>
        observations_point;
    observations_point.reserve(std::max(Nobservations_point, 1));
    mrcal_observation_point_t *c_observations_point = observations_point.data();

    for (int i_observation = 0; i_observation < Nobservations_board;
         i_observation++)
    {
        int32_t iframe =
            indices_frame_camintrinsics_camextrinsics.at(i_observation).x;
        int32_t icam_intrinsics =
            indices_frame_camintrinsics_camextrinsics.at(i_observation).y;
        int32_t icam_extrinsics =
            indices_frame_camintrinsics_camextrinsics.at(i_observation).z;

        c_observations_board[i_observation].icam.intrinsics = icam_intrinsics;
        c_observations_board[i_observation].icam.extrinsics = icam_extrinsics;
        c_observations_board[i_observation].iframe = iframe;
    }
    for (int i_observation = 0; i_observation < Nobservations_point;
         i_observation++)
    {
        int32_t i_point =
            indices_frame_camintrinsics_camextrinsics.at(i_observation).x;
        int32_t icam_intrinsics =
            indices_frame_camintrinsics_camextrinsics.at(i_observation).y;
        int32_t icam_extrinsics =
            indices_frame_camintrinsics_camextrinsics.at(i_observation).z;

        c_observations_point[i_observation].icam.intrinsics = icam_intrinsics;
        c_observations_point[i_observation].icam.extrinsics = icam_extrinsics;
        c_observations_point[i_observation].i_point = i_point;
    }

    int Ncameras_extrinsics = 0; // Seems to always be zero for single camera
    int Nframes =
        frames_rt_toref.size(); // Number of pictures of the object we've got
    // mrcal_observation_point_triangulated_t *observations_point_triangulated =
    //     NULL;

    if (!lensmodel_one_validate_args(&mrcal_lensmodel, intrinsics, false))
    {
        auto ret = std::make_unique<mrcal_result>();
        ret->success = false;
        return ret;
    }

    mrcal_problem_selections_t problem_selections = construct_problem_selections(
        do_optimize_intrinsics_core, do_optimize_intrinsics_distortions,
        do_optimize_extrinsics, do_optimize_frames, do_optimize_calobject_warp,
        do_apply_regularization, do_apply_outlier_rejection, Ncameras_intrinsics,
        Ncameras_extrinsics, Nframes, Nobservations_board);

    int Nstate = mrcal_num_states(
        Ncameras_intrinsics, Ncameras_extrinsics, Nframes, Npoints, Npoints_fixed,
        Nobservations_board, problem_selections, &mrcal_lensmodel);

    int Nmeasurements = mrcal_num_measurements(
        Nobservations_board, Nobservations_point,
        // observations_point_triangulated,
        // 0, // hard-coded to 0
        calibration_object_width_n, calibration_object_height_n,
        Ncameras_intrinsics, Ncameras_extrinsics, Nframes, Npoints, Npoints_fixed,
        problem_selections, &mrcal_lensmodel);

    // OK, now we should have everything ready! Just some final setup and then
    // call optimize

    // Residuals
    std::vector<double> c_b_packed_final_vec;
    c_b_packed_final_vec.reserve(Nstate);
    std::vector<double> c_x_final_vec;
    c_x_final_vec.reserve(Nmeasurements);
    double *c_b_packed_final = c_b_packed_final_vec.data();
    double *c_x_final = c_x_final_vec.data();

    // Seeds
    double *c_intrinsics = intrinsics.data();
    mrcal_pose_t *c_extrinsics = extrinsics_rt_fromref;
    mrcal_pose_t *c_frames = frames_rt_toref.data();
    mrcal_point3_t *c_points = points;
    mrcal_calobject_warp_t *c_calobject_warp = &calobject_warp;

    // in
    int *c_imagersizes = imagersize;
    auto point_min_range = -1.0, point_max_range = -1.0;
    mrcal_problem_constants_t problem_constants = {
        .point_min_range = point_min_range, .point_max_range = point_max_range};
    int verbose = 0;

    auto stats = mrcal_optimize(
        NULL, -1, c_x_final, Nmeasurements * sizeof(double), c_intrinsics,
        c_extrinsics, c_frames, c_points, c_calobject_warp, Ncameras_intrinsics,
        Ncameras_extrinsics, Nframes, Npoints, Npoints_fixed,
        c_observations_board, c_observations_point, Nobservations_board,
        Nobservations_point, c_observations_board_pool, &mrcal_lensmodel,
        c_imagersizes, problem_selections, &problem_constants,
        calibration_object_spacing, calibration_object_width_n,
        calibration_object_height_n, verbose, false);

    // and for fun, evaluate the jacobian
    // cholmod_sparse* Jt = NULL;
    int N_j_nonzero = _mrcal_num_j_nonzero(
        Nobservations_board, Nobservations_point, calibration_object_width_n,
        calibration_object_height_n, Ncameras_intrinsics, Ncameras_extrinsics,
        Nframes, Npoints, Npoints_fixed, c_observations_board,
        c_observations_point, problem_selections, &mrcal_lensmodel);

    cholmod_sparse *Jt = cholmod_l_allocate_sparse(
        static_cast<size_t>(Nstate), static_cast<size_t>(Nmeasurements),
        static_cast<size_t>(N_j_nonzero), 1, 1, 0, CHOLMOD_REAL, cctx.cc);

    // std::printf("Getting jacobian\n");
    if (!mrcal_optimizer_callback(
            c_b_packed_final, Nstate * sizeof(double), c_x_final,
            Nmeasurements * sizeof(double), Jt, c_intrinsics, c_extrinsics,
            c_frames, c_points, c_calobject_warp, Ncameras_intrinsics,
            Ncameras_extrinsics, Nframes, Npoints, Npoints_fixed,
            c_observations_board, c_observations_point, Nobservations_board,
            Nobservations_point, c_observations_board_pool, &mrcal_lensmodel,
            c_imagersizes, problem_selections, &problem_constants,
            calibration_object_spacing, calibration_object_width_n,
            calibration_object_height_n, verbose))
    {
        std::cerr << "callback failed!\n";
    }
    // std::cout << "Jacobian! " << std::endl;

    std::vector<double> residuals = {c_x_final, c_x_final + Nmeasurements};
    return std::make_unique<mrcal_result>(
        true, intrinsics, stats.rms_reproj_error__pixels, residuals, Jt,
        calobject_warp, stats.Noutliers);
}

// lifted from mrcal-pywrap.c
static mrcal_problem_selections_t construct_problem_selections(
    int do_optimize_intrinsics_core, int do_optimize_intrinsics_distortions,
    int do_optimize_extrinsics, int do_optimize_frames,
    int do_optimize_calobject_warp, int do_apply_regularization,
    int do_apply_outlier_rejection,

    int Ncameras_intrinsics, int Ncameras_extrinsics, int Nframes,
    int Nobservations_board)
{
    // By default we optimize everything we can
    if (do_optimize_intrinsics_core < 0)
        do_optimize_intrinsics_core = Ncameras_intrinsics > 0;
    if (do_optimize_intrinsics_distortions < 0)
        do_optimize_intrinsics_core = Ncameras_intrinsics > 0;
    if (do_optimize_extrinsics < 0)
        do_optimize_extrinsics = Ncameras_extrinsics > 0;
    if (do_optimize_frames < 0)
        do_optimize_frames = Nframes > 0;
    if (do_optimize_calobject_warp < 0)
        do_optimize_calobject_warp = Nobservations_board > 0;
    return {
        .do_optimize_intrinsics_core =
            static_cast<bool>(do_optimize_intrinsics_core),
        .do_optimize_intrinsics_distortions =
            static_cast<bool>(do_optimize_intrinsics_distortions),
        .do_optimize_extrinsics = static_cast<bool>(do_optimize_extrinsics),
        .do_optimize_frames = static_cast<bool>(do_optimize_frames),
        .do_optimize_calobject_warp =
            static_cast<bool>(do_optimize_calobject_warp),
        .do_apply_regularization = static_cast<bool>(do_apply_regularization),
        .do_apply_outlier_rejection =
            static_cast<bool>(do_apply_outlier_rejection),
        // .do_apply_regularization_unity_cam01 = false
    };
}

bool lensmodel_one_validate_args(mrcal_lensmodel_t *mrcal_lensmodel,
                                 std::vector<double> intrinsics,
                                 bool do_check_layout)
{
    int NlensParams = mrcal_lensmodel_num_params(mrcal_lensmodel);
    int NlensParams_have = intrinsics.size();
    if (NlensParams != NlensParams_have)
    {
        std::fprintf(stderr, "intrinsics.shape[-1] MUST be %d. Instead got %d", NlensParams,
                     NlensParams_have);
        return false;
    }

    return true;
}

mrcal_result::~mrcal_result()
{
    cholmod_l_free_sparse(&Jt, cctx.cc);
    // free(Jt.p);
    // free(Jt.i);
    // free(Jt.x);
    return;
}

int main(int argc, char **argv)
{
    std::cout << "Hello!\n";

    std::vector<double> board_pts =
        {325.516, 132.934, 1.0, 371.214, 134.351, 1.0, 415.623, 135.342, 1.0, 460.354, 136.823, 1.0, 504.145, 138.109, 1.0, 547.712, 139.65, 1.0, 594.0, 148.683, 1.0, 324.871, 176.873, 1.0, 369.412, 177.909, 1.0, 414.233, 179.545, 1.0, 457.929, 181.193, 1.0, 501.911, 181.665, 1.0, 545.353, 183.286, 1.0, 587.117, 184.587, 1.0, 323.335, 221.308, 1.0, 368.023, 221.689, 1.0, 412.79, 223.232, 1.0, 456.687, 223.741, 1.0, 499.676, 225.028, 1.0, 543.056, 226.144, 1.0, 584.376, 227.355, 1.0, 321.873, 264.356, 1.0, 366.604, 265.474, 1.0, 411.506, 265.928, 1.0, 454.473, 267.156, 1.0, 497.687, 267.316, 1.0, 540.8, 268.549, 1.0, 582.004, 268.906, 1.0, 321.069, 307.494, 1.0, 365.617, 308.399, 1.0, 409.188, 309.055, 1.0, 453.092, 309.161, 1.0, 495.585, 309.516, 1.0, 538.113, 310.626, 1.0, 579.114, 310.916, 1.0, 319.962, 351.063, 1.0, 363.211, 351.18, 1.0, 407.939, 351.029, 1.0, 450.832, 351.136, 1.0, 493.292, 351.66, 1.0, 535.927, 352.151, 1.0, 576.977, 352.415, 1.0, 317.523, 394.612, 1.0, 361.653, 393.122, 1.0, 405.486, 393.69, 1.0, 449.094, 393.107, 1.0, 490.867, 393.069, 1.0, 533.174, 393.251, 1.0, 573.45, 392.904, 1.0, 207.359, 161.061, 1.0, 256.83, 163.237, 1.0, 304.053, 165.752, 1.0, 349.537, 168.3, 1.0, 393.125, 170.923, 1.0, 436.193, 172.818, 1.0, 476.734, 174.922, 1.0, 206.2, 207.683, 1.0, 255.307, 209.547, 1.0, 303.05, 211.483, 1.0, 347.176, 213.29, 1.0, 391.548, 214.998, 1.0, 434.194, 216.182, 1.0, 475.306, 217.711, 1.0, 204.869, 254.591, 1.0, 253.717, 255.146, 1.0, 301.636, 256.939, 1.0, 346.212, 257.436, 1.0, 389.826, 258.667, 1.0, 432.929, 259.004, 1.0, 473.42, 260.297, 1.0, 203.314, 301.767, 1.0, 251.833, 301.487, 1.0, 299.666, 301.357, 1.0, 344.634, 301.545, 1.0, 387.881, 301.493, 1.0, 431.046, 302.38, 1.0, 471.777, 302.712, 1.0, 201.107, 348.792, 1.0, 249.8, 347.677, 1.0, 297.241, 347.004, 1.0, 343.254, 346.381, 1.0, 386.326, 345.487, 1.0, 429.81, 345.23, 1.0, 469.742, 345.034, 1.0, 199.756, 395.295, 1.0, 248.198, 394.029, 1.0, 295.721, 392.398, 1.0, 340.746, 390.831, 1.0, 384.77, 389.311, 1.0, 427.527, 388.627, 1.0, 468.236, 387.648, 1.0, 197.684, 442.702, 1.0, 246.477, 439.342, 1.0, 293.202, 437.257, 1.0, 339.3, 435.403, 1.0, 382.577, 432.917, 1.0, 425.605, 431.302, 1.0, 465.707, 429.225, 1.0, 305.709, 174.707, 1.0, 351.673, 176.16, 1.0, 397.419, 177.562, 1.0, 442.075, 179.037, 1.0, 487.177, 180.891, 1.0, 531.785, 181.86, 1.0, 573.738, 183.557, 1.0, 304.294, 219.62, 1.0, 350.203, 220.724, 1.0, 395.748, 221.699, 1.0, 440.862, 222.973, 1.0, 485.52, 224.85, 1.0, 530.185, 225.869, 1.0, 572.114, 227.503, 1.0, 303.243, 263.59, 1.0, 349.341, 265.627, 1.0, 394.469, 266.043, 1.0, 439.742, 267.237, 1.0, 484.055, 268.79, 1.0, 528.175, 269.724, 1.0, 570.69, 270.726, 1.0, 301.669, 309.033, 1.0, 347.288, 309.528, 1.0, 393.567, 310.66, 1.0, 437.619, 311.441, 1.0, 482.058, 312.254, 1.0, 526.403, 313.246, 1.0, 569.039, 313.931, 1.0, 299.327, 353.836, 1.0, 345.584, 354.487, 1.0, 391.137, 354.882, 1.0, 436.249, 355.728, 1.0, 480.324, 356.082, 1.0, 524.946, 356.456, 1.0, 566.89, 357.05, 1.0, 297.979, 399.116, 1.0, 344.187, 399.653, 1.0, 389.909, 399.152, 1.0, 434.862, 399.209, 1.0, 478.911, 400.062, 1.0, 522.668, 399.882, 1.0, 565.371, 400.272, 1.0, 296.078, 445.016, 1.0, 342.71, 444.04, 1.0, 387.822, 443.536, 1.0, 433.286, 443.428, 1.0, 476.779, 442.87, 1.0, 520.055, 442.343, 1.0, 562.414, 442.205, 1.0, 91.257764, 62.341333, 1.0, 156.367723, 66.97445, 1.0, 218.066065, 71.650665, 1.0, 276.386861, 76.251825, 1.0, 331.055492, 81.147211, 1.0, 383.696897, 84.814439, 1.0, 430.893194, 89.012836, 1.0, 91.833674, 123.430732, 1.0, 155.905789, 126.34495, 1.0, 217.913026, 129.702873, 1.0, 274.98218, 133.31974, 1.0, 329.372274, 135.975815, 1.0, 380.871511, 138.540811, 1.0, 427.956504, 141.086789, 1.0, 91.771236, 183.897303, 1.0, 156.00571, 185.474423, 1.0, 217.247203, 187.258936, 1.0, 274.219614, 188.919293, 1.0, 327.751591, 189.691818, 1.0, 378.443874, 191.387865, 1.0, 425.847568, 191.85023, 1.0, 91.861943, 243.611033, 1.0, 155.182405, 243.511549, 1.0, 216.832614, 243.122519, 1.0, 273.129283, 242.355705, 1.0, 325.343307, 241.717585, 1.0, 375.851167, 241.553501, 1.0, 423.055064, 241.803709, 1.0, 91.671178, 302.440746, 1.0, 155.273091, 300.177818, 1.0, 215.216509, 297.399528, 1.0, 272.414663, 294.579327, 1.0, 323.101889, 292.983598, 1.0, 373.559284, 291.323639, 1.0, 419.835057, 290.277082, 1.0, 92.857058, 359.214116, 1.0, 154.937554, 355.849957, 1.0, 213.863967, 351.613097, 1.0, 269.476977, 347.721722, 1.0, 321.803464, 344.059031, 1.0, 371.3437, 341.393939, 1.0, 417.516845, 338.833116, 1.0, 93.07796, 415.613843, 1.0, 154.037428, 409.923307, 1.0, 212.834834, 404.066145, 1.0, 267.771666, 398.70259, 1.0, 319.298246, 393.980064, 1.0, 368.22885, 389.593709, 1.0, 414.674171, 385.356734, 1.0, 203.417, 161.504, 1.0, 239.114, 163.886, 1.0, 273.107, 166.449, 1.0, 305.916, 168.563, 1.0, 337.115, 170.991, 1.0, 368.03, 172.639, 1.0, 397.193, 175.197, 1.0, 202.091, 195.309, 1.0, 237.449, 197.254, 1.0, 271.811, 199.126, 1.0, 303.884, 201.339, 1.0, 335.799, 202.775, 1.0, 366.476, 203.962, 1.0, 395.621, 205.477, 1.0, 200.763, 229.055, 1.0, 235.846, 230.374, 1.0, 270.15, 231.701, 1.0, 302.963, 233.051, 1.0, 334.118, 233.94, 1.0, 364.861, 235.145, 1.0, 393.631, 236.292, 1.0, 199.378, 263.126, 1.0, 234.485, 263.471, 1.0, 268.802, 263.894, 1.0, 301.142, 265.078, 1.0, 332.232, 265.327, 1.0, 363.139, 265.668, 1.0, 391.948, 266.685, 1.0, 198.029, 296.998, 1.0, 233.112, 296.261, 1.0, 266.88, 296.323, 1.0, 299.629, 296.302, 1.0, 330.424, 296.424, 1.0, 361.084, 297.002, 1.0, 389.842, 296.814, 1.0, 195.902, 329.523, 1.0, 231.127, 329.233, 1.0, 265.381, 329.073, 1.0, 297.542, 327.951, 1.0, 328.969, 326.898, 1.0, 359.115, 327.24, 1.0, 388.128, 327.274, 1.0, 194.57, 362.996, 1.0, 229.508, 361.475, 1.0, 263.279, 360.993, 1.0, 295.782, 359.146, 1.0, 326.44, 358.773, 1.0, 357.322, 357.793, 1.0, 385.821, 357.147, 1.0, 171.432587, 62.91091, 1.0, 233.543966, 66.989676, 1.0, 292.892198, 71.025168, 1.0, 349.449118, 75.488547, 1.0, 402.475665, 78.823783, 1.0, 454.664044, 82.293706, 1.0, 502.641518, 85.776245, 1.0, 171.514734, 121.993103, 1.0, 232.390154, 125.308964, 1.0, 291.615192, 128.775042, 1.0, 347.095808, 131.455464, 1.0, 399.571916, 133.920527, 1.0, 451.377575, 136.702216, 1.0, 499.408046, 139.005337, 1.0, 170.628776, 181.228863, 1.0, 231.887269, 183.495513, 1.0, 289.549706, 184.943596, 1.0, 345.09963, 186.724366, 1.0, 397.291107, 187.669673, 1.0, 448.012251, 188.937037, 1.0, 495.593336, 189.964319, 1.0, 170.167998, 238.958158, 1.0, 230.55839, 239.418254, 1.0, 288.497209, 239.644103, 1.0, 342.515469, 239.491195, 1.0, 394.007115, 239.553513, 1.0, 444.372561, 240.019514, 1.0, 491.944262, 240.14174, 1.0, 169.316729, 295.157425, 1.0, 229.919699, 293.84609, 1.0, 285.963235, 292.93516, 1.0, 339.829832, 291.170168, 1.0, 391.046096, 291.017874, 1.0, 441.65549, 290.262712, 1.0, 488.484545, 289.606238, 1.0, 169.391253, 351.146683, 1.0, 227.941254, 348.429636, 1.0, 284.725293, 345.328308, 1.0, 337.367128, 343.36398, 1.0, 388.802075, 341.231567, 1.0, 438.530539, 340.104779, 1.0, 485.397165, 338.584278, 1.0, 167.777378, 405.478817, 1.0, 226.74825, 401.012785, 1.0, 282.079972, 397.14788, 1.0, 335.558834, 393.843829, 1.0, 385.729546, 390.894412, 1.0, 434.287148, 387.675643, 1.0, 480.912754, 385.395124, 1.0, 170.954619, 66.207185, 1.0, 232.925674, 70.329078, 1.0, 291.929905, 74.309458, 1.0, 348.618405, 78.647759, 1.0, 401.907159, 82.127252, 1.0, 454.073162, 85.908807, 1.0, 502.384304, 89.612773, 1.0, 170.672483, 125.415122, 1.0, 231.462866, 128.773806, 1.0, 291.032499, 132.398581, 1.0, 345.684337, 134.915663, 1.0, 399.486674, 137.530287, 1.0, 450.909032, 139.320663, 1.0, 498.894327, 142.166906, 1.0, 170.039566, 184.623157, 1.0, 231.346178, 186.659774, 1.0, 289.163911, 188.055595, 1.0, 344.132387, 189.912225, 1.0, 396.321555, 191.204305, 1.0, 447.459807, 192.313964, 1.0, 495.143206, 193.384192, 1.0, 169.696611, 241.964717, 1.0, 229.931254, 242.875875, 1.0, 288.01937, 243.058687, 1.0, 341.607179, 243.256923, 1.0, 393.716058, 243.405927, 1.0, 443.892988, 243.204055, 1.0, 491.834639, 243.220219, 1.0, 168.652822, 299.444695, 1.0, 228.891661, 297.586808, 1.0, 285.893939, 296.780702, 1.0, 340.007199, 294.868026, 1.0, 390.975809, 294.235148, 1.0, 441.273511, 293.692402, 1.0, 487.912647, 293.046936, 1.0, 168.141104, 354.722532, 1.0, 227.178901, 352.045371, 1.0, 284.087214, 349.40292, 1.0, 337.067904, 346.926664, 1.0, 388.459654, 345.137176, 1.0, 437.933373, 343.278757, 1.0, 484.875402, 342.18047, 1.0, 167.262818, 409.231675, 1.0, 225.776135, 404.722118, 1.0, 281.542602, 401.181308, 1.0, 334.813427, 397.384595, 1.0, 385.508089, 394.555612, 1.0, 434.342519, 391.542815, 1.0, 480.929907, 388.713162, 1.0, 167.096579, 75.563045, 1.0, 229.04439, 79.769831, 1.0, 288.347806, 83.685341, 1.0, 345.133847, 87.745306, 1.0, 398.242697, 91.371038, 1.0, 450.258727, 94.791385, 1.0, 498.481567, 98.006336, 1.0, 166.457718, 134.764653, 1.0, 228.030388, 137.434067, 1.0, 287.553592, 141.303498, 1.0, 343.134211, 143.527946, 1.0, 395.604435, 146.123987, 1.0, 447.141897, 148.657081, 1.0, 495.483977, 150.93944, 1.0, 165.628349, 193.670954, 1.0, 227.43617, 195.5245, 1.0, 285.517103, 197.434608, 1.0, 341.055025, 198.983142, 1.0, 393.692971, 199.687896, 1.0, 444.170013, 200.967691, 1.0, 492.007105, 201.736728, 1.0, 165.260826, 251.970544, 1.0, 225.90027, 251.973265, 1.0, 284.098946, 252.060422, 1.0, 338.834414, 251.78845, 1.0, 390.193044, 251.929631, 1.0, 440.831715, 251.885293, 1.0, 488.336721, 252.191396, 1.0, 163.791281, 309.386063, 1.0, 224.929397, 307.232453, 1.0, 282.176961, 305.725654, 1.0, 335.984449, 304.607775, 1.0, 387.27907, 303.196963, 1.0, 437.747368, 302.540835, 1.0, 485.297854, 301.494266, 1.0, 163.968029, 365.050099, 1.0, 223.201096, 362.148888, 1.0, 280.445895, 359.3519, 1.0, 333.640557, 356.491297, 1.0, 385.61138, 354.094168, 1.0, 435.165143, 352.729433, 1.0, 482.091338, 350.966958, 1.0, 163.221401, 419.674165, 1.0, 221.857647, 415.074118, 1.0, 277.713041, 411.798257, 1.0, 331.76678, 407.068828, 1.0, 382.058162, 404.138822, 1.0, 431.852815, 401.145043, 1.0, 478.272757, 398.091691, 1.0};

    std::vector<mrcal_point3_t> board;
    for (int i = 0; i < board_pts.size(); i += 3)
    {
        board.push_back({
            board_pts[i + 0],
            board_pts[i + 1],
            board_pts[i + 2]
        });
    }

    std::vector<double> frames = {
        0.18955483458775926, -0.033038727866531614, 0.022707065075538276, 0.003881530461773854, -0.060050636989376564, 0.6751569929686021, 0.11341870891157015, -0.21416096044771266, 0.025656700549271303, -0.0586624615643108, -0.041434082345533044, 0.6409249466153064, 0.09003894451110257, -0.04465290188401688, 0.023802586908097726, -0.007512243337051627, -0.036381438094227246, 0.664915160152289, 0.22306250816468168, -0.286404122257168, 0.015308161204149554, -0.09275585627950353, -0.07164118510169161, 0.5024534336391102, 0.16554669186808843, -0.23640360992915935, 0.039916560276319485, -0.08525049313901799, -0.05675606115679195, 0.9005871543142424, 0.21152155778240092, -0.23601682761573972, 0.02168544211427134, -0.06129277965976731, -0.07426199231199616, 0.5186553595621118, 0.2070459866214724, -0.23677885924427713, 0.02150145710415093, -0.061584079540542375, -0.07275206818298144, 0.5183080961175048, 0.19937011038568678, -0.243807370146021, 0.02085660175780747, -0.0631523001187754, -0.06861389990046424, 0.5163805642400598};
    std::vector<mrcal_pose_t> frames_rt_toref = {};
    for (int i = 0; i < frames.size(); i += 6)
    {
        frames_rt_toref.push_back({
            {
                frames[i + 0],
                frames[i + 1],
                frames[i + 2]
            }, {
                frames[i + 3],
                frames[i + 4],
                frames[i + 5]
            }
        });
    }
    auto result = mrcal_main(
        board, frames_rt_toref, {7, 7}, 0.0254, {640, 480});

    std::cout << (result->success ? "YAY" : "NAY") << std::endl;

    return 0;
}