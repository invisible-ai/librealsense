// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2015 Intel Corporation. All Rights Reserved.

#include <librealsense2/rs.hpp>

#include <vector>
#include <map>
#include <string>
#include <cstring>
#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>

#include "tclap/CmdLine.h"
#include "tclap/ValueArg.h"

using namespace TCLAP;

#define WAIT_FOR_DEVICE_TIMEOUT 15

#if _WIN32
#include <io.h>
#define ISATTY _isatty
#define FILENO _fileno
#else
#include <unistd.h>
#define ISATTY isatty
#define FILENO fileno
#endif

std::vector<uint8_t> read_fw_file(std::string file_path)
{
    std::vector<uint8_t> rv;

    std::ifstream file(file_path, std::ios::in | std::ios::binary | std::ios::ate);
    auto file_deleter = std::unique_ptr< std::ifstream, void ( * )( std::ifstream * ) >( &file,
                                                                                         []( std::ifstream * file )
                                                                                         {
                                                                                             if( file )
                                                                                                 file->close();
                                                                                         } );
    if (file.is_open())
    {
        rv.resize(file.tellg());

        try
        {
            file.seekg( 0, std::ios::beg );
            file.read( (char *)rv.data(), rv.size() );
        }
        catch( ... )
        {
            // Nothing to do, file goodbit is false
        }
        if( ! file.good() )
        {
            std::cout << std::endl << "Error reading firmware file";
            rv.resize( 0 ); // Signal error, don't use partial read data
        }
        
    }

    return rv;
}

void print_device_info(rs2::device d)
{
    std::map<rs2_camera_info, std::string> camera_info;

    for (int i = 0; i < RS2_CAMERA_INFO_COUNT; i++)
    {
        auto info = (rs2_camera_info)i;
        camera_info[info] = d.supports(info) ? d.get_info(info) : "unknown";
    }

    std::cout << "Name: " << camera_info[RS2_CAMERA_INFO_NAME] <<
        ", serial number: " << camera_info[RS2_CAMERA_INFO_SERIAL_NUMBER] <<
        ", update serial number: " << camera_info[RS2_CAMERA_INFO_FIRMWARE_UPDATE_ID] <<
        ", firmware version: " << camera_info[RS2_CAMERA_INFO_FIRMWARE_VERSION] <<
        ", USB type: " << camera_info[RS2_CAMERA_INFO_USB_TYPE_DESCRIPTOR] << std::endl;
}

std::vector<uint8_t> read_firmware_data(bool is_set, const std::string& file_path)
{
    if (!is_set)
    {
        throw rs2::error("Firmware file must be selected");
    }

    std::vector<uint8_t> fw_image = read_fw_file(file_path);

    if (fw_image.size() == 0)
    {
        throw rs2::error("Failed to read firmware file");
    }

    return fw_image;
}


void update(rs2::update_device fwu_dev, std::vector<uint8_t> fw_image)
{  
    std::cout << std::endl << "Firmware update started. Please don't disconnect device!"<< std::endl << std::endl;
    
    if (ISATTY(FILENO(stdout)))
    {
        fwu_dev.update(fw_image, [&](const float progress)
            {
                printf("\rFirmware update progress: %d[%%]", (int)(progress * 100));
            });
    }
    else
    {
      fwu_dev.update(fw_image, [&](const float progress) {} ); 
    }
    std::cout << std::endl << std::endl << "Firmware update done" << std::endl;
}

void list_devices(rs2::context ctx)
{
    auto devs = ctx.query_devices();
    if (devs.size() == 0)
    {
        std::cout << std::endl << "There are no connected devices" << std::endl;
        return;
    }

    std::cout << std::endl << "Connected devices:" << std::endl;


    int counter = 0;
    for (auto&& d : devs)
    {
        std::cout << ++counter << ") ";
        print_device_info(d);
    }
}

int write_fw_to_mipi_device( const rs2::device & dev, const std::vector< uint8_t > & fw_image )
{
    // Write firmware to appropriate file descriptor
    std::cout << std::endl << "Update can take up to 2 minutes" << std::endl;
    std::ofstream fw_path_in_device( dev.get_info( RS2_CAMERA_INFO_DFU_DEVICE_PATH ), std::ios::binary );
    auto file_deleter = std::unique_ptr< std::ofstream, void ( * )( std::ofstream * ) >( &fw_path_in_device,
                                                                                         []( std::ofstream * file )
                                                                                         {
                                                                                             if( file )
                                                                                                 file->close();
                                                                                         } );
    if( fw_path_in_device )
    {
        bool flush_done = false;
        std::thread show_progress_thread(
            [&flush_done]()
            {
                for( int i = 0; i < 101 && ! flush_done; ++i ) // Show percentage [0-100]
                {
                    printf( "%d%%\r", i );
                    std::cout.flush();
                    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
                }
            } );
        try
        {
            fw_path_in_device.write( reinterpret_cast< const char * >( fw_image.data() ), fw_image.size() );
        }
        catch( ... )
        {
            // Nothing to do, file goodbit is false
        }
        flush_done = true;
        show_progress_thread.join();
        printf( "    \r" ); // Delete progress, as it is not accurate, don't leave 85% when writing done
        if( ! fw_path_in_device.good() )
        {
            std::cout << std::endl << "Firmware Update failed - write to device error";
            return EXIT_FAILURE;
        }
    }
    else 
    {
        std::cout << std::endl << "Firmware Update failed - wrong path or permissions missing";
        return EXIT_FAILURE;
    }
    std::cout << std::endl << "Firmware update done" << std::endl;

    return EXIT_SUCCESS;
}

bool is_mipi_device( const rs2::device & dev )
{
    std::string usb_type = "unknown";

    if( dev.supports( RS2_CAMERA_INFO_USB_TYPE_DESCRIPTOR ) )
        usb_type = dev.get_info( RS2_CAMERA_INFO_USB_TYPE_DESCRIPTOR );

    bool d457_device = strcmp( dev.get_info( RS2_CAMERA_INFO_PRODUCT_ID ), "ABCD" ) == 0;

    // Currently only D457 model has MIPI connection
    return d457_device && usb_type.compare( "unknown" ) == 0;
}

int main( int argc, char ** argv )
try
{
#ifdef BUILD_EASYLOGGINGPP
    rs2::log_to_console(RS2_LOG_SEVERITY_WARN);
#endif

    rs2::context ctx;

    std::condition_variable cv;
    std::mutex mutex;
    std::string selected_serial_number;

    rs2::device new_device;
    rs2::update_device new_fw_update_device;

    bool done = false;

    CmdLine cmd("librealsense rs-fw-update tool", ' ', RS2_API_FULL_VERSION_STR);

    SwitchArg list_devices_arg("l", "list_devices", "List all available devices");
    SwitchArg recover_arg("r", "recover", "Recover all connected devices which are in recovery mode");
    SwitchArg unsigned_arg("u", "unsigned", "Update unsigned firmware, available only for unlocked cameras");
    ValueArg<std::string> backup_arg("b", "backup", "Create a backup to the camera flash and saved it to the given path", false, "", "string");
    ValueArg<std::string> file_arg("f", "file", "Path of the firmware image file", false, "", "string");
    ValueArg<std::string> serial_number_arg("s", "serial_number", "The serial number of the device to be update, this is mandetory if more than one device is connected", false, "", "string");

    cmd.add(list_devices_arg);
    cmd.add(recover_arg);
    cmd.add(unsigned_arg);
    cmd.add(file_arg);
    cmd.add(serial_number_arg);
    cmd.add(backup_arg);

    cmd.parse(argc, argv);

    if (!list_devices_arg.isSet() && !recover_arg.isSet() && !unsigned_arg.isSet() &&
        !backup_arg.isSet() && !file_arg.isSet() && !serial_number_arg.isSet())
    {
        std::cout << std::endl << "Nothing to do, run again with -h for help" << std::endl;
        list_devices(ctx);
        return EXIT_SUCCESS;
    }

    bool recovery_request = recover_arg.getValue();

    if (list_devices_arg.isSet())
    {
        list_devices(ctx);
        return EXIT_SUCCESS;
    }

    if (!file_arg.isSet() && !backup_arg.isSet())
    {
        std::cout << std::endl << "Nothing to do, run again with -h for help" << std::endl;
        return EXIT_FAILURE;
    }

    if (serial_number_arg.isSet())
    {
        selected_serial_number = serial_number_arg.getValue();
        std::cout << std::endl << "Search for device with serial number: " << selected_serial_number << std::endl;
    }

    std::string update_serial_number;

    // Recovery
    if (recover_arg.isSet() )
    {
        std::vector<uint8_t> fw_image = read_firmware_data(file_arg.isSet(), file_arg.getValue());

        std::cout << std::endl << "Update to FW: " << file_arg.getValue() << std::endl;
        auto devs = ctx.query_devices(RS2_PRODUCT_LINE_DEPTH);
        rs2::device recovery_device;

        for (auto&& d : devs)
        {
            if( ! d.is< rs2::update_device >() )
                continue;
            auto sn = d.get_info( RS2_CAMERA_INFO_FIRMWARE_UPDATE_ID );
            if( ! selected_serial_number.empty() && sn != selected_serial_number )
                continue;
            if( recovery_device )
            {
                std::cout << std::endl << "More than one recovery device is connected; serial number must be specified" << std::endl << std::endl;
                return EXIT_FAILURE;
            }
            recovery_device = d;
        }
        if( ! recovery_device )
        {
            std::cout << std::endl << "No recovery devices were found!" << std::endl << std::endl;
            return EXIT_FAILURE;
        }
        try
        {
            update_serial_number = recovery_device.get_info( RS2_CAMERA_INFO_FIRMWARE_UPDATE_ID );
            volatile bool recovery_device_found = false;
            ctx.set_devices_changed_callback( [&]( rs2::event_information & info ) {
                for( auto && d : info.get_new_devices() )
                {
                    if( d.is< rs2::update_device >() )
                        continue;
                    auto recovery_sn = d.get_info( RS2_CAMERA_INFO_FIRMWARE_UPDATE_ID );
                    if( recovery_sn == update_serial_number )
                    {
                        {
                            std::lock_guard< std::mutex > lk( mutex );
                            recovery_device_found = true;
                        }
                        cv.notify_one();
                        break;
                    }
                }
            } );
            std::cout << std::endl << "Recovering device: " << std::endl;
            print_device_info( recovery_device );
            update( recovery_device, fw_image );
            std::cout << "Waiting for new device..." << std::endl;
            {
                std::unique_lock< std::mutex > lk( mutex );
                if( ! recovery_device_found
                    && ! cv.wait_for( lk, std::chrono::seconds( WAIT_FOR_DEVICE_TIMEOUT ), [&]() {
                           return recovery_device_found;
                       } ) )
                {
                    std::cout << "... timed out!" << std::endl;
                    return EXIT_FAILURE;
                }
            }
            std::cout << std::endl << "Recovery done" << std::endl;
            return EXIT_SUCCESS;
        }
        catch (...)
        {
            std::cout << std::endl << "Failed to recover device" << std::endl;
            return EXIT_FAILURE;
        }
    }

    // Update device
    ctx.set_devices_changed_callback([&](rs2::event_information& info)
    {
        if (info.get_new_devices().size() == 0)
        {
            return;
        }

        for (auto&& d : info.get_new_devices())
        {
            std::lock_guard<std::mutex> lk(mutex);
            if (d.is<rs2::update_device>() && (d.get_info(RS2_CAMERA_INFO_FIRMWARE_UPDATE_ID) == update_serial_number))
                new_fw_update_device = d;
            else
                new_device = d;
        }
        if(new_fw_update_device || new_device)
            cv.notify_one();
    });

    auto devs = ctx.query_devices(RS2_PRODUCT_LINE_DEPTH);

    if (!serial_number_arg.isSet() && devs.size() > 1)
    {
        std::cout << std::endl << "More than one device is connected, serial number must be selected" << std::endl << std::endl;
        return EXIT_FAILURE;
    }

    if ( devs.size() == 1 && devs[0].is<rs2::update_device>() )
    {
        std::cout << std::endl << "Device is in recovery mode, use -r to recover" << std::endl << std::endl;
        return EXIT_FAILURE;
    }

    if (devs.size() == 0)
    {
        std::cout << std::endl << "No devices were found" << std::endl << std::endl;
        return EXIT_FAILURE;
    }

    bool device_found = false;

    for( auto&& d : devs )
    {
        if( !d.is<rs2::updatable>() || !( d.supports( RS2_CAMERA_INFO_SERIAL_NUMBER ) && d.supports( RS2_CAMERA_INFO_FIRMWARE_UPDATE_ID ) ) )
            continue;

        if( d.supports( RS2_CAMERA_INFO_USB_TYPE_DESCRIPTOR ) )
        {
            std::string usb_type = d.get_info( RS2_CAMERA_INFO_USB_TYPE_DESCRIPTOR );
            if( usb_type.find( "2." ) != std::string::npos ) {
                std::cout << std::endl << "Warning! the camera is connected via USB 2 port, in case the process fails, connect the camera to a USB 3 port and try again" << std::endl;
            }
        }

        update_serial_number = d.get_info( RS2_CAMERA_INFO_FIRMWARE_UPDATE_ID );

        auto sn = d.get_info( RS2_CAMERA_INFO_SERIAL_NUMBER );
        if( sn != selected_serial_number && devs.size() != 1 )
            continue;
        device_found = true;

        if( backup_arg.isSet() )
        {
            std::cout << std::endl << "Trying to back-up device flash" << std::endl;

            std::vector< uint8_t > flash;
            if( ISATTY( FILENO( stdout ) ) )
            {
                flash = d.as< rs2::updatable >().create_flash_backup( [&]( const float progress ) {
                    printf( "\rFlash backup progress: %d[%%]", (int)( progress * 100 ) );
                    } );
            }
            else
                flash = d.as<rs2::updatable>().create_flash_backup( [&]( const float progress ) {} );

            if( flash.empty() )
            {
                std::cout << std::endl << "Backup flash is not supported";
            }
            else
            {
                auto temp = backup_arg.getValue();
                std::ofstream file( temp.c_str(), std::ios::binary );
                auto file_deleter = std::unique_ptr< std::ofstream, void ( * )( std::ofstream* ) >( &file,
                    []( std::ofstream* file )
                    {
                        if( file )
                            file->close();
                    } );
                try
                {
                    file.write( (const char*)flash.data(), flash.size() );
                }
                catch( ... )
                {
                    // Nothing to do, file goodbit is false
                }
                if( !file.good() )
                {
                    std::cout << std::endl << "Creating backup file failed";
                }
            }
        }

        // FW DFU
        if( file_arg.isSet() )
        {
            std::vector<uint8_t> fw_image = read_firmware_data( file_arg.isSet(), file_arg.getValue() );

            std::cout << std::endl << "Updating device FW: " << std::endl;
            print_device_info( d );

            // If device is D457 connected by MIPI connector
            if( is_mipi_device( d ) )
            {
                if( unsigned_arg.isSet() )
                {
                    std::cout << std::endl << "Only signed FW is currently supported for MIPI devices" << std::endl;
                    return EXIT_FAILURE;
                }

                return write_fw_to_mipi_device( d, fw_image );
            }

            if( unsigned_arg.isSet() )
            {
                std::cout << std::endl << "Firmware update started. Please don't disconnect device!" << std::endl << std::endl;

                if( ISATTY( FILENO( stdout ) ) )
                {
                    d.as<rs2::updatable>().update_unsigned( fw_image, [&]( const float progress )
                        {
                            printf( "\rFirmware update progress: %d[%%]", (int)( progress * 100 ) );
                        } );
                }
                else
                    d.as<rs2::updatable>().update_unsigned( fw_image, [&]( const float progress ) {} );

                std::cout << std::endl << std::endl << "Firmware update done" << std::endl;
            }
            else
            {
                auto upd = d.as<rs2::updatable>();
                // checking compatibility bewtween firmware and device
                if( !upd.check_firmware_compatibility( fw_image ) )
                {
                    std::stringstream ss;
                    ss << "This firmware version is not compatible with ";
                    ss << d.get_info( RS2_CAMERA_INFO_NAME ) << std::endl;
                    std::cout << std::endl << ss.str() << std::endl;
                    return EXIT_FAILURE;
                }

                upd.enter_update_state();

                std::unique_lock<std::mutex> lk( mutex );
                if( !cv.wait_for( lk, std::chrono::seconds( WAIT_FOR_DEVICE_TIMEOUT ), [&] { return new_fw_update_device; } ) )
                {
                    std::cout << std::endl << "Failed to locate a device in FW update mode" << std::endl;
                    return EXIT_FAILURE;
                }

                 update( new_fw_update_device, fw_image );

                done = true;
                break;
            }
        }
    }

    if (!device_found)
    {
        if(serial_number_arg.isSet())
            std::cout << std::endl << "Couldn't find the requested serial number" << std::endl;
        else if (devs.size() == 1)
        {
            std::cout << std::endl << "Nothing to do, run again with -h for help" << std::endl;
        }
        return EXIT_FAILURE;
    }

    std::cout << std::endl << "Waiting for device to reconnect..." << std::endl;
    std::unique_lock<std::mutex> lk(mutex);
    cv.wait_for(lk, std::chrono::seconds(WAIT_FOR_DEVICE_TIMEOUT), [&] { return !done || new_device; });

    if (done)
    {
        auto devs = ctx.query_devices();
        for (auto&& d : devs)
        {
            auto sn = d.supports(RS2_CAMERA_INFO_SERIAL_NUMBER) ? d.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER) : "unknown";
            if (serial_number_arg.isSet() && sn != selected_serial_number)
                continue;

            auto fw = d.supports(RS2_CAMERA_INFO_FIRMWARE_VERSION) ? d.get_info(RS2_CAMERA_INFO_FIRMWARE_VERSION) : "unknown";
            std::cout << std::endl << "Device " << sn << " successfully updated to FW: " << fw << std::endl;
        }
    }

    return EXIT_SUCCESS;
}
catch (const rs2::error & e)
{
    std::cerr << "RealSense error calling " << e.get_failed_function() << "(" << e.get_failed_args() << "):\n    " << e.what() << std::endl;
    return EXIT_FAILURE;
}
