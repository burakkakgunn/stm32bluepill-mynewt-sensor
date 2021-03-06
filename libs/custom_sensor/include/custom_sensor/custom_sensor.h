/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */


#ifndef __CUSTOM_SENSOR_H__
#define __CUSTOM_SENSOR_H__

#include "os/mynewt.h"
#include "sensor/sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////
//  Custom Sensor Data Definitions

//  Raw Temperature Sensor: Instead of floating-point computed temperature, we transmit the
//  raw temperature value as integer to the Collector Node and CoAP Server to reduce message
//  size and ROM size.  The raw temperature is converted to computed temperature at the
//  CoAP Server (e.g. thethings.io)

//  This sensor data definition is derived from the sensor_temp_data definition at 
//  repos/apache-mynewt-core/hw/sensor/include/sensor/sensor.h

//  Allocate the next unused Sensor Type ID.
#define SENSOR_TYPE_AMBIENT_TEMPERATURE_RAW SENSOR_TYPE_USER_DEFINED_1

struct sensor_temp_raw_data {   //  Represents a single temperature sensor raw value
    uint32_t strd_temp_raw;     //  Raw temp from STM32 Internal Temp Sensor is 0 to 4095.
    uint8_t  strd_temp_raw_is_valid:1;  //  1 if data is valid
} __attribute__((packed));

#ifdef __cplusplus
}
#endif

#endif /* __CUSTOM_SENSOR_H__ */
