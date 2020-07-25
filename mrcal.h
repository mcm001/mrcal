#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "basic_points.h"
#include "poseutils.h"


// unconstrained 6DOF pose containing a rodrigues rotation and a translation
typedef struct
{
    point3_t r,t;
} pose_t;

// An observation of a calibration board. Each "observation" is ONE camera
// observing a board
typedef struct
{
    // indexes the extrinsics array. -1 means "at coordinate system reference"
    int  i_cam_extrinsics;
    // indexes the intrinsics array
    int  i_cam_intrinsics;
    int  i_frame;
} observation_board_t;

typedef struct
{
    // indexes the extrinsics array. -1 means "at coordinate system reference"
    int  i_cam_extrinsics;
    // indexes the intrinsics array
    int  i_cam_intrinsics;
    int  i_point;

    // Observed pixel coordinates
    // .x, .y are the pixel observations
    // .z is the weight of the observation. Most of the weights are expected to
    // be 1.0, which implies that the noise on the observation is gaussian,
    // independent on x,y, and has standard deviation of
    // observed_pixel_uncertainty. observed_pixel_uncertainty scales inversely
    // with the weight.
    point3_t px;
} observation_point_t;



typedef struct
{
    double focal_xy [2];
    double center_xy[2];
} intrinsics_core_t;

// names of the lens models, intrinsic parameter counts. A parameter count of
// <=0 means the parameter count is dynamic and will be computed by
// mrcal_getNlensParams(). This also implies that this model has some
// configuration that affects the parameter count
#define LENSMODEL_NOCONFIG_LIST(_)                                    \
    _(LENSMODEL_PINHOLE, 4)                                           \
    _(LENSMODEL_STEREOGRAPHIC, 4) /* Simple stereographic-only model */ \
    _(LENSMODEL_OPENCV4, 8)                                           \
    _(LENSMODEL_OPENCV5, 9)                                           \
    _(LENSMODEL_OPENCV8, 12)                                          \
    _(LENSMODEL_OPENCV12,16)   /* available in OpenCV >= 3.0.0) */    \
    _(LENSMODEL_CAHVOR,  9)                                           \
    _(LENSMODEL_CAHVORE, 13)   /* CAHVORE is CAHVOR + E + linearity */
#define LENSMODEL_WITHCONFIG_LIST(_)                                  \
    _(LENSMODEL_SPLINED_STEREOGRAPHIC,      0)
#define LENSMODEL_LIST(_)                       \
    LENSMODEL_NOCONFIG_LIST(_)                  \
    LENSMODEL_WITHCONFIG_LIST(_)

// parametric models have no extra configuration, and no precomputed data
typedef struct {} LENSMODEL_PINHOLE__config_t;
typedef struct {} LENSMODEL_STEREOGRAPHIC__config_t;
typedef struct {} LENSMODEL_OPENCV4__config_t;
typedef struct {} LENSMODEL_OPENCV5__config_t;
typedef struct {} LENSMODEL_OPENCV8__config_t;
typedef struct {} LENSMODEL_OPENCV12__config_t;
typedef struct {} LENSMODEL_CAHVOR__config_t;
typedef struct {} LENSMODEL_CAHVORE__config_t;

typedef struct {} LENSMODEL_PINHOLE__precomputed_t;
typedef struct {} LENSMODEL_STEREOGRAPHIC__precomputed_t;
typedef struct {} LENSMODEL_OPENCV4__precomputed_t;
typedef struct {} LENSMODEL_OPENCV5__precomputed_t;
typedef struct {} LENSMODEL_OPENCV8__precomputed_t;
typedef struct {} LENSMODEL_OPENCV12__precomputed_t;
typedef struct {} LENSMODEL_CAHVOR__precomputed_t;
typedef struct {} LENSMODEL_CAHVORE__precomputed_t;

#define MRCAL_ITEM_DEFINE_ELEMENT(name, type, pybuildvaluecode, PRIcode,SCNcode, bitfield, cookie) type name bitfield;

_Static_assert(sizeof(uint16_t) == sizeof(unsigned short int), "I need a short to be 16-bit. Py_BuildValue doesn't let me just specify that. H means 'unsigned short'");


// The splined stereographic models have the spline and projection configuration
// parameters
#define MRCAL_LENSMODEL_SPLINED_STEREOGRAPHIC_CONFIG_LIST(_, cookie)    \
    /* Maximum degree of each 1D polynomial. This is almost certainly 2 */ \
    /* (quadratic splines, C1 continuous) or 3 (cubic splines, C2 continuous) */ \
    _(order,        uint16_t, "H", PRIu16,SCNu16, , cookie)             \
    /* We have a Nx by Ny grid of control points */                     \
    _(Nx,           uint16_t, "H", PRIu16,SCNu16, , cookie)             \
    _(Ny,           uint16_t, "H", PRIu16,SCNu16, , cookie)             \
    /* The horizontal field of view. Not including fov_y. It's proportional with */ \
    /* Ny and Nx */                                                     \
    _(fov_x_deg,    uint16_t, "H", PRIu16,SCNu16, , cookie)
typedef struct
{
    MRCAL_LENSMODEL_SPLINED_STEREOGRAPHIC_CONFIG_LIST(MRCAL_ITEM_DEFINE_ELEMENT, )
} LENSMODEL_SPLINED_STEREOGRAPHIC__config_t;

// The splined stereographic models configuration parameters can be used to
// compute the segment size. I cache this computation
typedef struct
{
    // The distance between adjacent knots (1 segment) is u_per_segment =
    // 1/segments_per_u
    double segments_per_u;
} LENSMODEL_SPLINED_STEREOGRAPHIC__precomputed_t;

#define LENSMODEL_OPENCV_FIRST LENSMODEL_OPENCV4
#define LENSMODEL_OPENCV_LAST  LENSMODEL_OPENCV12
#define LENSMODEL_IS_OPENCV(d) (LENSMODEL_OPENCV_FIRST <= (d) && (d) <= LENSMODEL_OPENCV_LAST)


// types <0 are invalid. The different invalid types are just for error
// reporting
typedef enum
    { LENSMODEL_INVALID           = -2,
      LENSMODEL_INVALID_BADCONFIG = -1
      // The rest, starting with 0
#define LIST_WITH_COMMA(s,n) ,s
      LENSMODEL_LIST( LIST_WITH_COMMA ) } lensmodel_type_t;


typedef struct
{
    lensmodel_type_t type;
    union
    {
#define CONFIG_STRUCT(s,n) s##__config_t s##__config;
        LENSMODEL_LIST(CONFIG_STRUCT);
#undef CONFIG_STRUCT
    };
} lensmodel_t;

typedef struct
{
    bool ready;
    union
    {
#define PRECOMPUTED_STRUCT(s,n) s##__precomputed_t s##__precomputed;
        LENSMODEL_LIST(PRECOMPUTED_STRUCT);
#undef PRECOMPUTED_STRUCT
    };
} mrcal_projection_precomputed_t;

__attribute__((unused))
static bool mrcal_lensmodel_type_is_valid(lensmodel_type_t t)
{
    return t >= 0;
}

#define MRCAL_LENSMODEL_META_LIST(_, cookie)                    \
    _(has_core,                  bool, "i",,, :1, cookie)       \
    _(can_project_behind_camera, bool, "i",,, :1, cookie)
typedef struct
{
    MRCAL_LENSMODEL_META_LIST(MRCAL_ITEM_DEFINE_ELEMENT, )
} mrcal_lensmodel_meta_t;

typedef struct
{
    // Applies only to those models that HAVE a core (fx,fy,cx,cy)
    bool do_optimize_intrinsic_core        : 1;

    // For models that have a core, these are all the non-core parameters. For
    // models that do not, these are ALL the parameters
    bool do_optimize_intrinsic_distortions : 1;
    bool do_optimize_extrinsics            : 1;
    bool do_optimize_frames                : 1;
    bool do_skip_regularization            : 1;
    bool do_optimize_calobject_warp        : 1;
} mrcal_problem_details_t;

typedef struct
{
    double  point_max_range;
    double  point_min_range;
} mrcal_problem_constants_t;

#define DO_OPTIMIZE_ALL ((mrcal_problem_details_t) { .do_optimize_intrinsic_core        = true, \
                                                     .do_optimize_intrinsic_distortions = true, \
                                                     .do_optimize_extrinsics            = true, \
                                                     .do_optimize_frames                = true, \
                                                     .do_optimize_calobject_warp        = true, \
                                                     .do_skip_regularization            = false})

// These return a string describing the lens model. mrcal_lensmodel_name()
// returns a static string. For models with no configuration, this is the FULL
// string. For models that have a configuration, however, a static string cannot
// contain the configuration values, so mrcal_lensmodel_name() returns
// LENSMODEL_XXX_a=..._b=..._c=... Note the ... that stands in for the
// configuration parameters. So for models with a configuration
// mrcal_lensmodel_from_name( mrcal_lensmodel_name(...) ) would fail
//
// mrcal_lensmodel_name_full() does the same thing, except it writes the string
// into a buffer, and it expands the configuration parameters. The arguments are
// the same as with snprintf(): the output buffer, and the maximum size. We
// return true if we succeeded successfully. So even for models with a
// configuration mrcal_lensmodel_from_name( mrcal_lensmodel_name_full(...) )
// would succeed
const char*        mrcal_lensmodel_name                  ( lensmodel_t model );
bool               mrcal_lensmodel_name_full             ( char* out, int size, lensmodel_t model );

// parses the model name AND the configuration into a lensmodel_t structure.
// Strings with valid model names but missing or unparseable configuration
// return {.type = LENSMODEL_INVALID_BADCONFIG}. Unknown model names return
// invalid lensmodel.type, which can be checked with
// mrcal_lensmodel_type_is_valid(lensmodel->type)
lensmodel_t        mrcal_lensmodel_from_name             ( const char* name );

// parses the model name only. The configuration is ignored. Even if it's
// missing or unparseable. Unknown model names return LENSMODEL_INVALID
lensmodel_type_t   mrcal_lensmodel_type_from_name        ( const char* name );

mrcal_lensmodel_meta_t mrcal_lensmodel_meta              ( const lensmodel_t m );
int                mrcal_getNlensParams                  ( const lensmodel_t m );
int                mrcal_getNintrinsicOptimizationParams ( mrcal_problem_details_t problem_details,
                                                           lensmodel_t m );
const char* const* mrcal_getSupportedLensModels          ( void ); // NULL-terminated array of char* strings

bool mrcal_get_knots_for_splined_models( // buffers must hold at least
                                         // config->Nx and config->Ny values
                                         // respectively
                                         double* ux, double* uy,
                                         lensmodel_t lensmodel);

// Wrapper around the internal project() function: the function used in the
// inner optimization loop. These map world points to their observed pixel
// coordinates, and to optionally provide gradients. dxy_dintrinsics and/or
// dxy_dp are allowed to be NULL if we're not interested those gradients.
//
// This function supports CAHVORE distortions if we don't ask for gradients
bool mrcal_project( // out
                   point2_t* q,

                   // Stored as a row-first array of shape (N,2,3). Each
                   // trailing ,3 dimension element is a point3_t
                   point3_t* dq_dp,
                   // core, distortions concatenated. Stored as a row-first
                   // array of shape (N,2,Nintrinsics). This is a DENSE array.
                   // High-parameter-count lens models have very sparse
                   // gradients here, and the internal project() function
                   // returns those sparsely. For now THIS function densifies
                   // all of these
                   double*   dq_dintrinsics,

                   // in
                   const point3_t* p,
                   int N,
                   lensmodel_t lensmodel,
                   // core, distortions concatenated
                   const double* intrinsics);

// Maps a set of distorted 2D imager points q to a 3d vector in camera
// coordinates that produced these pixel observations. The 3d vector is defined
// up-to-length, so the vectors reported here will all have z = 1.
//
// This is the "reverse" direction, so an iterative nonlinear optimization is
// performed internally to compute this result. This is much slower than
// mrcal_project. For OpenCV models specifically, OpenCV has cvUndistortPoints()
// (and cv2.undistortPoints()), but these are inaccurate:
// https://github.com/opencv/opencv/issues/8811
//
// This function does NOT support CAHVORE
bool mrcal_unproject( // out
                     point3_t* out,

                     // in
                     const point2_t* q,
                     int N,
                     lensmodel_t lensmodel,
                     // core, distortions concatenated
                     const double* intrinsics);

// Compute a stereographic projection/unprojection using a constant fxy, cxy.
// This is the same as the pinhole projection for long lenses, but supports
// views behind the camera. There's only one singularity point: directly behind
// the camera. Thus this is a good basis for optimization over observation
// vectors: it's unconstrained, smoooth and effectively singularity-free
void mrcal_project_stereographic( // output
                                 point2_t* q,
                                 point3_t* dq_dv, // May be NULL. Each point
                                                  // gets a block of 2 point3_t
                                                  // objects

                                  // input
                                 const point3_t* v,
                                 int N,
                                 double fx, double fy,
                                 double cx, double cy);
void mrcal_unproject_stereographic( // output
                                   point3_t* v,
                                   point2_t* dv_dq, // May be NULL. Each point
                                                    // gets a block of 3
                                                    // point2_t objects

                                   // input
                                   const point2_t* q,
                                   int N,
                                   double fx, double fy,
                                   double cx, double cy);


#define MRCAL_STATS_ITEM(_)                                           \
    _(double,         rms_reproj_error__pixels,   PyFloat_FromDouble) \
    _(int,            Noutliers,                  PyInt_FromLong)

#define MRCAL_STATS_ITEM_DEFINE(type, name, pyconverter) type name;

typedef struct
{
    MRCAL_STATS_ITEM(MRCAL_STATS_ITEM_DEFINE)
} mrcal_stats_t;


mrcal_stats_t
mrcal_optimize( // out
                // Each one of these output pointers may be NULL
                // Shape (Nmeasurements,)
                double* x_final,
                // used only to confirm that the user passed-in the buffer they
                // should have passed-in. The size must match exactly
                int buffer_size_x_final,

                // Which camera we're querying for covariance_intrinsics and
                // covariances_ief... If <0 then I return the covariances for
                // ALL the cameras
                int icam_intrinsics_covariances_ief,

                // out, in
                //
                // This is a dogleg_solverContext_t. I don't want to #include
                // <dogleg.h> here, so this is void
                //
                // if(_solver_context != NULL) then this is a persistent solver
                // context. The context is NOT freed on exit.
                // mrcal_free_context() should be called to release it
                //
                // if(*_solver_context != NULL), the given context is reused
                // if(*_solver_context == NULL), a context is created, and
                // returned here on exit
                void** _solver_context,

                // These are a seed on input, solution on output

                // intrinsics is a concatenation of the intrinsics core and the
                // distortion params. The specific distortion parameters may
                // vary, depending on lensmodel, so this is a variable-length
                // structure
                double*       intrinsics,         // Ncameras_intrinsics * NlensParams
                pose_t*       extrinsics_fromref, // Ncameras_extrinsics of these. Transform FROM the reference frame
                pose_t*       frames_toref,       // Nframes of these.    Transform TO the reference frame
                point3_t*     points,             // Npoints of these.    In the reference frame
                point2_t*     calobject_warp,     // 1 of these. May be NULL if !problem_details.do_optimize_calobject_warp

                // All the board pixel observations, in order.
                // .x, .y are the pixel observations
                // .z is the weight of the observation. Most of the weights are
                // expected to be 1.0, which implies that the noise on the
                // observation has standard deviation of
                // observed_pixel_uncertainty. observed_pixel_uncertainty scales
                // inversely with the weight.
                //
                // z<0 indicates that this is an outlier. This is respected on
                // input (even if skip_outlier_rejection). New outliers are
                // marked with z<0 on output, so this isn't const
                point3_t* observations_board_pool,
                int NobservationsBoard,

                // in
                int Ncameras_intrinsics, int Ncameras_extrinsics, int Nframes,
                int Npoints, int Npoints_fixed, // at the end of points[]

                const observation_board_t* observations_board,
                const observation_point_t* observations_point,
                int NobservationsPoint,

                bool check_gradient,
                bool verbose,
                // Whether to try to find NEW outliers. The outliers given on
                // input are respected regardless
                const bool skip_outlier_rejection,

                lensmodel_t lensmodel,
                double observed_pixel_uncertainty,
                const int* imagersizes, // Ncameras_intrinsics*2 of these
                mrcal_problem_details_t          problem_details,
                const mrcal_problem_constants_t* problem_constants,

                double calibration_object_spacing,
                int calibration_object_width_n,
                int calibration_object_height_n);

struct cholmod_sparse_struct;

bool mrcal_optimizerCallback(// output measurements
                             double*         x,

                             // output Jacobian. May be NULL if we don't need
                             // it. This is the unitless Jacobian, used by the
                             // internal optimization routines
                             struct cholmod_sparse_struct* Jt,

                             // May be NULL
                             // I return this ONLY if
                             // icam_intrinsics_covariances_ief>=0 and at least
                             // one of covariances_ief... is requested. The
                             // icam_extrinsics corresponding to
                             // icam_intrinsics_covariances_ief is reported
                             // here. If the given camera is at the reference,
                             // return <0. If the cameras are moving (mapping
                             // between icam_intrinsics and icam_extrinsics not
                             // bijective) and
                             // icam_extrinsics_covariances_ief!=NULL, this
                             // function call will return false
                             int* icam_extrinsics_covariances_ief,


                             // in

                             // Which camera we're querying for
                             // covariances_ief... If <0 then I return the
                             // covariances for ALL the cameras
                             int icam_intrinsics_covariances_ief,

                             // intrinsics is a concatenation of the intrinsics core
                             // and the distortion params. The specific distortion
                             // parameters may vary, depending on lensmodel, so
                             // this is a variable-length structure
                             const double*       intrinsics,         // Ncameras_intrinsics * NlensParams
                             const pose_t*       extrinsics_fromref, // Ncameras_extrinsics of these. Transform FROM reference frame
                             const pose_t*       frames_toref,       // Nframes of these.    Transform TO reference frame
                             const point3_t*     points,     // Npoints of these.    In the reference frame
                             const point2_t*     calobject_warp, // 1 of these. May be NULL if !problem_details.do_optimize_calobject_warp

                             int Ncameras_intrinsics, int Ncameras_extrinsics, int Nframes,
                             int Npoints, int Npoints_fixed, // at the end of points[]

                             const observation_board_t* observations_board,

                             // All the board pixel observations, in order.
                             // .x, .y are the pixel observations
                             // .z is the weight of the observation. Most of the
                             // weights are expected to be 1.0, which implies
                             // that the noise on the observation has standard
                             // deviation of observed_pixel_uncertainty.
                             // observed_pixel_uncertainty scales inversely with
                             // the weight.
                             //
                             // z<0 indicates that this is an outlier
                             const point3_t* observations_board_pool,
                             int NobservationsBoard,

                             const observation_point_t* observations_point,
                             int NobservationsPoint,
                             bool verbose,

                             lensmodel_t lensmodel,
                             double observed_pixel_uncertainty,
                             const int* imagersizes, // Ncameras_intrinsics*2 of these

                             mrcal_problem_details_t          problem_details,
                             const mrcal_problem_constants_t* problem_constants,

                             double calibration_object_spacing,
                             int calibration_object_width_n,
                             int calibration_object_height_n,
                             int Nintrinsics, int Nmeasurements, int N_j_nonzero);


int mrcal_getNmeasurements_all(int Ncameras_intrinsics,
                               int NobservationsBoard,
                               int NobservationsPoint,
                               int calibration_object_width_n,
                               int calibration_object_height_n,
                               mrcal_problem_details_t problem_details,
                               lensmodel_t lensmodel);
int mrcal_getNmeasurements_boards(int NobservationsBoard,
                                  int calibration_object_width_n,
                                  int calibration_object_height_n);
int mrcal_getNmeasurements_points(int NobservationsPoint);
int mrcal_getNmeasurements_regularization(int Ncameras_intrinsics,
                                          mrcal_problem_details_t problem_details,
                                          lensmodel_t lensmodel);
int mrcal_getNstate(int Ncameras_intrinsics, int Ncameras_extrinsics,
                    int Nframes, int NpointsVariable,
                    mrcal_problem_details_t problem_details,
                    lensmodel_t lensmodel);
int mrcal_getN_j_nonzero( int Ncameras_intrinsics, int Ncameras_extrinsics,
                          const observation_board_t* observations_board,
                          int NobservationsBoard,
                          const observation_point_t* observations_point,
                          int NobservationsPoint,
                          int Npoints, int Npoints_fixed,
                          mrcal_problem_details_t problem_details,
                          lensmodel_t lensmodel,
                          int calibration_object_width_n,
                          int calibration_object_height_n);

// frees a dogleg_solverContext_t. I don't want to #include <dogleg.h> here, so
// this is void
void mrcal_free_context(void** ctx);


int mrcal_state_index_intrinsics(int i_cam_intrinsics,
                                 mrcal_problem_details_t problem_details,
                                 lensmodel_t lensmodel);
int mrcal_state_index_camera_rt(int i_cam_extrinsics,
                                int Ncameras_intrinsics,
                                mrcal_problem_details_t problem_details,
                                lensmodel_t lensmodel);
int mrcal_state_index_frame_rt(int i_frame,
                               int Ncameras_intrinsics, int Ncameras_extrinsics,
                               mrcal_problem_details_t problem_details,
                               lensmodel_t lensmodel);
int mrcal_state_index_point(int i_point, int Nframes,
                            int Ncameras_intrinsics, int Ncameras_extrinsics,
                            mrcal_problem_details_t problem_details,
                            lensmodel_t lensmodel);
int mrcal_state_index_calobject_warp(int NpointsVariable,
                                     int Nframes,
                                     int Ncameras_intrinsics, int Ncameras_extrinsics,
                                     mrcal_problem_details_t problem_details,
                                     lensmodel_t lensmodel);

// packs/unpacks a vector
void mrcal_pack_solver_state_vector( // out, in
                                     double* p, // unitless, FULL state on
                                                // input, scaled, decimated
                                                // (subject to problem_details),
                                                // meaningful state on output

                                     // in
                                     const lensmodel_t lensmodel,
                                     mrcal_problem_details_t problem_details,
                                     int Ncameras_intrinsics, int Ncameras_extrinsics,
                                     int Nframes, int NpointsVariable);

void mrcal_unpack_solver_state_vector( // out, in
                                       double* p, // unitless state on input,
                                                  // scaled, meaningful state on
                                                  // output

                                       // in
                                       const lensmodel_t lensmodel,
                                       mrcal_problem_details_t problem_details,
                                       int Ncameras_intrinsics, int Ncameras_extrinsics,
                                       int Nframes, int NpointsVariable);



// THESE ARE NOT A PART OF THE EXTERNAL API. Exported for the mrcal python
// wrapper only
void _mrcal_project_internal_opencv( // outputs
                                    point2_t* q,
                                    point3_t* dq_dp,               // may be NULL
                                    double* dq_dintrinsics_nocore, // may be NULL

                                    // inputs
                                    const point3_t* p,
                                    int N,
                                    const double* intrinsics,
                                    int Nintrinsics);
bool _mrcal_project_internal_cahvore( // out
                                     point2_t* out,

                                     // in
                                     const point3_t* v,
                                     int N,

                                     // core, distortions concatenated
                                     const double* intrinsics);
bool _mrcal_project_internal( // out
                             point2_t* q,

                             // Stored as a row-first array of shape (N,2,3). Each
                             // trailing ,3 dimension element is a point3_t
                             point3_t* dq_dp,
                             // core, distortions concatenated. Stored as a row-first
                             // array of shape (N,2,Nintrinsics). This is a DENSE array.
                             // High-parameter-count lens models have very sparse
                             // gradients here, and the internal project() function
                             // returns those sparsely. For now THIS function densifies
                             // all of these
                             double*   dq_dintrinsics,

                             // in
                             const point3_t* p,
                             int N,
                             lensmodel_t lensmodel,
                             // core, distortions concatenated
                             const double* intrinsics,

                             int Nintrinsics,
                             const mrcal_projection_precomputed_t* precomputed);
void _mrcal_precompute_lensmodel_data(mrcal_projection_precomputed_t* precomputed,
                                      lensmodel_t lensmodel);
bool _mrcal_unproject_internal( // out
                               point3_t* out,

                               // in
                               const point2_t* q,
                               int N,
                               lensmodel_t lensmodel,
                               // core, distortions concatenated
                               const double* intrinsics,
                               const mrcal_projection_precomputed_t* precomputed);
