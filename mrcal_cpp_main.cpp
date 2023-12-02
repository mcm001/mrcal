
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

class CholmodCtx {
public:
  cholmod_common Common, *cc;
  CholmodCtx() {
    cc = &Common;
    cholmod_l_start(cc);
  }

  ~CholmodCtx() { cholmod_l_finish(cc); }
};
static CholmodCtx cctx;

struct mrcal_result {
  bool success;
  std::vector<double> intrinsics;
  double rms_error;
  std::vector<double> residuals;
  cholmod_sparse* Jt;
  mrcal_calobject_warp_t calobject_warp;
  int Noutliers_board;
  // TODO standard devs

  mrcal_result() = default;
  mrcal_result(bool success_, std::vector<double> intrinsics_,
               double rms_error_, std::vector<double> residuals_,
               cholmod_sparse* Jt_, mrcal_calobject_warp_t calobject_warp_,
               int Noutliers_board_)
      : success{success_}, intrinsics{std::move(intrinsics_)},
        rms_error{rms_error_}, residuals{std::move(residuals_)}, Jt{Jt_},
        calobject_warp{calobject_warp_}, Noutliers_board{Noutliers_board_} {}
  mrcal_result(mrcal_result &&) = delete;
  ~mrcal_result();
};

struct Size {
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
    Size cameraRes) {
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
      1; // basically just non-splined should always be true
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
  for (int i = 0; i < Nobservations_board; i++) {
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
      observations_point; observations_point.reserve(std::max(Nobservations_point, 1));
  mrcal_observation_point_t *c_observations_point = observations_point.data();

  for (int i_observation = 0; i_observation < Nobservations_board;
       i_observation++) {
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
       i_observation++) {
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

  if (!lensmodel_one_validate_args(&mrcal_lensmodel, intrinsics, false)) {
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
  std::vector<double> c_b_packed_final_vec; c_b_packed_final_vec.reserve(Nstate);
  std::vector<double> c_x_final_vec; c_x_final_vec.reserve(Nmeasurements);
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

  cholmod_sparse* Jt = cholmod_l_allocate_sparse(
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
          calibration_object_height_n, verbose)) {
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
    int Nobservations_board) {
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
                                 bool do_check_layout) {
  int NlensParams = mrcal_lensmodel_num_params(mrcal_lensmodel);
  int NlensParams_have = intrinsics.size();
  if (NlensParams != NlensParams_have) {
    std::fprintf(stderr,"intrinsics.shape[-1] MUST be %d. Instead got %d", NlensParams,
         NlensParams_have);
    return false;
  }

  return true;
}


mrcal_result::~mrcal_result() {
  cholmod_l_free_sparse(&Jt, cctx.cc);
  // free(Jt.p);
  // free(Jt.i);
  // free(Jt.x);
  return;
}



int main() {
    
}