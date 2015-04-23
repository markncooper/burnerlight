
#include <wixel.h>
#include <usb.h>
#include <usb_com.h>
#include <stdio.h>
#include <radio_queue.h>

int32 CODE broadcast_interval = 500;

uint32 lastToggle = 0;

typedef struct colorMap
{
    uint8 length;
    uint8 red;
    uint8 green;
    uint8 blue;
} colorMap;

void updateLeds()
{
	uint8 XDATA report[64];
    uint8 reportLength;
    uint8 XDATA * txBuf;
    //usbShowStatusWithGreenLed();

//    LED_YELLOW(1);
//    LED_YELLOW(1);

    if (getMs() - lastToggle >= broadcast_interval/2)
    {
        if (txBuf = radioQueueTxCurrentPacket()){
        	txBuf[0] = (int8)3;
        	txBuf[1] = !LED_RED_STATE;
        	txBuf[2] = (int8)5;
        	txBuf[3] = (int8)6;

            reportLength = sprintf(report, "packet sent: %02x%02x%02x\r\n", txBuf[0], txBuf[1], txBuf[2]);
            usbComTxSend(report, reportLength);
            radioQueueTxSendPacket();

            LED_YELLOW(!LED_YELLOW_STATE);
            LED_RED(txBuf[1]);
            LED_GREEN(!LED_GREEN_STATE);
            lastToggle = getMs();

        }

    }
}

void main()
{
    systemInit();
    usbInit();
    radioQueueInit();

    while(1)
    {
        boardService();
        updateLeds();
        usbComService();
//        writePacket();
//        writeToConsole();
    }
}
