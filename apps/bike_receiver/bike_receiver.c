
#include <wixel.h>
#include <usb.h>
#include <usb_com.h>
#include <stdio.h>
#include <radio_queue.h>

int32 CODE broadcast_interval = 200;

uint32 lastToggle = 0;

void updateLeds()
{
    //usbShowStatusWithGreenLed();

//    LED_YELLOW(1);
//    LED_YELLOW(1);

    if (getMs() - lastToggle >= broadcast_interval/2)
    {
        LED_YELLOW(!LED_YELLOW_STATE);
        LED_RED(!LED_RED_STATE);
        LED_GREEN(!LED_GREEN_STATE);
        lastToggle = getMs();
    }
}

static uint16 nextReportTime = 0;

/*
void writeToConsole(){
//    if ((uint16)(getMs()) > nextReportTime)
//    {
        uint8 XDATA report[64];
        uint8 reportLength = sprintf(report, "packet read: %u, %u, %u\r\n", report[0], report[1], report[2]);
        usbComTxSend(report, reportLength);

        nextReportTime = (uint16)getMs() + 1000;
//    }

}*/

uint8 XDATA * rxBuf;
uint8 XDATA report[80];
uint8 reportLength;

typedef struct adcReport
{
    uint8 length;
    uint8 red;
    uint8 green;
    uint8 blue;
} colorMap;

void readPacket(){
	colorMap XDATA * rxPacket;

    if (rxPacket = (colorMap XDATA *)radioQueueRxCurrentPacket()){
//        LED_YELLOW(!LED_YELLOW_STATE);

        reportLength = sprintf(report, "read: %02x - r:%02x g:%02x b:%02x\r\n", rxPacket->length, rxPacket->red, rxPacket->green, rxPacket->blue);
        usbComTxSend(report, reportLength);

//        reportLength = sprintf(report, "packet read - len:\r\n");
//        usbComTxSend(report, reportLength);

        LED_RED(rxPacket->red);
        radioQueueRxDoneWithPacket();
    }
}

void main()
{
    systemInit();
    usbInit();
    radioQueueInit();

    //writeToConsole();
    while(1)
    {
        boardService();
        //updateLeds();
        usbComService();
        readPacket();
        //writeToConsole();
    }
}
