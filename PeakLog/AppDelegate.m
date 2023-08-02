/*
    File:           AppDelegate.m
 
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

#import "AppDelegate.h"
#import "LogLine.h"

#include "PeakUSB.h"

@implementation AppDelegate
{
    NSUInteger arrayControllerMaxSize;
}

@synthesize arrayController, bitratePopup;

- (IBAction)setBitrate:(NSPopUpButtonCell*)sender
{
    PeakInit(CAN_BAUD_RATES[sender.indexOfSelectedItem]);
}

- (IBAction)clearLog:(id)sender
{
    NSRange range = NSMakeRange(0, [[arrayController arrangedObjects] count]);
    [arrayController removeObjectsAtArrangedObjectIndexes:[NSIndexSet indexSetWithIndexesInRange:range]];
}

- (IBAction)trimLog:(id)sender
{
    NSUInteger size = [[arrayController arrangedObjects] count];
    NSInteger oversize = size - arrayControllerMaxSize;
    if(oversize > 0)
    {
        NSRange range = NSMakeRange(0, oversize);
        [arrayController removeObjectsAtArrangedObjectIndexes:[NSIndexSet indexSetWithIndexesInRange:range]];
    }
}

- (IBAction)pasteCanMessage:(id)sender
{
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    NSArray *classes = [[NSArray alloc] initWithObjects:[NSAttributedString class], [NSString class], nil];
    NSDictionary *options = [NSDictionary dictionary];
 
    if ([pasteboard canReadObjectForClasses:classes options:options]) {
        NSArray* pbis = [pasteboard pasteboardItems]; 
        if(pbis.count == 1) {
            
            NSString* string = [[[pbis objectAtIndex:0] stringForType:@"public.utf8-plain-text"] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            
            NSArray* lines = [string componentsSeparatedByCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@";\r\n"]];
            
            for (NSString* line in lines)
            {
                NSArray* numbers = [line componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                if(numbers.count > 0 && numbers.count < 10 && line.length > 0)
                {
                    CanMsg* msg = malloc(sizeof(CanMsg));
                    bzero(msg, sizeof(CanMsg));
                    msg->len = (UInt8) numbers.count-1;
                    msg->loc = 1;
                    for(int i = 0; i < numbers.count; i++)
                    {
                        unsigned value;
                        [[NSScanner scannerWithString:[numbers objectAtIndex:i]] scanHexInt:&value];
                        if(i == 0) {
                            if(value & 0x40000000) {
                                msg->rtr = 1;
                            }
                            if(value & 0x80000000) {
                                msg->ext = 1; // set extended flag
                                msg->canid.ul = value & 0x1fffffff; // CAN2.0B 29 bit identfier
                            }
                            else {
                                msg->canid.ul = value & 0x7ff; // CAN2.0A 11 bit identifier
                            }
                        } else {
                            msg->data[i-1] = (UInt8) value & 0xff;
                        }
                    }
                    gettimeofday(&msg->ts, NULL);
                    PeakSend(msg);
                    [self appendMsg:msg];
                }
            }
        }
    }
}

- (void)appendMsg:(CanMsg*)msg
{
    LogLine *logLine = [[LogLine alloc] initWithMessage:msg];
    
    if(!arrayController.filterPredicate || (arrayController.filterPredicate && [arrayController.filterPredicate evaluateWithObject:logLine])) {
        [arrayController addObject:logLine];
    }
    
    if([[arrayController arrangedObjects] count] > arrayControllerMaxSize)
    {
        [arrayController removeObjectAtArrangedObjectIndex:0];
    }
}

void notificationCallback (CFNotificationCenterRef center, void *observer, CFStringRef name, const void *object, CFDictionaryRef userInfo)
{
    AppDelegate* refToSelf = (__bridge AppDelegate *)(observer);
    
    dispatch_async(dispatch_get_main_queue(), ^(void) {
        
        if(CFStringCompare(name, CFSTR("CanMsg"), 0) == 0) {
            
            [refToSelf appendMsg:(CanMsg*)object];
            
        }
        else if(CFStringCompare(name, CFSTR("CanDevice"), 0) == 0) {
            if(object) {
                refToSelf.statusText.title = [NSString stringWithFormat:@"%d/sec", *(int*)object];
            } else {
                refToSelf.statusText.title = @"No device";
            }
        }
        else if(CFStringCompare(name, CFSTR("CanStatus"), 0) == 0) {
            //NSLog(@"%@", name);
        }
        
    });
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    NSArray* bitrates = [[NSArray alloc] initWithObjects:@"1 MBit/s", @"500 kBit/s", @"250 kBit/s",
                @"125 kBit/s", @"100 kBit/s", @"50 kBit/s", @"20 kBit/s", @"10 kBit/s", @"5 kBit/s", nil];
    [bitratePopup removeAllItems];
    [bitratePopup addItemsWithTitles:bitrates];
    [bitratePopup selectItemAtIndex:3]; // default 125kHz
    // bitrates above 125kHz are untested so disable them for now
    [[bitratePopup itemAtIndex:0] setEnabled:NO];
    [[bitratePopup itemAtIndex:1] setEnabled:NO];
    [[bitratePopup itemAtIndex:2] setEnabled:NO];
    [bitratePopup setAutoenablesItems:NO];
    
    [arrayController setClearsFilterPredicateOnInsertion:NO];
    arrayControllerMaxSize = 1000;
    [arrayController setFilterPredicate:[NSPredicate predicateWithFormat:@"length >= 0 OR canid >= 0"]];
    
    CFNotificationCenterAddObserver(CFNotificationCenterGetLocalCenter(), (__bridge const void *)(self), notificationCallback, NULL, NULL, CFNotificationSuspensionBehaviorHold);
        
    dispatch_async(dispatch_queue_create("PeakUSBDriver", NULL), ^(void) {
        PeakStart();
    });
}

- (void)applicationWillTerminate:(NSNotification *)notification
{
    PeakStop();
    [[NSApplication sharedApplication] terminate:self];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    return TRUE;
}

@end
