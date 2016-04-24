/**
 * @brief Lightweight Embedded System Observation Recording Framework (LES-ORF)
 * @author Adam C. Abernathy, adamabernathy@gmail.com
 *
 * LES-ORF is a lightweight framework intended for Embedded systems using the
 * Arduino (TM) hardware to collect scientific observations from instrument
 * input.
 *
 * Requires the following libraries:
 *     https://github.com/adafruit/Adafruit_CC3000_Library
 *     https://github.com/adafruit/Adafruit-BMP085-Library
 *     https://github.com/bblanchon/ArduinoJson
 *
 * Licence:
 * This software is released under the Apache License Version 2.0 dated
 * January 2004, http://www.apache.org/licenses/ .
 *
 * Portions of CC3000 interface code have been adapted from WiFi examples
 * from Limor Fried & Kevin Townsend which are licenced under BSD licence.
 *
 * @version 0.2.0
 *
 */

#include <Wire.h>
#include <SPI.h>
#include <string.h>
#include <SFE_BMP180.h>
#include "RTClib.h"


//#define ENABLE_WIFI
//#define DEBUG

#if defined(DEBUG)
#include <ccspi.h>
#include "utility/debug.h"
#endif

#if defined(ENABLE_WIFI)
/**
 * @brief Define the WiFi parameters
 *
 * @param WLAN_SECURITY , AP security modes: WLAN_SEC_UNSEC
 *                                           WLAN_SEC_WEP
 *                                           WLAN_SEC_WPA
 *                                           WLAN_SEC_WPA2
 * @param WLAN_SSID
 * @param WLAN_PASS
 * @param IDLE_TIMEOUT_MS , Time to wait (in milliseconds) with no data
 *                          received before closing the TCP/IP socket.
 * @param WEBSITE , Website host
 * @param WEB_HOOK , API path
 */
#include <ArduinoJson.h>
#include <Adafruit_CC3000.h>

#define ADAFRUIT_CC3000_IRQ     3
#define ADAFRUIT_CC3000_VBAT    5
#define ADAFRUIT_CC3000_CS      6
#define WLAN_SSID               ""
#define WLAN_PASS               ""
#define WLAN_SECURITY           WLAN_SEC_WPA2
#define IDLE_TIMEOUT_MS         3000
#define WEBSITE                 ""
#define WEB_HOOK                ""

Adafruit_CC3000 cc3000 = Adafruit_CC3000(ADAFRUIT_CC3000_CS,
                                         ADAFRUIT_CC3000_IRQ,
                                         ADAFRUIT_CC3000_VBAT,
                                         SPI_CLOCK_DIVIDER);
#else
/* SD card interface */
#include "SdFatUtil.h"
#include "SdFat.h"
SdFat SD;
#endif


/**
 * BMP-180 Sensor
 * @param ALTITUDE , Local altitude
 */
const int ALTITUDE = 1390;
SFE_BMP180 BMP;

/**
 * Other global variables
 */
RTC_DS1307 RTC;
uint32_t ip;

/**
 * @brief Initialize the Arduino
 */
void setup(void)
{
    Serial.begin(9600);
    Serial.println(F("Hello Denzian!\n"));

    #if defined(DEBUG)
    Serial.print("Free RAM: "); Serial.println(getFreeRam(), DEC);
    #endif

    /* Initialize the BMP180 sensor package */
    if ( !BMP.begin() ) {
        Serial.write( "BMP180 Error!\n" );
        delay(2000); /* Wait 2 seconds and hope for the best */
    }

    /* Initialise the CC3000 module */
    #if defined(ENABLE_WIFI)
    Serial.println(F("\nInitializing..."));
    if (!cc3000.begin()) {
        Serial.println(F("Couldn't begin()! Check your wiring?"));
        while(1);
    }
    #endif

    #if defined(DEBUG) && defined(ENABLE_WIFI)
    /* Delete any old connection data on the module */
    Serial.println(F("\nDeleting old connection profiles"));
    if (!cc3000.deleteProfiles()) {
      Serial.println(F("Failed!"));
      while(1);
    }
    #endif

    #if defined(ENABLE_WIFI)
    Serial.print(F("\nAttempting to connect to ")); Serial.println(WLAN_SSID);
    if (!cc3000.connectToAP(WLAN_SSID, WLAN_PASS, WLAN_SECURITY)) {
        Serial.println(F("Failed!"));
        while(1);
    }
    Serial.println(F("Connected!"));

    /* Wait for DHCP to complete */
    Serial.println(F("Requesting DHCP ..."));
    while (!cc3000.checkDHCP()) {
        delay(100);
    }

    /* Display the IP address DNS, Gateway, etc. */
    while (! displayConnectionDetails()) {
        delay(1000);
    }

    /* @bug Until the DNS resolve issue is complete, you need to manually
     *      declare the IP of the server here.
     */
    ip = cc3000.IP2U32(155, 101, 3, 198);
    cc3000.printIPdotsRev(ip);
    #else

    /* Start SD card interface */
    if ( !SD.begin(10) ) {
        Serial.write( "SD Card fail!\n" );
        return;
    }
    #endif

}


/**
 * Main operational loop
 * @brief Collect observations and push to server or store on SD card
 */
void loop(void)
{
    char status;
    double tl, p, p0, calt = -999;

    DateTime now = RTC.now();

    /* Get readings from the BMP180 */
    status = BMP.startTemperature();
    if ( status != 0 ) {
        delay(status);
        status = BMP.getTemperature(tl);
    } else {
        Serial.write( "BMP180 temp error\n" );
    }

    if ( status != 0 ) {
        status = BMP.startPressure(3);
        if ( status != 0 ) {
            delay(status);
            status = BMP.getPressure(p,tl);
        }
        if ( status != 0 ) {
            p0 = BMP.sealevel(p,ALTITUDE);
            calt = BMP.altitude(p,p0);
        }
    } else {
        Serial.write( "BMP180 pressure error\n" );
    }

    /**
     * @brief Collect other observations here
     */

    /**
    * @brief Push to record.
    * Depending on the compiled in data option either WiFI or SD will
    * be used to record the data.
    */
    #if defined(ENABLE_WIFI)

    /* Prepare JSON string to push upstream */
    StaticJsonBuffer<150> jsonBuffer;

    JsonObject& root   = jsonBuffer.createObject();
    root["object"]     = "data_upload";
    root["time_local"] = now.unixtime();

    JsonObject& data = root.createNestedObject("data");
    data["temp_onboard"]  = tl;
    data["pressure"]      = p;
    data["slp"]           = p0;
    data["calc_alt"]      = calt;

    Serial.println("\n");
    root.prettyPrintTo(Serial);

    /*
    * Try connecting to the website.
    * Note: HTTP/1.1 protocol is used to keep the server from closing the
    * connection before all data is read.
    */
    Adafruit_CC3000_Client client = cc3000.connectTCP(ip, 80);
    if (client.connected()) {
        Serial.println(F("\n\nPushing upstream\n\n"));
        client.fastrprint(F("GET "));
        client.fastrprint(F(WEB_HOOK));
        client.fastrprint("?push=");
        root.printTo(client);
        client.fastrprint(F("\n"));
        client.fastrprint(F(" HTTP/1.1\r\n"));
        client.fastrprint(F("Host: "));
        client.fastrprint(F("155.101.3.198"));
        client.fastrprint(F("\r\n"));
        client.fastrprint(F("\r\n"));
        client.println();
    } else {
        Serial.println(F("Connection failed"));
        return;
    }

    /*
     * Read data until either the connection is closed, or the idle
     * timeout is reached.
     */

    #if defined(DEBUG) && defined(ENABLE_WIFI)
    Serial.println(F("Server Response:"));
    unsigned long lastRead = millis();
    while (client.connected() && (millis() - lastRead < IDLE_TIMEOUT_MS)) {
        while (client.available()) {
            char c = client.read();
            Serial.print(c);
            lastRead = millis();
        }
    }
    #endif

    client.close(); /* Close connection with server */

    /*
     * Disconnect from AP, else the CC3000 might not work next time.
     */
    Serial.println(F("\nDisconnecting"));
    cc3000.disconnect();
    #else

    File F = SD.open("data.txt", FILE_WRITE);

    /* If the file opened okay, write to it */
    if (F) {
        Serial.println("Saving to SD ...");
        F.print( now.unixtime() );
        F.print( ", " );
        F.print( tl );
        F.print( ", " );
        F.print( p );
        F.print( ", " );
        F.print( p0 );
        F.print( ", " );
        F.println( calt );

        F.close();
    } else {
        /* If the file didn't open, print an error */
        Serial.write( "\n\nSD Card Error!\n" );
    }
    #endif

    Serial.write( "Idling...\n" );
    delay(60000 * 1); /* Wait to collect the next observation */
}


/**
 * @brief Read the IP address and other connection details
 * @return boolean
 */
#if defined(ENABLE_WIFI)
bool displayConnectionDetails(void)
{
    uint32_t ipAddress, netmask, gateway, dhcpserv, dnsserv;

    if( !cc3000.getIPAddress(&ipAddress, &netmask, &gateway,
                             &dhcpserv, &dnsserv)) {
        Serial.println(F("Unable to retrieve the IP Address!\r\n"));
        return false;
    } else {
        Serial.print(F("\nIP Addr: ")); cc3000.printIPdotsRev(ipAddress);
        Serial.print(F("\nNetmask: ")); cc3000.printIPdotsRev(netmask);
        Serial.print(F("\nGateway: ")); cc3000.printIPdotsRev(gateway);
        Serial.print(F("\nDHCPsrv: ")); cc3000.printIPdotsRev(dhcpserv);
        Serial.print(F("\nDNSserv: ")); cc3000.printIPdotsRev(dnsserv);
        Serial.println();
        return true;
    }
}
#endif

/**
 * @brief Reboot the system
 */
void(* soft_reboot) (void) = 0; /* declare reset function @ address 0 */


/**
 * @brief Need to check the validity of our sensor returns
 * @param double number
 * @return double number
 */
double check_quality(double number)
{
    if (isnan(number) || isinf(number) ||
        number > 4294967040.0 || number <-4294967040.0){

        return -999.0;
    } else {
        return number;
    }
}
