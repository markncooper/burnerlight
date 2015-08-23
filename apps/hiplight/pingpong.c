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
void sendRGBa(uint16 rgb[]);


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

int32 toInt(uint8 serial[]){
	int32 result = (((int32)serial[0]) << 24) + (((int32)serial[1]) << 16) + (((int32)serial[2]) << 8) + (((int32)serial[3]));
	return result;
}

bool isLeader = false;
bool isSlave = false;

int32 CODE status_strobe_interval = 20;
uint32 lastStatusStrobe = 0;

uint16 patternOne[6][3] = {
		{255, 0, 0},
		{0, 255, 0},
		{0, 0, 255},
		{255, 0, 0},
		{0, 255, 0},
		{0, 0, 255}};

uint16 patternPosition = 0;

typedef struct {
    float r;       // percent
    float g;       // percent
    float b;       // percent
} rgb;

typedef struct {
	float h;       // angle in degrees
	float s;       // percent
	float v;       // percent
} hsv;


void hsv2rgb(float hin, float sin, float vin, float* rr, float* gg, float* bb)
{
	float hh;
	float p;
    float q;
    float t;
    float ff;
    long i;


	hsv in;
	in.h = hin;
	in.s = sin;
	in.v = vin;

    if(in.s <= 0.0) {       // < is bogus, just shuts up warnings
        *rr = in.v;
        *gg = in.v;
        *bb = in.v;
        return;
    }
    hh = in.h;
    if (hh >= 360.0) hh = 0.0;
    hh /= 60.0;
    i = (uint8)hh;
    ff = hh - i;
    p = in.v * (1.0 - in.s);
    q = in.v * (1.0 - (in.s * ff));
    t = in.v * (1.0 - (in.s * (1.0 - ff)));

    switch(i) {
    case 0:
        *rr = in.v;
        *gg = t;
        *bb = p;
        break;
    case 1:
    	*rr = q;
        *gg = in.v;
        *bb = p;
        break;
    case 2:
    	*rr = p;
    	*gg = in.v;
        *bb = t;
        break;

    case 3:
    	*rr = p;
    	*gg = q;
        *bb = in.v;
        break;
    case 4:
    	*rr = t;
    	*gg = p;
        *bb = in.v;
        break;
    case 5:
    default:
    	*rr = in.v;
        *gg = p;
        *bb = q;
        break;
    }
    return;
}


void stobeLeaderFollowerLights(){
    float r;
    float g;
    float b;

    float h = 0;

    if (getMs() > lastStatusStrobe)
    {
        lastStatusStrobe = getMs() + status_strobe_interval;
//        uint16 foo = patternOne[1]

        hsv2rgb(patternPosition, 1, 1, &r, &g, &b);

        sendRGB( (uint8)(r*255), (uint8)(g*255), (uint8)(b*255) );
        sendRGB( (uint8)(r*255), (uint8)(g*255), (uint8)(b*255) );
        sendRGB( (uint8)(r*255), (uint8)(g*255), (uint8)(b*255) );
        sendRGB( (uint8)(r*255), (uint8)(g*255), (uint8)(b*255) );
    	patternPosition += 1;
    	if (patternPosition > 360) patternPosition = 0;

    	toggleLatch();

        if (isLeader) LED_YELLOW_TOGGLE();
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

void sendRGBa(uint16 rgb[])
{
	sendRGB(rgb[0], rgb[1], rgb[2]);
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
uint32 highestIDSeen;
uint32 highestIDSeenExpirationMS;

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
    		if (isSlave) LED_YELLOW(0);
    		isSlave = true;
    		isLeader = false;

    		//
    		// Keep track of the highest ID and use it for clock synchronization
    		// Keep track of the last time this highest ID was seen
    		//
    		if (highestIDSeen == rxPacket->id){
    			highestIDSeenExpirationMS = getMs();
    		}
    		if (highestIDSeen < rxPacket->id ){
    			highestIDSeen = rxPacket->id;
    	        lastStatusStrobe = getMs() + status_strobe_interval;
    		}

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

    sendRGB(255, 255, 255);

    //writeToConsole();
    while(1)
    {
    	//
    	// Default services
    	//
        boardService();
        usbComService();

        //
        // Determine a leader
        //
        broadcastIdAndListen();

        //
        // Do something fun with the lights.
        //
        stobeLeaderFollowerLights();
    }
}
