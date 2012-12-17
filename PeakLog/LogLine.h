//
//  LogLine.h
//  PeakLog
//
//  Copyright © 2012 by Marc Delling on 03.12.12.
//

#import <Foundation/Foundation.h>

#include "PeakUSB.h"

#pragma mark - Formatter classes

@interface TimestampFormatter : NSFormatter
@end

@interface CanidFormatter : NSFormatter
@end

#pragma mark - CanopenPredicateEditorRowTemplate

@interface CanopenPredicateEditorRowTemplate : NSPredicateEditorRowTemplate
{
    NSPopUpButton *keypathPopUp;
	NSPopUpButton *typePopUp;
}

-(NSPopUpButton *)keypathPopUp;
-(NSPopUpButton *)typePopUp;

@end

#pragma mark - The log message

@interface LogLine : NSObject

@property (readonly) NSNumber *timestamp;
@property (readonly) NSString *flags;
@property (readonly) NSNumber *canid;
@property (readonly) NSNumber *length;
@property (readonly) NSString *data;
@property (readonly) NSString *datadescr;

- (id)initWithMessage:(CanMsg*)msg;

@end
