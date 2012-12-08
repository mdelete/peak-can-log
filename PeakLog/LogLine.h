//
//  LogLine.h
//  PeakLog
//
//  Created by Marc Delling on 03.12.12.
//

#import <Foundation/Foundation.h>

#include "PeakUSB.h"

@interface LogLine : NSObject

@property (retain) NSString *timestamp;
@property (retain) NSString *flags;
@property (retain) NSString *canid;
@property (retain) NSNumber *length;
@property (retain) NSString *data;
@property (retain) NSString *datadescr;

- (id)initWithMessage:(CanMsg*)msg;

@end
