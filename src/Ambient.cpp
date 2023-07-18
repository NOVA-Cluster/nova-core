#include <Arduino.h>
#include "Ambient.h"
#include "configuration.h"
#include "NovaIO.h"
// #include "SerialProto.h"
#include "main.h"
#include "pb_arduino.h"
#include "output/Star.h"
#include "LightUtils.h"

#include "messaging.pb.h"

/*

This class manages the background tasks that control:
  - Lighting
  - Power management

*/

Ambient *ambient = NULL;

Ambient::Ambient()
{

    messaging_Request request = messaging_Request_init_zero;
    Serial.println("Ambient setup started");

    Serial2.begin(NOVANET_BAUD);

    // Setup goes in here
}

void Ambient::loop()
{
    uint32_t currentTime = millis();

    // Serial.println("Ambient loop");
    // sendProtobuf();

    CRGB *leds = lightUtils->getLeds();

    /*
    for (int i = 0; i < lightUtils->getNumberOfLeds(); i++)
    {
        CRGB color = leds[i];
        uint8_t redValue = color.r;
        uint8_t greenValue = color.g;
        uint8_t blueValue = color.b;
        // Do something with the color
    }
    */

    uint32_t frameTime = micros();

    uint8_t lightsPre = 3;
    uint8_t lightsPost = 3;

    uint8_t dmxFixture = 0;
    for (int starIndex = 0; starIndex < 12; starIndex++)
    {
        uint8_t dmxValues[DMX512_MAX] = {};

        // Set the selected star to transmit.
        star->netOut(starIndex);

        if (starIndex == 0) // First Star
        {

            dmxValues[0] = 0x00; // Always 0x00 (other values are reserved)
            for (int lightsPreIndex = 0; lightsPreIndex < lightsPre; lightsPreIndex++)
            {
                dmxValues[1 + (7 * lightsPreIndex)] = 0xff;               // Brightness
                dmxValues[2 + (7 * lightsPreIndex)] = leds[dmxFixture].r; // red
                dmxValues[3 + (7 * lightsPreIndex)] = leds[dmxFixture].g; // green
                dmxValues[4 + (7 * lightsPreIndex)] = leds[dmxFixture].b; // blue
                dmxValues[5 + (7 * lightsPreIndex)] = 0x00;               // null
                dmxValues[6 + (7 * lightsPreIndex)] = 0x00;               // null
                dmxValues[7 + (7 * lightsPreIndex)] = 0x00;               // null

                dmxFixture++;
            }
        }
        else if (starIndex == 11) // Last Star
        {
            dmxValues[0] = 0x00; // Always 0x00 (other values are reserved)
            //for (int lightsPostIndex = 0; lightsPostIndex < lightsPost; lightsPostIndex++)
            for (int lightsPostIndex = lightsPost - 1; lightsPostIndex >= 0; lightsPostIndex--)
            {
                dmxValues[1 + (7 * lightsPostIndex)] = 0xff;               // Brightness
                dmxValues[2 + (7 * lightsPostIndex)] = leds[dmxFixture].r; // red
                dmxValues[3 + (7 * lightsPostIndex)] = leds[dmxFixture].g; // green
                dmxValues[4 + (7 * lightsPostIndex)] = leds[dmxFixture].b; // blue
                dmxValues[5 + (7 * lightsPostIndex)] = 0x00;               // null
                dmxValues[6 + (7 * lightsPostIndex)] = 0x00;               // null
                dmxValues[7 + (7 * lightsPostIndex)] = 0x00;               // null

                dmxFixture++;
            }
        }
        else
        {

            dmxValues[0] = 0x00; // Always 0x00 (other values are reserved)

            dmxValues[1] = 0xff;               // Brightness
            dmxValues[2] = leds[dmxFixture].r; // red
            dmxValues[3] = leds[dmxFixture].g; // green
            dmxValues[4] = leds[dmxFixture].b; // blue
            dmxValues[5] = 0x00;               // null
            dmxValues[6] = 0x00;               // null
            dmxValues[7] = 0x00;               // null

            dmxFixture++;
        }

        sendDmxMessage(dmxValues, DMX512_MAX);
    }

    if (currentTime - lastExecutionTime >= 5000)
    {
        // Calculate average frame time
        unsigned long avgFrameTime = totalFrameTime / numFrames;

        // Print statistics
        Serial.printf("Average frame time: %d (microseconds) %d (milliseconds) %d (fps) \n", avgFrameTime, avgFrameTime / 1000, 1000 / (avgFrameTime / 1000));

        // Reset statistics
        totalFrameTime = 0;
        numFrames = 0;

        // Update last execution time
        lastExecutionTime = currentTime;
    }

    totalFrameTime += micros() - frameTime;
    numFrames++;
}

/**
 * Sends a DMX message with the given DMX values over NovaNet.
 *
 * Think of this as the display driver. It takes the DMX values
 * and sends them to the display.
 *
 * @param dmxValues The DMX values to send.
 * @param dmxValuesSize The size of the DMX values array.
 */
void Ambient::sendDmxMessage(uint8_t *dmxValues, size_t dmxValuesSize)
{

    uint8_t newDmxValues[dmxValuesSize] = {};
    memcpy(newDmxValues, dmxValues, dmxValuesSize - 1);

    // Create a DmxRequest object
    messaging_DmxRequest dmxRequest = messaging_DmxRequest_init_zero;

    memcpy(dmxRequest.values.bytes, newDmxValues, dmxValuesSize);

    // Find the last index with data
    int lastIndexWithData = 0;
    for (int i = DMX512_MAX - 1; i >= 0; i--)
    {
        if (newDmxValues[i] != 0)
        {
            lastIndexWithData = i + 1;
            break;
        }
    }

    if (0)
    {
        Serial.println(lastIndexWithData);
        for (int i = 0; i < lastIndexWithData; i++)
        {
            Serial.print("newDmxValues[");
            Serial.print(i);
            Serial.print("] = ");
            Serial.println((int)newDmxValues[i]);
        }
        delay(10000);
    }

    dmxRequest.values.size = lastIndexWithData;

    dmxRequest.ack = false; // Request acknoledgement

    // Create a Request object and set its type to DMX_REQUEST
    messaging_Request request = messaging_Request_init_zero;
    // request.type = messaging_RequestType_REQUEST_DMX;
    request.request_payload.dmx_request = dmxRequest;
    request.which_request_payload = messaging_Request_dmx_request_tag;

    // Initialize a buffer stream for the encoded message
    uint8_t buffer[NOVABUF_MAX];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    // Encode the protobuf
    if (!pb_encode(&stream, messaging_Request_fields, &request))
    {
        // Encode error. Maybe the buffer isn't big enough?
        Serial.println("PB_Encode Error!!!");
    }

    if (0)
    {
        // Print the size of the encoded message.
        Serial.println(stream.bytes_written);

        // Print the encoded message in hexadecimal format.
        for (size_t i = 0; i < stream.bytes_written; i++)
        {
            if (buffer[i] < 16)
            {
                Serial.print('0'); // print leading zero for single-digit hex values
            }
            Serial.print(buffer[i], HEX);
        }
        Serial.println();
    }

    // Calculate the CRC of the protobuf
    uint16_t protobuf_crc = crc16_ccitt(buffer, stream.bytes_written);

    // Prepare the header: F0 9F 92 A5 followed by the CRC and the size of the protobuf
    uint8_t header[4] = {0xF0, 0x9F, 0x92, 0xA5};
    uint16_t msg_size = stream.bytes_written;

    // Send the header
    Serial2.write(header, sizeof(header));

    // Send the CRC of the protobuf
    Serial2.write((uint8_t *)&protobuf_crc, sizeof(protobuf_crc));

    // Send the size of the protobuf
    Serial2.write((uint8_t *)&msg_size, sizeof(msg_size));

    // Then send the protobuf
    Serial2.write(buffer, msg_size);

    // TODO: Cleanup the magic numbers.
    //  8 microseconds for each byte
    //
    //  8 bytes in the header before the message
    //  sizeof(msg_size) for the message.
    delayMicroseconds(8 * (8 + sizeof(msg_size)));

    // Wait for to make sure that the serial buffer is empty
    Serial2.flush(true);

    // Serial.println("Written");

    // delay(100); // Wait a bit before reading
    return;

    if (0)
    {

        // Read and check the header
        uint8_t received_header[4];
        while (Serial2.available() < sizeof(received_header))
        {
            // Wait until the header has been received
            yield();
        }
        Serial2.readBytes((char *)received_header, sizeof(received_header));
        if (memcmp(received_header, header, sizeof(header)) != 0)
        {
            // Handle the error: invalid header
            return;
        }

        // Read the CRC of the protobuf
        uint16_t received_protobuf_crc;
        while (Serial2.available() < sizeof(received_protobuf_crc))
        {
            // Wait until the CRC has been received
            yield();
        }
        Serial2.readBytes((char *)&received_protobuf_crc, sizeof(received_protobuf_crc));

        // Read the size of the received protobuf
        while (Serial2.available() < sizeof(msg_size))
        {
            // Wait until the size has been received
            yield();
        }
        uint16_t received_size;
        Serial2.readBytes((char *)&received_size, sizeof(received_size));

        // Wait until the entire protobuf has been received
        while (Serial2.available() < received_size)
        {
            // Wait
            yield();
        }

        // Now read the protobuf
        uint8_t received_buffer[NOVABUF_MAX];
        Serial2.readBytes((char *)received_buffer, received_size);

        // Calculate the CRC of the received protobuf
        uint16_t calculated_protobuf_crc = crc16_ccitt(received_buffer, received_size);
        if (received_protobuf_crc != calculated_protobuf_crc)
        {
            // Handle the error: invalid CRC
            return;
        }

        // Initialize a protobuf input stream
        pb_istream_t pb_istream = pb_istream_from_buffer(received_buffer, received_size);

        // Decode the received protobuf
        messaging_Request received_msg = messaging_Request_init_zero;
        if (!pb_decode(&pb_istream, messaging_Request_fields, &received_msg))
        {
            // Handle the decoding error
        }
    }
}

bool encode_callback(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
    uint8_t *dmxValues = (uint8_t *)(*arg);
    if (!pb_encode_tag_for_field(stream, field))
    {
        return false;
    }
    return pb_encode_string(stream, dmxValues, 512);
}

uint16_t Ambient::crc16(const uint8_t *data_p, uint16_t length)
{
    uint8_t x;
    uint16_t crc = 0xFFFF;

    while (length--)
    {
        x = crc >> 8 ^ *data_p++;
        x ^= x >> 4;
        crc = (crc << 8) ^ ((uint16_t)(x << 12)) ^ ((uint16_t)(x << 5)) ^ ((uint16_t)x);
    }
    return crc;
}

// CRC-16-CCITT function
uint16_t Ambient::crc16_ccitt(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF; // Initialize CRC to 0xFFFF

    // Iterate over the data
    for (uint16_t i = 0; i < length; i++)
    {
        crc ^= data[i] << 8; // XOR the current byte with the CRC

        // Iterate over the bits in the byte
        for (uint16_t j = 0; j < 8; j++)
        {
            // If the MSB of the CRC is 1, XOR with the polynomial
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021; // Polynomial: x^16 + x^12 + x^5 + 1
            }
            else // Otherwise, shift the CRC left by 1 bit
            {
                crc <<= 1;
            }
        }
    }

    return crc; // Return the final CRC value
}