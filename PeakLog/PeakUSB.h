/*
    File:           PeakUSB.h
 
    Description:    User-space driver using IOKitLib and IOUSBLib for PEAK PCAN-USB CAN to USB Adapters.
                    Based on USBPrivateDataSample and the Linux driver.
 
    Copyright:      © Copyright 2012 Marc Delling. All rights reserved.
 
    Disclaimer:     IMPORTANT: THE SOFTWARE IS PROVIDED ON AN "AS IS" BASIS. THE AUTHOR MAKES NO
                    WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED
                    WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
                    PURPOSE, REGARDING THE SOFTWARE OR ITS USE AND OPERATION ALONE OR IN
                    COMBINATION WITH YOUR PRODUCTS.
 
                    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR
                    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
                    GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
                    ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION
                    OF SOFTWARE, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT
                    (INCLUDING NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN IF THE AUTHOR HAS
                    BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PeakLog_PeakUSB_h
#define PeakLog_PeakUSB_h

#include <CoreFoundation/CoreFoundation.h>
#include <sys/time.h>

// peak vendor and device id
#define kPeakVendorID		0x0c72
#define kPeakProductID		0x000c

// enumeration of the four usb pipes
#define kPeakUsbCtrlOutputPipe  1
#define kPeakUsbCtrlInputPipe   2
#define kPeakUsbBulkReadPipe    3
#define kPeakUsbBulkWritePipe   4

// bit masks for status/length field in a USB message
#define STLN_WITH_TIMESTAMP 0x80
#define STLN_INTERNAL_DATA  0x40
#define STLN_EXTENDED_ID    0x20
#define STLN_RTR            0x10
#define STLN_DATA_LENGTH    0x0F // mask for length of data bytes

// Error-Flags for PCAN-USB
#define XMT_BUFFER_FULL           0x01
#define CAN_RECEIVE_QUEUE_OVERRUN 0x02
#define BUS_LIGHT                 0x04
#define BUS_HEAVY                 0x08
#define BUS_OFF                   0x10
#define QUEUE_RECEIVE_EMPTY       0x20
#define QUEUE_OVERRUN             0x40
#define QUEUE_XMT_FULL            0x80

// bitrate codes of BTR0/BTR1 registers
#define CAN_BAUD_1M     (uint16_t)0x0014      //   1 MBit/s
#define CAN_BAUD_500K   (uint16_t)0x001C      // 500 kBit/s
#define CAN_BAUD_250K   (uint16_t)0x011C      // 250 kBit/s
#define CAN_BAUD_125K   (uint16_t)0x031C      // 125 kBit/s
#define CAN_BAUD_100K   (uint16_t)0x432F      // 100 kBit/s
#define CAN_BAUD_50K    (uint16_t)0x472F      //  50 kBit/s
#define CAN_BAUD_20K    (uint16_t)0x532F      //  20 kBit/s
#define CAN_BAUD_10K    (uint16_t)0x672F      //  10 kBit/s
#define CAN_BAUD_5K     (uint16_t)0x7F7F      //   5 kBit/s

static const uint16_t CAN_BAUD_RATES[9] = {
    0x0014, 0x001C, 0x011C, 0x031C, 0x432F, 0x472F, 0x532F, 0x672F, 0x7F7F
};

// Some timing-constants from the linux driver
#define PCAN_USB_TS_DIV_SHIFTER          20
#define PCAN_USB_TS_US_PER_TICK    44739243

// Activity states
#define ACTIVITY_NONE        0          // LED off           - set when the channel is created or deleted
#define ACTIVITY_INITIALIZED 1          // LED on            - set when the channel is initialized
#define ACTIVITY_IDLE        2          // LED slow blinking - set when the channel is ready to receive or transmit
#define ACTIVITY_XMIT        3          // LED fast blinking - set when the channel has received or transmitted

#define CAN_ERROR_ACTIVE     0          // CAN-Bus error states for busStatus - initial and normal state
#define CAN_ERROR_PASSIVE    1          // receive only state
#define CAN_BUS_OFF          2          // switched off from Bus

typedef union {
    UInt8 uc[4];
    UInt32 ul;
} CanId;

typedef union {
    UInt8 uc[2];
    UInt16 uw;
} CanTimeStamp;

typedef struct {
    CanId canid; // 11 Bit/29 Bit
    struct timeval ts;
    UInt8 ext:1; // CAN2.0B message if 1
    UInt8 rtr:1; // RTR frame if 1
    UInt8 err:1; // Error frame if 1
    UInt8 loc:1; // Origin of frame: 0 = CAN-Bus, 1 = PeakLog
    UInt8 len:4; // Length of data (0-8 bytes)
    union {
        UInt8 data[8];
        UInt16 sdata[4];
        UInt32 idata[2];
        UInt64 ldata;
    };
} CanMsg;

// The adapted usb ctrl structure (the ctrl pipes are 16 bytes)
typedef struct
{
	UInt8  Function;
	UInt8  Number;
	UInt8  Param[14];
} __attribute__ ((packed)) PCAN_USB_PARAM;

// The adapted time structure from the linux driver
typedef struct
{
	UInt64  ullCumulatedTicks;         // sum of all ticks
	UInt64  ullOldCumulatedTicks;      // old ...
	struct timeval StartTime;          // time of first receive
	UInt16  wStartTicks;               // ticks at first init
	UInt16  wLastTickValue;            // Last aquired tick count
	UInt16  wOldLastTickValue;         // old ...
	UInt8   ucLastTickValue;           // the same for byte tick counts
} PCAN_USB_TIME;

static const PCAN_USB_PARAM PCAN_CTRL_BITRATE125KHZ = { 1, 2, { 0x1c, 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
static const PCAN_USB_PARAM PCAN_CTRL_CANON = { 3, 2, { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
static const PCAN_USB_PARAM PCAN_CTRL_CANOFF = { 3, 2, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
static const PCAN_USB_PARAM PCAN_CTRL_SJA1000INIT = { 9, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
static const PCAN_USB_PARAM PCAN_CTRL_SILENTOFF = { 3, 3, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
static const PCAN_USB_PARAM PCAN_CTRL_EXTVCCOFF =  { 10, 2, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
static const PCAN_USB_PARAM PCAN_CTRL_READ_BITRATE = { 1, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
static const PCAN_USB_PARAM PCAN_CTRL_READ_QUARTZ = { 2, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
static const PCAN_USB_PARAM PCAN_CTRL_READ_DEVICENO = { 4, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
static const PCAN_USB_PARAM PCAN_CTRL_READ_SNR = { 6, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };

IOReturn PeakInit(UInt16 bitrate);
IOReturn PeakStart(void);
IOReturn PeakStop(void);
IOReturn PeakSend(CanMsg* msg);

#endif
