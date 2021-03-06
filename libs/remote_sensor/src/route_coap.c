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

//  Route Sensor Data Messages from Sensor Nodes to the Remote Sensor Drivers and trigger their Listener Functions

#define CBOR_IMPLEMENTATION  //  Define the TinyCBOR functions here.
#include <tinycbor/cbor.h>
#include <assert.h>
#include <os/os.h>
#include <sensor/sensor.h>
#include <console/console.h>
#include <os/os_mbuf.h>
#include <oic/oc_rep.h>
#include <sensor_network/sensor_network.h>
#include <nrf24l01/nrf24l01.h>
#include "remote_sensor/remote_sensor.h"

static void receive_callback(struct os_event *ev);
static int process_coap_message(const char *name, uint8_t *data, uint8_t size0);
static int decode_coap_payload(uint8_t *data, uint8_t size, oc_rep_t **out_rep);

static uint8_t rxData[MYNEWT_VAL(NRF24L01_TX_SIZE)];  //  Buffer for received data
static const char *_nrf = "NRF ";                     //  Prefix for log messages

int remote_sensor_start(void) {
    //  Start the router that receives CBOR messages from Sensor Nodes
    //  and triggers the Remote Sensor for the field names in the CBOR message. 
    //  The router is started only for Collector Node.  Return 0 if successful.
    if (!is_collector_node()) { return 0; }  //  Only start for Collector Nodes, not Sensor Nodes.
    
    //  Open the nRF24L01 driver to start listening.
    {   //  Lock the nRF24L01 driver for exclusive use.
        //  Find the nRF24L01 device by name "nrf24l01_0".
        struct nrf24l01 *dev = (struct nrf24l01 *) os_dev_open(NRF24L01_DEVICE, OS_TIMEOUT_NEVER, NULL);
        assert(dev != NULL);

        //  At this point the nRF24L01 driver will start listening for messages.
        //  Set the callback that will be called when a CBOR message is received.
        int rc = nrf24l01_set_rx_callback(dev, receive_callback);
        assert(rc == 0);

        //  Close the nRF24L01 device when we are done.
        os_dev_close((struct os_dev *) dev);        
    }   //  Unlock the nRF24L01 driver for exclusive use.
    return 0;
}

static void receive_callback(struct os_event *ev) {
    //  Callback that is triggered when we receive an nRF24L01 message.
    //  This callback is triggered by the nRF24L01 receive interrupt,
    //  which is forwarded to the Default Event Queue.
    //  console_printf("%srx interrupt\n", _nrf);
    const char **sensor_node_names = get_sensor_node_names();
    assert(sensor_node_names);
    //  On Collector Node: Check Pipes 1-5 for received data.
    int i;
    for (i = 0; i < NRL24L01_MAX_RX_PIPES * 2; i++) {
        //  Keep checking until there is no more data to process.  For safety, stop after 10 iterations.
        int pipe = -1;
        int rxDataCnt = 0;
        const char *name = NULL;
        {   //  Lock the nRF24L01 driver for exclusive use.
            //  Find the nRF24L01 device by name "nrf24l01_0".
            struct nrf24l01 *dev = (struct nrf24l01 *) os_dev_open(NRF24L01_DEVICE, OS_TIMEOUT_NEVER, NULL);
            assert(dev != NULL);

            //  Get a pipe that has data to receive.
            pipe = nrf24l01_readable_pipe(dev);
            if (pipe > 0) {
                //  Read the data into the receive buffer
                rxDataCnt = nrf24l01_receive(dev, pipe, rxData, MYNEWT_VAL(NRF24L01_TX_SIZE));
                assert(rxDataCnt > 0 && rxDataCnt <= MYNEWT_VAL(NRF24L01_TX_SIZE));
                //  Get the rx (sender) address for the pipe.
                name = sensor_node_names[pipe - 1];
            }
            //  Close the nRF24L01 device when we are done.
            os_dev_close((struct os_dev *) dev);
        }   //  Unlock the nRF24L01 driver for exclusive use.

        //  If no data available, quit.
        if (pipe <= 0) { break; }

        //  Process the received data.
        if (rxDataCnt > 0) { 
            //  Display the receive buffer contents
            console_printf("%srx ", _nrf); console_dump((const uint8_t *) rxData, rxDataCnt); console_printf("\n"); 
            int rc = process_coap_message(name, rxData, rxDataCnt);  //  Process the incoming message and trigger the Remote Sensor.
            assert(rc == 0);
        }
    }
}

static int process_coap_message(const char *name, uint8_t *data, uint8_t size0) {
    //  Process the incoming CoAP payload in "data".  Trigger a request request to the Sensor Framework
    //  that will send the sensor data into the Listener Function for the Remote Sensor.
    //  Payload contains {field1: val1, field2: val2, ...} in CBOR format.
    //  Last byte is sequence number.  Between the CoAP payload and the last byte, all bytes are 0 
    //  and should be discarded before decoding.  "name" is the Sensor Node Address like "b3b4b5b6f1".
    //  Return 0 if successful.
    assert(name);  assert(data);  assert(size0 > 0);
    uint8_t size = size0;
    data[size - 1] = 0;  //  Erase sequence number.
    while (size > 0 && data[size - 1] == 0) { size--; }  //  Discard trailing zeroes.

    //  Decode CoAP Payload (CBOR).
    oc_rep_t *rep = NULL;
    int rc = decode_coap_payload(data, size, &rep);
    assert(rc == 0);
    oc_rep_t *first_rep = rep;

    //  For each field in the payload...
    while(rep) {
        //  Convert the field name to sensor type, e.g. t -> SENSOR_TYPE_AMBIENT_TEMPERATURE_RAW
        sensor_type_t type = remote_sensor_lookup_type(oc_string(rep->name));  
        assert(type);  //  Unknown field name

        //  Fetch the Remote Sensor by name.  "name" looks like "b3b4b5b6f1", the Sensor Node Address.
        struct sensor *remote_sensor = sensor_mgr_find_next_bydevname(name, NULL);
        assert(remote_sensor);  //  Sensor not found

        //  Send the read request to Remote Sensor.  This causes the sensor to be read and Listener Function to be called.
        rc = sensor_read(remote_sensor, type, NULL, rep, 0);
        assert(rc == 0);

        //  Move to next field in the payload.
        rep = rep->next;
    }
    //  Free the decoded representation.
    oc_free_rep(first_rep);
    return 0;
}

static int decode_coap_payload(uint8_t *data, uint8_t size, oc_rep_t **out_rep) {
    //  Decode CoAP Payload in CBOR format from the "data" buffer with "size" bytes.  
    //  Decoded payload will be written to out_rep.  Payload contains {field1: val1, field2: val2, ...}
    //  Return 0 if successful.
    
    //  Convert data buffer to mbuf, since oc_parse_rep() only accepts mbuf.
    int rc;
    struct os_mbuf *om;

    //  Get a packet header mbuf.
    om = os_msys_get_pkthdr(MYNEWT_VAL(NRF24L01_TX_SIZE), 4);
    assert(om);
    if (!om) { return -1; }

    //  Copy data buffer into mbuf.
    rc = os_mbuf_copyinto(om, 0, data, size);
    if (rc) { rc = -2; goto exit; }  //  Out of mbufs.

    //  Parse the mbuf.
    rc = oc_parse_rep(om, 0, size, out_rep);
    assert(rc == 0);

exit:
    //  Free the mbuf.
    os_mbuf_free_chain(om);
    return rc;
}

