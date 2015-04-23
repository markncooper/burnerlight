#include <cc2511_map.h>
#include <wixel.h>
#include <usb.h>
#include <usb_com.h>
#include <stdio.h>
#include <radio_queue.h>
#include <random.h>
#include "pingpong.h"

#define SHIFTBRITE_LATCH P1_7
#define SHIFTBRITE_DATA P1_6
#define SHIFTBRITE_CLOCK P1_5
#define SHIFTBRITE_DISABLE P1_4

// parameters
int32 CODE param_input_bits = 8;
int32 CODE param_echo_on = 1;

// This is initialized to be equal to param_input_bits, but restricted to be within 1..16.
uint8 input_bits;

// The number of hex characters it takes to express a color.
// 1-4 input bits = 1 char; 5-8 input bits = 2 chars; etc.
uint8 hex_chars_per_color;

// amount to shift to create the output
int8 shift;
void sendBit(BIT value);
void toggleLatch();
void sendRGB(uint16 r, uint16 g, uint16 b);


uint8 XDATA * rxBuf;
uint8 XDATA report[80];
uint8 reportLength;


int MSG_TYPE_COLOR_CMD = 0;
typedef struct colorCommand
{
    uint8 length;
    uint8 msgType;
    uint8 red;
    uint8 green;
    uint8 blue;
} colorCommand;

int MSG_TYPE_BROADCAST_ME_CMD = 1;
typedef struct broadcastMe
{
    uint8 length;
    uint8 msgType;
    uint8 address[4];
} broadcastMe;

void readPacket(){
	colorCommand XDATA * rxPacket;

    if (rxPacket = (colorCommand XDATA *)radioQueueRxCurrentPacket()){
//        LED_YELLOW(!LED_YELLOW_STATE);

        reportLength = sprintf(report, "read: %02x - r:%02x g:%02x b:%02x\r\n", rxPacket->length, rxPacket->red, rxPacket->green, rxPacket->blue);
        usbComTxSend(report, reportLength);

        reportLength = sprintf(report, "read: %02x %02x %02x %02x\r\n", serialNumber[0], serialNumber[1], serialNumber[2], serialNumber[3]);
        usbComTxSend(report, reportLength);

//        reportLength = sprintf(report, "packet read - len:\r\n");
//        usbComTxSend(report, reportLength);

        sendRGB(0, 0, 100);
        toggleLatch();

        LED_RED(rxPacket->red);
        radioQueueRxDoneWithPacket();
    }
}


int32 toInt(uint8 serial[]){
	int32 result = (((int32)serial[0]) << 24) + (((int32)serial[1]) << 16) + (((int32)serial[2]) << 8) + (((int32)serial[3]));
	return result;
}

bool isLeader = false;
bool isSlave = false;

int32 CODE status_strobe_interval = 1000;
uint32 lastStatusStrobe = 0;

void stobeLeaderFollowerLights(){
    if (getMs() > lastStatusStrobe)
    {
        lastStatusStrobe = getMs() + 1000;
    	sendRGB(100, 0, 100);
    	toggleLatch();

        if (isLeader) LED_GREEN_TOGGLE();
        if (isSlave)  LED_RED_TOGGLE();
//        if (!isLeader && !isSlave) LED_YELLOW_TOGGLE();
    }
}

/**
 * ShiftBrite code
 */

void sendBit(BIT value)
{
    SHIFTBRITE_DATA = value;
    delayMicroseconds(1);
    SHIFTBRITE_CLOCK = 1;
    delayMicroseconds(1);
    SHIFTBRITE_CLOCK = 0;
    delayMicroseconds(1);
}

void sendRGB(uint16 r, uint16 g, uint16 b)
{
    uint16 mask = 512;
    sendBit(0);
    sendBit(0);
    while(mask)
    {
        sendBit((mask & b) ? 1 : 0);
        mask >>= 1;
    }
    mask = 512;
    while(mask)
    {
        sendBit((mask & r) ? 1 : 0);
        mask >>= 1;
    }
    mask = 512;
    while(mask)
    {
        sendBit((mask & g) ? 1 : 0);
        mask >>= 1;
    }
}

// limits value to lie between min and max
int32 restrictRange(int32 value, int32 min, int32 max)
{
    if(value < min)
        return min;
    if(value > max)
        return max;
    return value;
}

void shiftbriteInit()
{
    input_bits = restrictRange(param_input_bits,1,16); // allow up to 16 bits = 4 hex digits
    hex_chars_per_color = ((input_bits-1) >> 2) + 1;
    shift = 10 - input_bits;

    SHIFTBRITE_DISABLE = 1; // disable shiftbrites until a valid color is sent
    SHIFTBRITE_CLOCK = 0; // clock low
    SHIFTBRITE_LATCH = 0; // prevent unintended latching
    P1DIR |= (1<<4); // P1_4 = Disable !Enable
    P1DIR |= (1<<5); // P1_5 = Clock
    P1DIR |= (1<<6); // P1_6 = Data
    P1DIR |= (1<<7); // P1_7 = Latch
}

void toggleLatch()
{
    SHIFTBRITE_LATCH = 1;
    delayMicroseconds(1);
    SHIFTBRITE_LATCH = 0;
    delayMicroseconds(1);
    SHIFTBRITE_DISABLE = 0; // enable shiftbrites
}


//
// Leader discovery
//
uint32 myPriorityID;
int MSG_TYPE_DISCOVER_LEADER_CMD = 1;
typedef struct discoverLeaderCommand
{
    uint8 length;
    uint8 msgType;
    uint32 id;
} discoverLeaderCommand;

uint32 slaveStateExpiration = 0;
uint32 nextHelloBroadcast = 0;

void broadcastIdAndListen(){
	discoverLeaderCommand XDATA * rxPacket;
	discoverLeaderCommand XDATA * txPacket;

    if (rxPacket = (discoverLeaderCommand XDATA *)radioQueueRxCurrentPacket()){
    	if (rxPacket->id > myPriorityID){
    		if (isSlave) LED_GREEN(0);
    		isSlave = true;
    		isLeader = false;
    		slaveStateExpiration = getMs() + 3000;
    	}

    	// IDs are equal? choose a new ID? Hopefully they converge quickly
    	if (rxPacket->id == myPriorityID){
    		myPriorityID = randomNumber();
    	}
    	radioQueueRxDoneWithPacket();
    }

    // Haven't heard of any IDs higher than mine? Guess I'm the master.
    if ( getMs() > slaveStateExpiration){
    	isLeader = true;
    	isSlave = false;
		LED_RED(0)
    }

    // Send out my priority
    if (getMs() > nextHelloBroadcast){
    	nextHelloBroadcast = getMs() + 100;

    	txPacket = (discoverLeaderCommand XDATA *) radioQueueTxCurrentPacket();
    	txPacket->length = 6;
    	txPacket->id = myPriorityID;
    	txPacket->msgType = MSG_TYPE_DISCOVER_LEADER_CMD;
    	radioQueueTxSendPacket();
    }
}

void main()
{
    systemInit();
    usbInit();
    radioQueueInit();
    shiftbriteInit();
    randomSeedFromSerialNumber();
    myPriorityID = randomNumber();

    //writeToConsole();
    while(1)
    {
    	//
    	// Default services
    	//
        boardService();
        usbComService();
        stobeLeaderFollowerLights();

        //
        // Determine a leader
        //
        broadcastIdAndListen();
        //updateLeds();
//        readPacket();
        //writeToConsole();
    }
}
