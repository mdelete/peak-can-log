//
//  LogLine.m
//  PeakLog
//
//  Created by Marc Delling on 03.12.12.
//

#import "LogLine.h"

#pragma mark - Formatter classes

@implementation TimestampFormatter

- (NSString *)stringForObjectValue:(id)arg
{
    if (arg != nil)
    {
        if ([arg isKindOfClass:[NSNumber class]])
        {
            long long ts = [arg longLongValue];
            return [NSString stringWithFormat:@"%06llu.%06llu", (ts >> 20), (ts & 0xFFFFF)];
        }
        else
        {
            [[NSException exceptionWithName:NSInvalidArgumentException
                                     reason:@"Unsupported datatype"
                                   userInfo:nil] raise];
        }
    }
    else
    {
        [[NSException exceptionWithName:NSInvalidArgumentException
                                 reason:@"Nil argument"
                               userInfo:nil] raise];
    }
    return nil;
}

- (BOOL)getObjectValue:(id *)obj forString:(NSString *)str errorDescription:(NSString **)err
{
    if (str)
    {
        long long ts;
        NSScanner* scanner = [NSScanner scannerWithString:str];
        if([scanner scanLongLong:&ts])
            *obj = [NSNumber numberWithLongLong:ts];
    }
    return (obj != nil);
}

@end


@implementation CanidFormatter

- (NSString *)stringForObjectValue:(id)arg
{
    if (arg != nil)
    {
        if ([arg isKindOfClass:[NSNumber class]])
        {
            int cid = [arg intValue];
            return [NSString stringWithFormat:@"0x%03x (%d)", cid, cid];
        }
        else
        {
            [[NSException exceptionWithName:NSInvalidArgumentException
                                    reason:@"Unsupported datatype"
                                  userInfo:nil] raise];
        }
    }
    else
    {
        [[NSException exceptionWithName:NSInvalidArgumentException
                                 reason:@"Nil argument"
                               userInfo:nil] raise];
    }
    return nil;
}

- (BOOL)getObjectValue:(id *)obj forString:(NSString *)str errorDescription:(NSString **)err
{
    if (str)
    {
        unsigned canid;
        NSScanner* scanner = [NSScanner scannerWithString:str];
        if([scanner scanHexInt:&canid])
            *obj = [NSNumber numberWithInt:canid];
    }
    return (obj != nil);
}

@end

#pragma mark - The log message

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

- (id)initWithMessage:(CanMsg*)msg
{
    self = [super init];
    if(self) {
        _msg = msg;
    }
    return self;
}

- (NSNumber *)timestamp
{
    long long i = _msg->ts.tv_sec << 20 | _msg->ts.tv_usec;
    return [NSNumber numberWithLongLong:i];
}

- (NSString *)data
{
    NSMutableString* data = [[NSMutableString alloc] init];
    for(int i = 0; i < _msg->len; i++)
        [data appendFormat:@" 0x%02x", _msg->data[i]];
    return data;
}

- (NSString *)datadescr
{
    if(_msg->loc)
        return @"Pasted";
    else
        return @"";
}

- (NSNumber *)canid
{
    return [NSNumber numberWithInt:_msg->canid.ul];
}

- (NSNumber *)length
{
    return [NSNumber numberWithInt:_msg->len];
}

- (NSString *)flags
{
    NSMutableString* flags = [[NSMutableString alloc] init];
    if(_msg->err)
        [flags appendString:@"|Err"];
    if(_msg->rtr)
        [flags appendString:@"|Rtr"];
    if(_msg->ext)
        [flags appendString:@"|Ext"];
    else
        [flags appendString:@"|Basic"];
    return [flags substringFromIndex:1];
}

- (void)dealloc
{
    free(_msg);
}

@end
