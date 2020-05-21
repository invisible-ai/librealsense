// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020 Intel Corporation. All Rights Reserved.

#pragma once

// Utilities to read from/write to algo scene directories

#include <fstream>
#include <string>
#include "../../../src/algo/depth-to-rgb-calibration/k-to-dsm.h"

inline std::string bin_dir( std::string const & scene_dir )
{
    return scene_dir + "binFiles\\";
}


std::string bin_file( std::string const &prefix, size_t iteration, size_t w, size_t h, std::string const &suffix )
{
    return prefix + "_" + std::to_string( iteration ) + "_" + std::to_string( h ) + "x" +
           std::to_string( w ) + "_" + suffix;
}


std::string bin_file( std::string const &prefix, size_t w, size_t h, std::string const &suffix )
{
    return prefix + "_" + std::to_string( h ) + "x" + std::to_string( w ) + "_" + suffix;
}


template< typename T >
void read_data_from( std::string const & filename, T * data )
{
    std::fstream f = std::fstream( filename, std::ios::in | std::ios::binary );
    if( !f )
        throw std::runtime_error( "failed to read file:\n" + filename );
    f.seekg( 0, f.end );
    size_t cb = f.tellg();
    f.seekg( 0, f.beg );
    if( cb != sizeof( T ) )
        throw std::runtime_error( to_string()
            << "file size (" << cb << ") does not match data size (" << sizeof( T ) << "): " << filename );
    std::vector< T > vec( cb / sizeof( T ) );
    f.read( (char *)data, cb );
    f.close();
}


template< typename T >
std::vector< T > read_vector_from( std::string const & filename )
{
    std::fstream f = std::fstream( filename, std::ios::in | std::ios::binary );
    if( !f )
        throw std::runtime_error( "failed to read file:\n" + filename );
    f.seekg( 0, f.end );
    size_t cb = f.tellg();
    f.seekg( 0, f.beg );
    if( cb % sizeof( T ) )
        throw std::runtime_error( "file size is not a multiple of data size" );
    std::vector< T > vec( cb / sizeof( T ) );
    f.read( (char *)vec.data(), cb );
    f.close();
    return vec;
}


template < class T >
std::vector< T > read_image_file( std::string const &file, size_t width, size_t height )
{
    std::ifstream f;
    f.open( file, std::ios::in | std::ios::binary );
    if( !f.good() )
        throw std::runtime_error( "invalid file: " + file );
    f.seekg( 0, f.end );
    size_t cb = f.tellg();
    f.seekg( 0, f.beg );
    if( cb != sizeof( T ) * width * height )
        throw std::runtime_error( to_string()
            << "file size (" << cb << ") does not match expected size (" << sizeof( T ) * width * height << "): " << file );
    std::vector< T > data( width * height );
    f.read( (char *)data.data(), width * height * sizeof( T ) );
    return data;
}

template < typename T >
void dump_vec( std::vector< double > const & cpp, std::vector< T > const & matlab,
    char const * basename,
    size_t width, size_t height
)
{
    std::string filename = basename;
    filename += ".dump";
#if 0
    std::fstream f = std::fstream( filename, std::ios::out );
    if( !f )
        throw std::runtime_error( "failed to write file:\n" + filename );
    for( size_t x = )
        std::vector< T > vec( cb / sizeof( T ) );
    f.read( (char*)vec.data(), cb );
    f.close();
    return vec;
#endif
}

// This metadata describes what Matlab did: the files it used (sometimes there are more) and the
// iteration data/results that we need for comparison
struct scene_metadata
{
    uint64_t n_iterations;  // how many steps through optimization, and how many iteration file sets
    double correction_in_pixels;  // XY movement
    uint64_t n_edges;             // strong edges, i.e. after suppression
    uint64_t n_valid_pixels;
    uint64_t n_valid_ir_edges;
    bool is_scene_valid;
    bool is_output_valid;
    std::string rgb_file;
    std::string rgb_prev_file;  // TODO: looks like these need to be turned around!!!
    std::string ir_file;
    std::string z_file;

    scene_metadata( std::string const &scene_dir )
    {
        std::ifstream( bin_dir( scene_dir ) + "ac1x\\yuy_prev_z_i.files" ) >> rgb_file >>
        rgb_prev_file >> z_file >> ir_file;
        if( rgb_file.empty() )
            throw std::runtime_error( "failed to read file:\n" + bin_dir( scene_dir ) + "ac1x\\yuy_prev_z_i.files" );

        std::string metadata = bin_dir( scene_dir ) + "ac1x\\metadata";
        std::fstream f = std::fstream( metadata, std::ios::in | std::ios::binary );
        if( !f )
            throw std::runtime_error( "failed to read file:\n" + metadata );
        f.read( (char *)&correction_in_pixels, sizeof( correction_in_pixels ) );
        f.read( (char *)&n_edges, sizeof( n_edges ) );
        f.read( (char *)&n_valid_ir_edges, sizeof( n_valid_ir_edges ) );
        f.read( (char *)&n_valid_pixels, sizeof( n_valid_pixels ) );
        f.read( (char *)&n_iterations, sizeof( n_iterations ) );
        byte b;
        f.read( (char *)&b, 1 );
        is_scene_valid = b;
        f.read( (char *)&b, 1 );
        is_output_valid = b;
        f.close();
    }
};


// Encapsulate the calibration information for a specific camera
// All the sample images we use are usually from the same camera. I.e., their
// intrinsics & extrinsics are the same and can be reused via this structure
struct camera_params
{
    librealsense::algo::depth_to_rgb_calibration::rs2_intrinsics_double rgb;
    librealsense::algo::depth_to_rgb_calibration::rs2_intrinsics_double z;
    librealsense::algo::depth_to_rgb_calibration::rs2_extrinsics_double extrinsics;
    double z_units = 0.25f;
};

camera_params read_camera_params( std::string const &scene_dir, std::string const &filename )
{
    struct params_bin
    {
        // Some units are supposed to be int but we made matlab write out doubles....
        double depth_width;
        double depth_height;
        double depth_units;
        double k_depth[9];
        double rgb_width;
        double rgb_height;
        double k_rgb[9];
        double coeffs[5];
        double rotation[9];
        double translation[3];
        double p_mat[12];
    };

    params_bin param;
    read_data_from( bin_dir( scene_dir ) + filename, &param );

    double coeffs[5] = { 0 };
    const camera_params ci =
    {
        // RGB
        {
            int( param.rgb_width ), int( param.rgb_height ),
            librealsense::algo::depth_to_rgb_calibration::k_matrix{param.k_rgb[0], param.k_rgb[4]
            ,param.k_rgb[2], param.k_rgb[5]},
            RS2_DISTORTION_BROWN_CONRADY,
            param.coeffs
        },
        // Z
        {
            int( param.depth_width ), int( param.depth_height ),
            librealsense::algo::depth_to_rgb_calibration::k_matrix{param.k_depth[0], param.k_depth[4]
            ,param.k_depth[2], param.k_depth[5]},
            RS2_DISTORTION_NONE, coeffs
        },
        // EXTRINSICS
        {
            { param.rotation[0], param.rotation[1], param.rotation[2],
              param.rotation[3], param.rotation[4], param.rotation[5],
              param.rotation[6], param.rotation[7], param.rotation[8] },
            { param.translation[0], param.translation[1], param.translation[2] }
        }
    };
    return ci;
}

struct dsm_params
{
    rs2_dsm_params dsm_params;
    librealsense::algo::depth_to_rgb_calibration::DSM_regs dsm_regs;
};

dsm_params read_dsm_params(std::string const &scene_dir, std::string const &filename)
{
    dsm_params params;
    read_data_from(bin_dir(scene_dir) + filename, &params);

    return params;
}
