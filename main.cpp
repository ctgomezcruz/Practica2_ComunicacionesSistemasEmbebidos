/*******************************************************************************
 * Copyright (c) 2014, 2017 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 *
 * To do :
 *    Add magnetometer sensor output to IoT data stream
 *
 *******************************************************************************/

#include "LM75B.h"
#include "MMA7660.h"
#include "MQTTClient.h"
#include "MQTTEthernet.h"
#include "C12832.h"
#include "Arial12x12.h"
#include "rtos.h"
#include "math.h"

// Update this to the next number *before* a commit
#define __APP_SW_REVISION__ "19"

// Configuration values needed to connect to IBM IoT Cloud
#define ORG "ITESO"             // For a registered connection, replace with your org
#define ID "45070.17.15.37"          // For a registered connection, replace with your id
#define AUTH_TOKEN ""                // For a registered connection, replace with your auth-token
#define TYPE DEFAULT_TYPE_NAME       // For a registered connection, replace with your type

#define MQTT_PORT 18591
#define MQTT_TLS_PORT 8883
#define MQTT_SERVER_PORT MQTT_PORT
#define MQTT_MAX_PACKET_SIZE 250

#if defined(TARGET_UBLOX_C027)
#warning "Compiling for mbed C027"
#include "C027.h"
#elif defined(TARGET_LPC1768)
#warning "Compiling for mbed LPC1768"
#include "LPC1768.h"
#elif defined(TARGET_K64F)
#warning "Compiling for mbed K64F"
#include "K64F.h"
#endif

bool quickstartMode = true;
char org[11] = ORG;  
char type[30] = TYPE;
char id[30] = ID;                 // mac without colons
char auth_token[30] = AUTH_TOKEN; // Auth_token is only used in non-quickstart mode
char recolection_date[16] = "Without date...";
char garbage_kind[22] = "Non-organics";

bool connected = false;
bool mqttConnecting = false;
bool netConnected = false;
bool netConnecting = false;
bool ethernetInitialising = true;
int connack_rc = 0; // MQTT connack return code
int retryAttempt = 0;
int menuItem = 0;

char* joystickPos = "CENTRE";
int blink_interval = 0;

const char* ip_addr = "";
const char* gateway_addr = "n/a";
const char* host_addr = "";
int connectTimeout = 1000;

void off()
{
    r = g = b = 1.0;    // 1 is off, 0 is full brightness
}

void red()
{
    r = 0.7; g = 1.0; b = 1.0;    // 1 is off, 0 is full brightness
}

void yellow()
{
    r = 0.7; g = 0.7; b = 1.0;    // 1 is off, 0 is full brightness
}

void green()
{
    r = 1.0; g = 0.7; b = 1.0;    // 1 is off, 0 is full brightness
}

void flashing_yellow(void const *args)
{
    bool on = false;
    while (!connected && connack_rc != MQTT_NOT_AUTHORIZED && connack_rc != MQTT_BAD_USERNAME_OR_PASSWORD)    // flashing yellow only while connecting 
    {
        on = !on; 
        if (on)
            yellow();
        else
            off();   
        wait(0.5);
    }
}

void flashing_red(void const *args)  // to be used when the connection is lost
{
    bool on = false;
    while (!connected)
    {
        on = !on;
        if (on)
            red();
        else
            off();
        wait(2.0);
    }
}

void printMenu(int menuItem) 
{
    static char last_line1[30] = "", last_line2[30] = "";
    char line1[30] = "", line2[30] = "";
        
    switch (menuItem)
    {
        case 0:
            sprintf(line1, "Smart Trashcan System");
            sprintf(line2, "Scroll with joystick");
            break;
        case 1:
            sprintf(line1, "Device Identity:");
            sprintf(line2, "%s", id);
            break;
        case 2:
            sprintf(line1, "Recolection date:");
            sprintf(line2, "%s", recolection_date);
            break;
        case 3:
            sprintf(line1, "Garbage kind:");
            sprintf(line2, "%s", garbage_kind);
            break;
        case 4:
            sprintf(line1, "Ethernet State:");
            sprintf(line2, ethernetInitialising ? "Initializing..." : "Initialized");
            break;
        case 5:
            sprintf(line1, "Socket State:");
            if (netConnecting)
                sprintf(line2, "Connecting... %d/5", retryAttempt);
            else
                sprintf(line2, netConnected ? "Connected" : "Disconnected");
            break;
        case 6:
            sprintf(line1, "IP Address:");
            sprintf(line2, "%s", ip_addr);
            break;
        case 7:
            sprintf(line1, "MQTT Status:");
            if (mqttConnecting)
                sprintf(line2, "Connecting... %d/5", retryAttempt);
            else
            {
                if (connected)
                    sprintf(line2, "Connected");
                else
                {
                    switch (connack_rc)
                    {
                        case MQTT_CLIENTID_REJECTED:
                            sprintf(line2, "Clientid rejected");
                            break;
                        case MQTT_BAD_USERNAME_OR_PASSWORD:
                            sprintf(line2, "Invalid username or password");
                            break;
                        case MQTT_NOT_AUTHORIZED:
                            sprintf(line2, "Not authorized");
                            break;
                        default:
                            sprintf(line2, "Disconnected");
                    }
                }
            }
            break;
        case 8:
            sprintf(line1, "ITESO");
            sprintf(line2, "Sistemas Embebidos");
            break;
        case 9:
            sprintf(line1, "Cesar T Gomez Cruz");
            sprintf(line2, "Workshop 2");
            break;
    }
    
    if (strcmp(line1, last_line1) != 0 || strcmp(line2, last_line2) != 0)
    {
        lcd.cls(); 
        lcd.locate(0, 0);
        lcd.printf(line1);
        strncpy(last_line1, line1, sizeof(last_line1));

        lcd.locate(0,16);
        lcd.printf(line2);
        strncpy(last_line2, line2, sizeof(last_line2));
    }
}

void setMenu()
{
    if (Down)
    {
        joystickPos = "DOWN";
        if (menuItem >= 0 && menuItem < 9)
            printMenu(++menuItem);
    } 
    else if (Left)
        joystickPos = "LEFT";
    else if (Click)
        joystickPos = "CLICK";
    else if (Up)
    {
        joystickPos = "UP";
        if (menuItem <= 9 && menuItem > 0)
            printMenu(--menuItem);
    }
    else if (Right)
        joystickPos = "RIGHT";
    else
        joystickPos = "CENTRE";
}

void menu_loop(void const *args)
{
    int count = 0;
    while(true)
    {
        setMenu();
        if (++count % 10 == 0)
            printMenu(menuItem);
        Thread::wait(100);
    }
}

/**
 * Display a message on the LCD screen prefixed with IBM IoT Cloud
 */
void displayMessage(char* message)
{
    lcd.cls();
    lcd.locate(0,0);        
    lcd.printf("Smart Trashcan System");
    lcd.locate(0,16);
    lcd.printf(message);
}

int connect(MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE>* client, MQTTEthernet* ipstack)
{   
    const char* mqtt_server = "driver.cloudmqtt.com";
    char hostname[strlen(mqtt_server) + 1];

    sprintf(hostname, "%s", mqtt_server);
    EthernetInterface& eth = ipstack->getEth();
    ip_addr = eth.get_ip_address();
    
    // Construct clientId - d:org:type:id
    char clientId[strlen(org) + strlen(type) + strlen(id) + 5];
    sprintf(clientId, "d:%s:%s:%s", org, type, id);
    
    // Network debug statements 
    LOG("=====================================\r\n");
    LOG("Connecting Ethernet.\r\n");
    LOG("IP ADDRESS: %s\r\n", ip_addr);
    LOG("MAC ADDRESS: %s\r\n", eth.get_mac_address());
    LOG("Server Hostname: %s\r\n", hostname);
    LOG("Client ID: %s\r\n", clientId);
    LOG("=====================================\r\n");
    
    netConnecting = true;
    int rc = ipstack->connect(hostname, MQTT_SERVER_PORT, connectTimeout);
    if (rc != 0)
    {
        WARN("IP Stack connect returned: %d\r\n", rc);    
        return rc;
    }
    netConnected = true;
    netConnecting = false;

    // MQTT Connect
    mqttConnecting = true;
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = clientId;
    
    if (quickstartMode) 
    {        
        data.username.cstring = "Cesar";
        data.password.cstring = "clase2023";
    }
    
    if ((rc = client->connect(data)) == 0) 
    {       
        connected = true;
        green();    
        displayMessage("Connected");
        wait(1);
        displayMessage("Scroll with joystick");
    }
    else
        WARN("MQTT connect returned %d\r\n", rc);
    if (rc >= 0)
        connack_rc = rc;
    mqttConnecting = false;
    return rc;
}

int getConnTimeout(int attemptNumber)
{  // First 10 attempts try within 3 seconds, next 10 attempts retry after every 1 minute
   // after 20 attempts, retry every 10 minutes
    return (attemptNumber < 10) ? 3 : (attemptNumber < 20) ? 60 : 600;
}

void attemptConnect(MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE>* client, MQTTEthernet* ipstack)
{
    connected = false;
   
    // make sure a cable is connected before starting to connect
    while (!linkStatus()) 
    {
        wait(1.0f);
        WARN("Ethernet link not present. Check cable connection\r\n");
    }
        
    while (connect(client, ipstack) != MQTT_CONNECTION_ACCEPTED) 
    {    
        if (connack_rc == MQTT_NOT_AUTHORIZED || connack_rc == MQTT_BAD_USERNAME_OR_PASSWORD)
            return; // don't reattempt to connect if credentials are wrong
            
        Thread red_thread(flashing_red);

        int timeout = getConnTimeout(++retryAttempt);
        WARN("Retry attempt number %d waiting %d\r\n", retryAttempt, timeout);
        
        // if ipstack and client were on the heap we could deconstruct and goto a label where they are constructed
        //  or maybe just add the proper members to do this disconnect and call attemptConnect(...)
        
        // this works - reset the system when the retry count gets to a threshold
        if (retryAttempt == 5)
            NVIC_SystemReset();
        else
            wait(timeout);
    }
}

int publish_level(MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE>* client, MQTTEthernet* ipstack)
{
    MQTT::Message message_level;
    char* pubTopic_level = "STS/level";
    char buf_level[250];
    sprintf(buf_level, "%0.2f", ain1.read()*100);

    message_level.qos = MQTT::QOS0;
    message_level.retained = false;
    message_level.dup = false;
    message_level.payload = (void*)buf_level;
    message_level.payloadlen = strlen(buf_level);
    
    LOG("Publishing %s\r\n", buf_level);
    return client->publish(pubTopic_level, message_level);
}

int publish_toxicity(MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE>* client, MQTTEthernet* ipstack)
{
    MQTT::Message message_toxicity;
    char* pubTopic_toxicity = "STS/toxicity";
    char buf_toxicity[250];
    sprintf(buf_toxicity, "%0.2f", ain2.read()*100);

    message_toxicity.qos = MQTT::QOS0;
    message_toxicity.retained = false;
    message_toxicity.dup = false;
    message_toxicity.payload = (void*)buf_toxicity;
    message_toxicity.payloadlen = strlen(buf_toxicity);
    
    LOG("Publishing %s\r\n", buf_toxicity);
    return client->publish(pubTopic_toxicity, message_toxicity);
}

int publish_temperature(MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE>* client, MQTTEthernet* ipstack)
{
    MQTT::Message message_temperature;
    char* pubTopic_temperature = "STS/temperature";
    char buf_temperature[250];
    sprintf(buf_temperature, "%0.2f", sensor.temp());

    message_temperature.qos = MQTT::QOS0;
    message_temperature.retained = false;
    message_temperature.dup = false;
    message_temperature.payload = (void*)buf_temperature;
    message_temperature.payloadlen = strlen(buf_temperature);
    
    LOG("Publishing %s\r\n", buf_temperature);
    return client->publish(pubTopic_temperature, message_temperature);
}

int publish_position(MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE>* client, MQTTEthernet* ipstack)
{
    MQTT::Message message_position;
    char* pubTopic_position = "STS/position";
    char buf_position[250];
    sprintf(buf_position, "%0.4f", MMA.z());
    double z = atof(buf_position);
    if (z > 1.0) {
        sprintf(buf_position, "%0.4f", 0.00);
    } else {
        sprintf(buf_position, "%0.4f", (180*acos(MMA.z()))/3.1416);
    }

    message_position.qos = MQTT::QOS0;
    message_position.retained = false;
    message_position.dup = false;
    message_position.payload = (void*)buf_position;
    message_position.payloadlen = strlen(buf_position);
    
    LOG("Publishing %s\r\n", buf_position);
    return client->publish(pubTopic_position, message_position);
}

char* getMac(EthernetInterface& eth, char* buf, int buflen)    // Obtain MAC address
{   
    strncpy(buf, eth.get_mac_address(), buflen);
    char* pos;                                                 // Remove colons from mac address

    while ((pos = strchr(buf, ':')) != NULL)
        memmove(pos, pos + 1, strlen(pos) + 1);
    return buf;
}

void messageArrived(MQTT::MessageData& md)
{
    MQTT::Message &message = md.message;
    char topic[md.topicName.lenstring.len + 1];
    char message_str[message.payloadlen + 1];

    sprintf(topic, "%.*s", md.topicName.lenstring.len, md.topicName.lenstring.data);

    if(strcmp(topic, "STS/date") == 0) {
        sprintf(message_str, "%.*s", message.payloadlen, (char *)message.payload);
        strcpy(recolection_date, message_str);
    } else if (strcmp(topic, "STS/kind") == 0) {
        sprintf(message_str, "%.*s", message.payloadlen, (char *)message.payload);
        if(strcmp(message_str, "0") == 0) {
            strcpy(garbage_kind, "Organics");
        } else if(strcmp(message_str, "1") == 0) {
            strcpy(garbage_kind, "Non-organics");
        } else if(strcmp(message_str, "2") == 0) {
            strcpy(garbage_kind, "Plastics");
        } else if(strcmp(message_str, "3") == 0) {
            strcpy(garbage_kind, "Glass");
        } else if(strcmp(message_str, "4") == 0){
            strcpy(garbage_kind, "Cardboard and paper");
        } else if(strcmp(message_str, "5") == 0){
            strcpy(garbage_kind, "Metal and wood");
        } 
    } else {
        if (strcmp(topic, "STS/blink") == 0){
            sprintf(message_str, "%.*s", message.payloadlen, (char *)message.payload);
            blink_interval = atoi(message_str);       
        }
    }
}

int main()
{    
    quickstartMode = true;
    lcd.set_font((unsigned char*) Arial12x12);  // Set a nice font for the LCD screen
    led2 = LED2_OFF; // K64F: turn off the main board LED 
    
    displayMessage("Connecting...");
    Thread yellow_thread(flashing_yellow);
    Thread menu_thread(menu_loop);  
    
    LOG("***** IoT Client Ethernet Example *****\r\n");
    MQTTEthernet ipstack;
    ethernetInitialising = false;
    MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE> client(ipstack);
    LOG("Ethernet Initialized\r\n"); 
    
    if (quickstartMode)
        getMac(ipstack.getEth(), id, sizeof(id));
        
    attemptConnect(&client, &ipstack);
    
    if (connack_rc == MQTT_NOT_AUTHORIZED || connack_rc == MQTT_BAD_USERNAME_OR_PASSWORD)    
    {
        red();
        while (true)
            wait(1.0); // Permanent failures - don't retry
    }
    
    if (quickstartMode) 
    {
        int rc = 0;
        if ((rc = client.subscribe("STS/#", MQTT::QOS1, messageArrived)) != 0) {
            WARN("rc from MQTT subscribe is %d\r\n", rc);
        }
    }
    
    blink_interval = 50;
    int count = 0;
    while (true)
    {
        if (++count % 100 == 0)
        {               // Publish a message every second
            if (publish_level(&client, &ipstack) != 0)
                attemptConnect(&client, &ipstack);   // if we have lost the connection
            if (publish_toxicity(&client, &ipstack) != 0)
                attemptConnect(&client, &ipstack);   // if we have lost the connection
        }

        if (++count % 200 == 0)
        {
            if (publish_temperature(&client, &ipstack) != 0)
                attemptConnect(&client, &ipstack);   // if we have lost the connection
        }
        
        if (++count % 400 == 0)
        {
            if (publish_position(&client, &ipstack) != 0)
                attemptConnect(&client, &ipstack);   // if we have lost the connection
            count = 0;
        }

        if (blink_interval == 0)
            led2 = LED2_OFF;
        else if (count % blink_interval == 0)
            led2 = !led2;
        client.yield(10);  // allow the MQTT client to receive messages
    }
}
