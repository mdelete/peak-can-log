/*
    File:           PeakUSBUserspaceDriver.c
 
    Description:    User-space driver using IOKitLib and IOUSBLib for PEAK PCAN-USB CAN to USB Adapters.
                    Based on USBPrivateDataSample and the pcan linux driver.
 
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

#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#include "PeakUSB.h"

#pragma mark Globals

typedef struct MyPrivateData {
    io_object_t				notification;
    IOUSBDeviceInterface	**deviceInterface;
    CFStringRef				deviceName;
} MyPrivateData;

static CFNotificationCenterRef      gNotificationCenter = NULL;
static IONotificationPortRef        gNotifyPort;
static io_iterator_t                gAddedIter;
static CFRunLoopRef                 gRunLoop;

//FIXME put into MyPrivateData, if feasible
static char                         gBufferReceive[64], gBufferSend[64];
static UInt8                        gTelegramCount;
static int                          gMsgCounter = 0;
static time_t                       gLast = 0;
static IOUSBInterfaceInterface**    gInterface = NULL;
static PCAN_USB_TIME                gUsbTime;
static UInt16                       gLastBitrate = CAN_BAUD_125K;

#pragma mark - Timestamp magic

static void calcTimevalFromTicks(struct timeval *tv)
{
	register PCAN_USB_TIME *t = &gUsbTime;
	UInt64 llx;
	UInt32 nb_s, nb_us;
    
	llx = t->ullCumulatedTicks - t->wStartTicks; // subtract initial offset
	llx *= PCAN_USB_TS_US_PER_TICK;
	llx >>= PCAN_USB_TS_DIV_SHIFTER;

	nb_s = (UInt32) llx / 1000000;
	nb_us = (UInt32)((UInt64)llx - (nb_s * 1000000));
	
	tv->tv_usec = t->StartTime.tv_usec + nb_us;
	if (tv->tv_usec > 1000000)
	{
		tv->tv_usec -= 1000000;
		nb_s++;
	}
	
	tv->tv_sec = t->StartTime.tv_sec + nb_s;
}

static void updateTimeStampFromWord(CanMsg* msg, UInt16 wTimeStamp, UInt8 ucStep)
{
	register PCAN_USB_TIME *t = &gUsbTime;
    
	if ((!t->StartTime.tv_sec) && (!t->StartTime.tv_usec))
	{
        gettimeofday(&t->StartTime, NULL);
		t->wStartTicks          = wTimeStamp;
		t->wOldLastTickValue    = wTimeStamp;
		t->ullCumulatedTicks    = wTimeStamp;
		t->ullOldCumulatedTicks = wTimeStamp;
	}
    
	// correction for status timestamp in the same telegram which is more recent, restore old contents
	if (ucStep)
	{
		t->ullCumulatedTicks = t->ullOldCumulatedTicks;
		t->wLastTickValue    = t->wOldLastTickValue;
	}
    
	// store current values for old ...
	t->ullOldCumulatedTicks = t->ullCumulatedTicks;
	t->wOldLastTickValue    = t->wLastTickValue;
    
	if (wTimeStamp < t->wLastTickValue)  // handle wrap, enhance tolerance
		t->ullCumulatedTicks += 0x10000LL;
    
	t->ullCumulatedTicks &= ~0xFFFFLL;   // mask in new 16 bit value - do not cumulate cause of error propagation
	t->ullCumulatedTicks |= wTimeStamp;
    
	t->wLastTickValue   = wTimeStamp;      // store for wrap recognition
	t->ucLastTickValue  = (UInt8)(wTimeStamp & 0xff); // each update for 16 bit tick updates the 8 bit tick, too
    
    calcTimevalFromTicks(&msg->ts);
}

static void updateTimeStampFromByte(CanMsg* msg, UInt8 ucTimeStamp)
{
	register PCAN_USB_TIME *t = &gUsbTime;
    
	if (ucTimeStamp < t->ucLastTickValue)  // handle wrap
	{
		t->ullCumulatedTicks += 0x100;
		t->wLastTickValue    += 0x100;
	}
    
	t->ullCumulatedTicks &= ~0xFFULL;      // mask in new 8 bit value - do not cumulate cause of error propagation
	t->ullCumulatedTicks |= ucTimeStamp;
    
	t->wLastTickValue    &= ~0xFF;         // correction for word timestamp, too
	t->wLastTickValue    |= ucTimeStamp;
    
	t->ucLastTickValue    = ucTimeStamp;   // store for wrap recognition
    
    calcTimevalFromTicks(&msg->ts);
}

#pragma mark - Buffer decoding

void DecodeMessages(void)
{
    UInt8 i, j;
    UInt8* ucMsgPtr = (UInt8*)&gBufferReceive[0];
    CanTimeStamp ts;
    
    UInt8 ucPrefix = *ucMsgPtr++; // ignore
    assert(ucPrefix == 2);
    UInt8 ucMessageLen = *ucMsgPtr++;
    
    for(i = 0; i < ucMessageLen; i++)
    {
        UInt8 ucStatusLen = *ucMsgPtr++;
        CanMsg* msg = malloc(sizeof(CanMsg));

        if (!(ucStatusLen & STLN_INTERNAL_DATA)) // real message
        {
            msg->len = ucStatusLen & STLN_DATA_LENGTH;
            if (msg->len > 8) msg->len = 8;
            msg->rtr = (ucStatusLen & STLN_RTR) > 0;
            msg->ext = (ucStatusLen & STLN_EXTENDED_ID) > 0;
            msg->err = 0;
            
            if (ucStatusLen & STLN_EXTENDED_ID)
			{
				msg->canid.uc[0] = *ucMsgPtr++;
				msg->canid.uc[1] = *ucMsgPtr++;
				msg->canid.uc[2] = *ucMsgPtr++;
				msg->canid.uc[3] = *ucMsgPtr++;
				msg->canid.ul >>= 3;
			}
			else
			{
				msg->canid.ul = 0;
				msg->canid.uc[0] = *ucMsgPtr++;
				msg->canid.uc[1] = *ucMsgPtr++;
				msg->canid.ul >>= 5;
			}
            
            if(i == 0) // only the first packet supplies a word timestamp
            {
                ts.uc[0] = *ucMsgPtr++;
                ts.uc[1] = *ucMsgPtr++;
                updateTimeStampFromWord(msg, ts.uw, i);
            } else {
                updateTimeStampFromByte(msg, *ucMsgPtr++);
            }
#ifdef DEBUG
            printf("Timestamp:%lu Flags:0x%02x Id:0x%02x Len:%d Rtr:%s Ext:%s\n", (unsigned long)msg->ts.tv_sec, ucStatusLen, (unsigned)msg->canid.ul, msg->len, (msg->rtr) ? "yes" : "no", (msg->ext) ? "yes" : "no");
#endif
            for(j = 0; j < msg->len; j++)
                msg->data[j] = *ucMsgPtr++;
            
            gMsgCounter++;
            
            CFNotificationCenterPostNotification (gNotificationCenter, CFSTR("CanMsg"), msg, NULL, true);
        }
        else
        {
            // internal data & errors
            UInt8 ucFunction = *ucMsgPtr++;
            UInt8 ucNumber = *ucMsgPtr++;
            
            msg->canid.uc[0] = ucFunction;
            msg->canid.uc[1] = ucNumber;
            
            if (ucStatusLen & STLN_WITH_TIMESTAMP)
            {
                if(i == 0) { // only the first packet supplies a word timestamp
                    ts.uc[0] = *ucMsgPtr++;
                    ts.uc[1] = *ucMsgPtr++;
                    updateTimeStampFromWord(msg, ts.uw, i);
                } else {
                    updateTimeStampFromByte(msg, *ucMsgPtr++);
                }
            }
            
            switch (ucFunction) {
                case 1:
                    {
                        if (ucNumber & CAN_RECEIVE_QUEUE_OVERRUN)
                            printf("CAN_RECEIVE_QUEUE_OVERRUN\n");
                        
                        if (ucNumber & QUEUE_OVERRUN)
                            printf("CAN_QUEUE_OVERRUN\n");
                        
                        if (ucNumber & BUS_OFF)
                            printf("BUS_OFF\n");
                        
                        if (ucNumber & BUS_HEAVY)
                            printf("BUS_HEAVY\n");
                        
                        if (ucNumber & BUS_LIGHT)
                            printf("BUS_LIGHT\n");
#ifdef DEBUG                        
                        if (ucNumber == 0)
                            printf("No error\n");
#endif
                    }
                    break;
                case 2: // get_analog_value, remove bytes
                    ucMsgPtr++;
                    ucMsgPtr++;
                    break;
                case 3: // get_bus_load, remove byte
                    ucMsgPtr++;
                    break;
                case 4:
                    ts.uc[0] = *ucMsgPtr++;
                    ts.uc[1] = *ucMsgPtr++;
                    updateTimeStampFromWord(msg, ts.uw, i);
                    break;
                case 5: // ErrorFrame/ErrorBusEvent.
                    if (ucNumber & QUEUE_XMT_FULL)
                    {
                        printf("QUEUE_XMT_FULL signaled, ucNumber = 0x%02x\n", ucNumber);
                        //dev->wCANStatus |= CAN_ERR_QXMTFULL; // fatal error!
                        //dev->dwErrorCounter++;
                    }
                    
                    //j = 0;
                    //while (ucLen--)
                    //    msg.Msg.DATA[j++] = *ucMsgPtr++;
                    break;
            }
            CFNotificationCenterPostNotification (gNotificationCenter, CFSTR("CanStatus"), msg, NULL, true);
#ifdef DEBUG            
            printf("Status Function:%d Number:%d Timestamp:%06lu.%06u\n", ucFunction, ucNumber, msg->ts.tv_sec, msg->ts.tv_usec);
#endif
        }
    }
    
    time_t now = time(NULL);
    if(now > gLast) {
        gLast = now;
        CFNotificationCenterPostNotification (gNotificationCenter, CFSTR("CanDevice"), &gMsgCounter, NULL, true);
        gMsgCounter = 0;
    }
}

#pragma mark - Synchronous ctrl I/O functions

IOReturn WriteToCtrlPipe(IOUSBInterfaceInterface **interface, const PCAN_USB_PARAM param)
{
    return (*interface)->WritePipe(interface, kPeakUsbCtrlInputPipe, (void*)&param, sizeof(PCAN_USB_PARAM));
}

IOReturn ReadFromCtrlPipe(IOUSBInterfaceInterface **interface, const PCAN_USB_PARAM param)
{
    IOReturn kr = (*interface)->WritePipe(interface, kPeakUsbCtrlInputPipe, (void*)&param, sizeof(PCAN_USB_PARAM));
    UInt32 numBytesRead = 16;
    int i;
    
    if (kr == kIOReturnSuccess)
    {
        usleep(5);

        kr = (*interface)->ReadPipe(interface, kPeakUsbCtrlOutputPipe, gBufferReceive, &numBytesRead);
        if (kr == kIOReturnSuccess)
        {
            for (i = 0; i < 16; i++)
                printf("%02X ", (UInt8)gBufferReceive[i]);
            printf(" (%ld)\n", (long)numBytesRead);
        } else {
            printf("Unable to perform ctrl read (%08x)\n", kr);
        }
    
    }
    
    return kr;
}

#pragma mark - Asynchronous bulk I/O functions

void BulkWriteCompletion(void *refCon, IOReturn result, void *arg0)
{
    IOUSBInterfaceInterface **interface = (IOUSBInterfaceInterface **) refCon;
#ifdef DEBUG
    UInt64 numBytesWritten = (UInt64) arg0;
    printf("Asynchronous bulk write complete\n");
#endif
    if (result != kIOReturnSuccess)
    {
        printf("error from asynchronous bulk write (%08x)\n", result);
        (void) (*interface)->USBInterfaceClose(interface);
        (void) (*interface)->Release(interface);
        return;
    }
#ifdef DEBUG
    printf("Wrote %lld bytes to bulk endpoint\n", (long long)numBytesWritten);
#endif
}

IOReturn WriteToBulkPipe(IOUSBInterfaceInterface **interface)
{
    IOReturn kr = (*interface)->WritePipeAsync(interface, kPeakUsbBulkWritePipe, gBufferSend, sizeof(gBufferSend), BulkWriteCompletion, (void *) interface);
    
    if (kr != kIOReturnSuccess)
    {
        printf("Unable to perform asynchronous bulk write (%08x)\n", kr);
        (void) (*interface)->USBInterfaceClose(interface);
        (void) (*interface)->Release(interface);
    }
    
    return kr;
}

void ReadFromBulkPipe(IOUSBInterfaceInterface **interface);

void BulkReadCompletion(void *refCon, IOReturn result, void *arg0)
{
    IOUSBInterfaceInterface **interface = (IOUSBInterfaceInterface **) refCon;
    UInt64 numBytesRead = (UInt64) arg0;
    
#ifdef DEBUG
    printf("Asynchronous bulk read complete (%ld)\n", (long)numBytesRead);
#endif
    
    if (result != kIOReturnSuccess) {
        printf("Error from async bulk read (%08x)\n", result);
        (void) (*interface)->USBInterfaceClose(interface);
        (void) (*interface)->Release(interface);
        return;
    }
    
    if(numBytesRead > 0) {
        DecodeMessages();
#ifdef DEBUG
        printf("Decoded message\n");
        int i;
        for (i = 0; i < numBytesRead; i++) {
            printf("%02X ", (UInt8)gBufferReceive[i]);
            if(i == 31) printf("\n");
        }
        printf("\n");
#endif        
    }
    
    ReadFromBulkPipe(interface);
}

void ReadFromBulkPipe(IOUSBInterfaceInterface **interface)
{
    UInt32 numBytesRead = 64;
    IOReturn kr = (*interface)->ReadPipeAsync(interface, kPeakUsbBulkReadPipe, gBufferReceive, numBytesRead, BulkReadCompletion, (void*)interface);
    
    if (kr != kIOReturnSuccess)
    {
        printf("Unable to read async interface (%08x)\n", kr);
    }
}

#pragma mark - Entry points and USB device handling stuff

IOReturn PeakInit(UInt16 bitrate)
{
    gLastBitrate = bitrate;
    
    if(gInterface == NULL)
        return kIOReturnNoDevice;
    
    IOUSBInterfaceInterface **interface = gInterface;
    PCAN_USB_PARAM br = { 1, 2, { (bitrate & 0xff), (bitrate >> 8), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
    
    IOReturn kr = WriteToCtrlPipe(interface, PCAN_CTRL_CANOFF);
    if (kr != kIOReturnSuccess)
    {
        printf("Unable to perform PCAN_CTRL_CANOFF (%08x)\n", kr);
        return kr;
    }
    
    kr = WriteToCtrlPipe(interface, PCAN_CTRL_SJA1000INIT);
    if (kr != kIOReturnSuccess)
    {
        printf("Unable to perform PCAN_CTRL_SJA1000INIT (%08x)\n", kr);
        return kr;
    }
    
    kr = WriteToCtrlPipe(interface, br);
    if (kr != kIOReturnSuccess)
    {
        printf("Unable to perform PCAN_CTRL_BITRATE_... (%08x)\n", kr);
        return kr;
    }
    
    kr = WriteToCtrlPipe(interface, PCAN_CTRL_SILENTOFF);
    if (kr != kIOReturnSuccess)
    {
        printf("Unable to perform PCAN_CTRL_SILENTOFF (%08x)\n", kr);
        return kr;
    }
    
    kr = WriteToCtrlPipe(interface, PCAN_CTRL_EXTVCCOFF);
    if (kr != kIOReturnSuccess)
    {
        printf("Unable to perform PCAN_CTRL_EXTVCCOFF (%08x)\n", kr);
        return kr;
    }
    
    kr = WriteToCtrlPipe(interface, PCAN_CTRL_CANON);
    if (kr != kIOReturnSuccess)
    {
        printf("Unable to perform PCAN_CTRL_CANON (%08x)\n", kr);
        return kr;
    }
    
    return kr;
}

IOReturn ConfigureDevice(IOUSBDeviceInterface **dev)
{
    UInt8 numConfig;
    IOReturn kr;
    IOUSBConfigurationDescriptorPtr configDesc;
    //Get the number of configurations. The sample code always chooses
    //the first configuration (at index 0) but your code may need a
    //different one
    kr = (*dev)->GetNumberOfConfigurations(dev, &numConfig);
    if (!numConfig)
        return -1;
    //Get the configuration descriptor for index 0
    kr = (*dev)->GetConfigurationDescriptorPtr(dev, 0, &configDesc);
    if (kr)
    {
        printf("Couldn’t get configuration descriptor for index %d (err = %08x)\n", 0, kr);
        return -1;
    }
    //Set the device’s configuration. The configuration value is found in
    //the bConfigurationValue field of the configuration descriptor
    kr = (*dev)->SetConfiguration(dev, configDesc->bConfigurationValue);
    if (kr)
    {
        printf("Couldn’t set configuration to value %d (err = %08x)\n", 0, kr);
        return -1;
    }
    return kIOReturnSuccess;
}

IOReturn FindInterfaces(IOUSBDeviceInterface **device)
{
    IOReturn kr;
    IOUSBFindInterfaceRequest request;
    io_iterator_t iterator;
    io_service_t usbInterface;
    IOCFPlugInInterface **plugInInterface = NULL;
    IOUSBInterfaceInterface **interface = NULL;
    HRESULT result;
    SInt32 score;
    UInt8 interfaceClass;
    UInt8 interfaceSubClass;
    UInt8 interfaceNumEndpoints;
    int pipeRef;
    
    CFRunLoopSourceRef runLoopSource;
    
    //Placing the constant kIOUSBFindInterfaceDontCare into the following
    //fields of the IOUSBFindInterfaceRequest structure will allow you
    //to find all the interfaces
    request.bInterfaceClass = kIOUSBFindInterfaceDontCare;
    request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
    request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    request.bAlternateSetting = kIOUSBFindInterfaceDontCare;
    //Get an iterator for the interfaces on the device
    kr = (*device)->CreateInterfaceIterator(device, &request, &iterator);
    
    while ((usbInterface = IOIteratorNext(iterator)))
    {
        //Create an intermediate plug-in
        kr = IOCreatePlugInInterfaceForService(usbInterface,
                                               kIOUSBInterfaceUserClientTypeID,
                                               kIOCFPlugInInterfaceID,
                                               &plugInInterface, &score);
        //Release the usbInterface object after getting the plug-in
        kr = IOObjectRelease(usbInterface);
        if ((kr != kIOReturnSuccess) || !plugInInterface)
        {
            printf("Unable to create a plug-in (%08x)\n", kr);
            break;
        }
        //Now create the device interface for the interface
        result = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID), (LPVOID *) &interface);
        //No longer need the intermediate plug-in
        (*plugInInterface)->Release(plugInInterface);
        if (result || !interface)
        {
            printf("Couldn’t create a device interface for the interface (%08x)\n", (int) result);
            break;
        }
        //Get interface class and subclass
        kr = (*interface)->GetInterfaceClass(interface, &interfaceClass);
        kr = (*interface)->GetInterfaceSubClass(interface, &interfaceSubClass);
        
        printf("Interface class %d, subclass %d\n", interfaceClass, interfaceSubClass);
        //Now open the interface. This will cause the pipes associated with
        //the endpoints in the interface descriptor to be instantiated
        kr = (*interface)->USBInterfaceOpen(interface);
        if (kr != kIOReturnSuccess)
        {
            printf("Unable to open interface (%08x)\n", kr);
            (void) (*interface)->Release(interface);
            break;
            
        }
        //Get the number of endpoints associated with this interface
        kr = (*interface)->GetNumEndpoints(interface, &interfaceNumEndpoints);
        if (kr != kIOReturnSuccess)
        {
            printf("Unable to get number of endpoints (%08x)\n", kr);
            (void) (*interface)->USBInterfaceClose(interface);
            (void) (*interface)->Release(interface);
            break;
        }
        printf("Interface has %d endpoints\n", interfaceNumEndpoints);
        //Access each pipe in turn, starting with the pipe at index 1
        //The pipe at index 0 is the default control pipe and should be
        //accessed using (*usbDevice)->DeviceRequest() instead
        for (pipeRef = 1; pipeRef <= interfaceNumEndpoints; pipeRef++)
        {
            IOReturn kr2;
            UInt8 direction;
            UInt8 number;
            UInt8 transferType;
            UInt16 maxPacketSize;
            UInt8 interval;
            char *message;
            
            kr2 = (*interface)->GetPipeProperties(interface, pipeRef, &direction, &number, &transferType, &maxPacketSize, &interval);
            
            if (kr2 != kIOReturnSuccess)
            {
                printf("Unable to get properties of pipe %d (%08x)\n", pipeRef, kr2);
            }
            else
            {
                printf("PipeRef %d: ", pipeRef);
                switch (direction)
                {
                    case kUSBOut:
                        message = "out";
                        break;
                    case kUSBIn:
                        message = "in";
                        break;
                    case kUSBNone:
                        message = "none";
                        break;
                    case kUSBAnyDirn:
                        message = "any";
                        break;
                    default:
                        message = "???";
                }
                printf("direction %s, ", message);
                switch (transferType)
                {
                    case kUSBControl:
                        message = "control";
                        break;
                    case kUSBIsoc:
                        message = "isoc";
                        break;
                    case kUSBBulk:
                        message = "bulk";
                        break;
                    case kUSBInterrupt:
                        message = "interrupt";
                        break;
                    case kUSBAnyType:
                        message = "any";
                        break;
                    default:
                        message = "???";
                }
                printf("transfer type %s, number %x maxPacketSize %d\n", message, number, maxPacketSize);
            }
        }
        
        //As with service matching notifications, to receive asynchronous
        //I/O completion notifications, you must create an event source and
        //add it to the run loop
        kr = (*interface)->CreateInterfaceAsyncEventSource(interface, &runLoopSource);
        
        if (kr != kIOReturnSuccess)
        {
            printf("Unable to create asynchronous event source (%08x)\n", kr);
            (void) (*interface)->USBInterfaceClose(interface);
            (void) (*interface)->Release(interface);
            break;
        }
        CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopDefaultMode);
        printf("Asynchronous event source added to run loop\n");
        
        // set interface before init
        gInterface = interface;
        
        kr = PeakInit(gLastBitrate);
        
        if (kr != kIOReturnSuccess)
        {
            (void) (*interface)->USBInterfaceClose(interface);
            (void) (*interface)->Release(interface);
            break;
        }
        
        ReadFromCtrlPipe(interface, PCAN_CTRL_READ_SNR);
        ReadFromCtrlPipe(interface, PCAN_CTRL_READ_QUARTZ);
        ReadFromCtrlPipe(interface, PCAN_CTRL_READ_DEVICENO);
        ReadFromCtrlPipe(interface, PCAN_CTRL_READ_BITRATE);
        
        ReadFromBulkPipe(interface); // start reading from the bulk input interface
        
        //just use first interface, so exit loop
        break;
    }
    return kr;
}


//================================================================================================
//
//	DeviceNotification
//
//	This routine will get called whenever any kIOGeneralInterest notification happens.  We are
//	interested in the kIOMessageServiceIsTerminated message so that's what we look for.  Other
//	messages are defined in IOMessage.h.
//
//================================================================================================
void DeviceNotification(void *refCon, io_service_t service, natural_t messageType, void *messageArgument)
{
    kern_return_t	kr;
    MyPrivateData	*privateDataRef = (MyPrivateData *) refCon;
    
    if (messageType == kIOMessageServiceIsTerminated) {
        fprintf(stderr, "Device removed.\n");
        
        // Dump our private data to stderr just to see what it looks like.
        fprintf(stderr, "privateDataRef->deviceName: ");
		CFShow(privateDataRef->deviceName);
        
        // Free the data we're no longer using now that the device is going away
        CFRelease(privateDataRef->deviceName);
        
        if (privateDataRef->deviceInterface) {
            kr = (*privateDataRef->deviceInterface)->Release(privateDataRef->deviceInterface);
        }
        
        CFNotificationCenterPostNotification (gNotificationCenter, CFSTR("CanDevice"), NULL, NULL, true);
        
        kr = IOObjectRelease(privateDataRef->notification);
        
        free(privateDataRef);
    }
    else
        fprintf(stderr, "DeviceNotification %x\n", messageType);
}

//================================================================================================
//
//	DeviceAdded
//
//	This routine is the callback for our IOServiceAddMatchingNotification.  When we get called
//	we will look at all the devices that were added and we will:
//
//	1.  Create some private data to relate to each device (in this case we use the service's name
//	    and the location ID of the device
//	2.  Submit an IOServiceAddInterestNotification of type kIOGeneralInterest for this device,
//	    using the refCon field to store a pointer to our private data.  When we get called with
//	    this interest notification, we can grab the refCon and access our private data.
//
//================================================================================================
void DeviceAdded(void *refCon, io_iterator_t iterator)
{
    kern_return_t		kr;
    io_service_t		usbDevice;
    IOCFPlugInInterface	**plugInInterface = NULL;
    SInt32				score;
    HRESULT 			res;
    
    while ((usbDevice = IOIteratorNext(iterator)))
    {
        io_name_t		deviceName;
        CFStringRef		deviceNameAsCFString;
        MyPrivateData	*privateDataRef = NULL;
        
        printf("Device added.\n");
        
        privateDataRef = malloc(sizeof(MyPrivateData));
        bzero(privateDataRef, sizeof(MyPrivateData));
        
        // Get the USB device's name.
        kr = IORegistryEntryGetName(usbDevice, deviceName);
        
        if (KERN_SUCCESS != kr)
        {
            deviceName[0] = '\0';
        }
        
        deviceNameAsCFString = CFStringCreateWithCString(kCFAllocatorDefault, deviceName, kCFStringEncodingASCII);
        
        // Dump our data to stderr just to see what it looks like.
        fprintf(stderr, "deviceName: ");
        CFShow(deviceNameAsCFString);
        
        // Save the device's name to our private data.
        privateDataRef->deviceName = deviceNameAsCFString;
        
        kr = IOCreatePlugInInterfaceForService(usbDevice, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
                                               &plugInInterface, &score);
        
        if ((kIOReturnSuccess != kr) || !plugInInterface) {
            fprintf(stderr, "IOCreatePlugInInterfaceForService returned 0x%08x.\n", kr);
            continue;
        }
        
        // Use the plugin interface to retrieve the device interface.
        res = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                                                 (LPVOID*) &privateDataRef->deviceInterface);
        
        // Now done with the plugin interface.
        (*plugInInterface)->Release(plugInInterface);
        
        if (res || privateDataRef->deviceInterface == NULL) {
            fprintf(stderr, "QueryInterface returned %d.\n", (int) res);
            continue;
        }
                
        // Open the device to change its state
        kr = (*privateDataRef->deviceInterface)->USBDeviceOpen(privateDataRef->deviceInterface);
        if (kr != kIOReturnSuccess)
        {
            printf("Unable to open device: %08x\n", kr);
            (void) (*privateDataRef->deviceInterface)->Release(privateDataRef->deviceInterface);
            continue;
        }
        
        //Configure device
        kr = ConfigureDevice(privateDataRef->deviceInterface);
        if (kr != kIOReturnSuccess)
        {
            printf("Unable to configure device: %08x\n", kr);
            (void) (*privateDataRef->deviceInterface)->USBDeviceClose(privateDataRef->deviceInterface);
            (void) (*privateDataRef->deviceInterface)->Release(privateDataRef->deviceInterface);
            continue;
        }
        
        kr = FindInterfaces(privateDataRef->deviceInterface);
        
        // Register for an interest notification of this device being removed. Use a reference to our
        // private data as the refCon which will be passed to the notification callback.
        kr = IOServiceAddInterestNotification(gNotifyPort,						// notifyPort
											  usbDevice,						// service
											  kIOGeneralInterest,				// interestType
											  DeviceNotification,				// callback
											  privateDataRef,					// refCon
											  &(privateDataRef->notification)	// notification
											  );
        
        if (KERN_SUCCESS != kr) {
            printf("IOServiceAddInterestNotification returned 0x%08x.\n", kr);
        }
        
        // Notify AppDelegate to remove the 'no device' info and display the message counter
        CFNotificationCenterPostNotification (gNotificationCenter, CFSTR("CanDevice"), &gMsgCounter, NULL, true);
        
        // Done with this USB device; release the reference added by IOIteratorNext
        kr = IOObjectRelease(usbDevice);
    }
}

IOReturn PeakSend(CanMsg* msg)
{
    int i;
    CanId tc;
    tc.ul = msg->canid.ul;
    bzero(gBufferSend, sizeof(gBufferSend));
    UInt8* ucMsgPtr = (UInt8*)&gBufferSend[0];
    
    UInt8* pucStatusLen;
	UInt8* pucMsgCountPtr;
    
    *ucMsgPtr++ = 2; // starts with a magic value
    pucMsgCountPtr = ucMsgPtr++;
    pucStatusLen = ucMsgPtr++;
    *pucStatusLen = msg->len & STLN_DATA_LENGTH;
    
    if (msg->rtr)
        *pucStatusLen |= STLN_RTR; // add RTR flag
    
    if (msg->ext)
    {
        *pucStatusLen |= STLN_EXTENDED_ID;
        tc.ul <<= 3;
        *ucMsgPtr++ = tc.uc[0];
        *ucMsgPtr++ = tc.uc[1];
        *ucMsgPtr++ = tc.uc[2];
        *ucMsgPtr++ = tc.uc[3];
    }
    else
    {
        tc.ul <<= 5;
        *ucMsgPtr++ = tc.uc[0];
        *ucMsgPtr++ = tc.uc[1];
    }
    
    if (!msg->rtr)
    {
        for(i = 0; i < msg->len; i++)
            *ucMsgPtr++ = msg->data[i];
    }
    
    // FIXME this part is somewhat hinky, I could not fully recover the linux driver functionality here
    if(gTelegramCount++ > 200) gTelegramCount = 1;
    *ucMsgPtr++ = gTelegramCount;
    *pucMsgCountPtr = 1;
    *ucMsgPtr++ = 0;
    
    if(gInterface)
        WriteToBulkPipe(gInterface);
    
    return kIOReturnSuccess;
}

//================================================================================================
//	PeakStop
//================================================================================================
IOReturn PeakStop()
{
    CFRunLoopStop(gRunLoop);
    return kIOReturnSuccess;
}

//================================================================================================
//	PeakStart
//================================================================================================
IOReturn PeakStart()
{
    CFMutableDictionaryRef 	matchingDict;
    CFRunLoopSourceRef		runLoopSource;
    CFNumberRef				numberRef;
    kern_return_t			kr;
    SInt32					usbVendor = kPeakVendorID;
    SInt32					usbProduct = kPeakProductID;
    
    fprintf(stderr, "Looking for devices matching vendor ID=%d and product ID=%d.\n", (int)usbVendor, usbProduct);
    
    // Set up the matching criteria for the devices we're interested in. The matching criteria needs to follow
    // the same rules as kernel drivers: mainly it needs to follow the USB Common Class Specification, pp. 6-7.
    // See also Technical Q&A QA1076 "Tips on USB driver matching on Mac OS X"
	// <http://developer.apple.com/qa/qa2001/qa1076.html>.
    // One exception is that you can use the matching dictionary "as is", i.e. without adding any matching
    // criteria to it and it will match every IOUSBDevice in the system. IOServiceAddMatchingNotification will
    // consume this dictionary reference, so there is no need to release it later on.
    
    matchingDict = IOServiceMatching(kIOUSBDeviceClassName);	// Interested in instances of class
                                                                // IOUSBDevice and its subclasses
    if (matchingDict == NULL) {
        fprintf(stderr, "IOServiceMatching returned NULL.\n");
        return -1;
    }
    
    // We are interested in all USB devices (as opposed to USB interfaces).  The Common Class Specification
    // tells us that we need to specify the idVendor, idProduct, and bcdDevice fields, or, if we're not interested
    // in particular bcdDevices, just the idVendor and idProduct.  Note that if we were trying to match an 
    // IOUSBInterface, we would need to set more values in the matching dictionary (e.g. idVendor, idProduct, 
    // bInterfaceNumber and bConfigurationValue.
    
    // Create a CFNumber for the idVendor and set the value in the dictionary
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &usbVendor);
    CFDictionarySetValue(matchingDict, 
                         CFSTR(kUSBVendorID), 
                         numberRef);
    CFRelease(numberRef);
    
    // Create a CFNumber for the idProduct and set the value in the dictionary
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &usbProduct);
    CFDictionarySetValue(matchingDict, 
                         CFSTR(kUSBProductID), 
                         numberRef);
    CFRelease(numberRef);
    numberRef = NULL;

    gNotificationCenter = CFNotificationCenterGetLocalCenter();
    
    // Create a notification port and add its run loop event source to our run loop
    // This is how async notifications get set up.
    
    gNotifyPort = IONotificationPortCreate(kIOMasterPortDefault);
    runLoopSource = IONotificationPortGetRunLoopSource(gNotifyPort);
    
    gRunLoop = CFRunLoopGetCurrent();
    CFRunLoopAddSource(gRunLoop, runLoopSource, kCFRunLoopDefaultMode);
    
    // Now set up a notification to be called when a device is first matched by I/O Kit.
    kr = IOServiceAddMatchingNotification(gNotifyPort,					// notifyPort
                                          kIOFirstMatchNotification,	// notificationType
                                          matchingDict,					// matching
                                          DeviceAdded,					// callback
                                          NULL,							// refCon
                                          &gAddedIter					// notification
                                          );		
                                            
    // Iterate once to get already-present devices and arm the notification    
    DeviceAdded(NULL, gAddedIter);	

    // Start the run loop. Now we'll receive notifications.
    fprintf(stderr, "Starting run loop.\n");
    
    CFRunLoopRun();
        
    fprintf(stderr, "Stopped run loop.\n");
    return kIOReturnSuccess;
}
