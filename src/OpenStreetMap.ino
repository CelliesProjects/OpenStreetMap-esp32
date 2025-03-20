#include <bb_spi_lcd.h>
#include <WiFi.h>

#include "OpenStreetMap-esp32.h"

const char *ssid = "your_ssid";
const char *password = "your_password";

BB_SPI_LCD lcd, the_map;
OpenStreetMap osm;

double longitude = 5.9;
double latitude = 51.5;
int zoom = 6;

//SET_LOOP_TASK_STACK_SIZE(32 * 1024);

void setup()
{
    Serial.begin(115200);
    delay(3000);
    // Default Arduino stack size is too small
} /* setup() */

void loop()
{
    Serial.printf("WiFi connecting to %s\n", ssid);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected");

    lcd.begin(DISPLAY_CYD_543);
    lcd.fillScreen(TFT_BLACK);
    osm.setResolution(lcd.width(), lcd.height());

    // create a sprite to store the map
    Serial.println("Creating sprite for map image");
    the_map.createVirtual(lcd.width(), lcd.height()); 
    the_map.fillScreen(TFT_BLACK);

    const bool success = osm.fetchMap(&the_map, longitude, latitude, zoom);

    if (success) {
        lcd.drawSprite(0, 0, &the_map, 0xffffffff);
    } else {
        Serial.println("Failed to fetch map.");
    }
    while (1)
    {
        delay(1000);
    }
} /* loop() */
