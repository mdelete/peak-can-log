//
//  LogLine.m
//  PeakLog
//
//  Created by Marc Delling on 03.12.12.
//

#import "LogLine.h"

@implementation LogLine
{
    CanMsg* _msg;
}

- (id)init
{
    @throw [NSException exceptionWithName:NSInternalInconsistencyException
                                   reason:@"-init is not a valid initializer for the class LogLine"
                                 userInfo:nil];
    return nil;
}

- (id)initWithStatus:(CanMsg*)msg
{
    self = [super init];
    if(self) {
        _msg = msg;
    }
    return self;
}

- (id)initWithMessage:(CanMsg*)msg
{
    self = [super init];
    if(self) {
        _msg = msg;
        NSMutableString* flags = [[NSMutableString alloc] init];
        if(msg->err)
            [flags appendString:@"|Err"];
        if(msg->rtr)
            [flags appendString:@"|Rtr"];
        if(msg->ext)
            [flags appendString:@"|Ext"];
        else
            [flags appendString:@"|Basic"];
        
        NSMutableString* data = [[NSMutableString alloc] init];
        for(int i = 0; i < msg->len; i++)
            [data appendFormat:@" 0x%02x", msg->data[i]];
        
        self.timestamp = [NSString stringWithFormat:@"%06lu.%06u", msg->ts.tv_sec, msg->ts.tv_usec];
        self.flags = [flags substringFromIndex:1];
        self.canid = [NSString stringWithFormat:@"0x%03x (%d)", msg->canid.ul, msg->canid.ul];
        self.length = [NSNumber numberWithInt:msg->len];
        self.data = data;
        if(msg->loc)
            self.datadescr = @"Pasted";
        else
            self.datadescr = @"";
    }
    return self;
}

- (void)dealloc {
    free(_msg);
}

@end
