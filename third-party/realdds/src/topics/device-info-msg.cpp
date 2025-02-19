// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2022 Intel Corporation. All Rights Reserved.

#include <realdds/topics/device-info-msg.h>
#include <realdds/topics/dds-topic-names.h>
#include <realdds/dds-exceptions.h>
#include <realdds/dds-utilities.h>

#include <rsutils/easylogging/easyloggingpp.h>

namespace realdds {
namespace topics {


static std::string name_key( "name", 4 );
static std::string topic_root_key( "topic-root", 10 );
static std::string serial_number_key( "serial", 6 );


/* static  */ device_info device_info::from_json( rsutils::json const & j )
{
    device_info ret;
    ret._json = j;

    // Check the two mandatory fields are there
    if( ret.name().empty() )
        DDS_THROW( runtime_error, "empty device-info name" );
    if( ret.topic_root().empty() )
        DDS_THROW( runtime_error, "empty device-info topic-root" );

    return ret;
}


rsutils::json const & device_info::to_json() const
{
    return _json;
}


std::string const & device_info::name() const
{
    return _json.nested( name_key ).string_ref_or_empty();
}

void device_info::set_name( std::string const & v )
{
    _json[name_key] = v;
}


std::string const & device_info::topic_root() const
{
    return _json.nested( topic_root_key ).string_ref_or_empty();
}

void device_info::set_topic_root( std::string const & v )
{
    _json[topic_root_key] = v;
}


std::string const & device_info::serial_number() const
{
    return _json.nested( serial_number_key ).string_ref_or_empty();
}

void device_info::set_serial_number( std::string const & v )
{
    _json[serial_number_key] = v;
}


rsutils::string::slice device_info::debug_name() const
{
    return device_name_from_root( topic_root() );
}


}  // namespace topics
}  // namespace realdds
