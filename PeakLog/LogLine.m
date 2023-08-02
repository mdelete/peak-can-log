//
//  LogLine.m
//  PeakLog
//
//  Copyright © 2012 by Marc Delling on 03.12.12.
//

#import "LogLine.h"

#pragma mark - TimestampFormatter class

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

#pragma mark - CanopenPredicateEditorRowTemplate

@implementation CanopenPredicateEditorRowTemplate

-(NSPopUpButton *)keypathPopUp
{
	if(!keypathPopUp)
	{
		NSMenu *keypathMenu = [[NSMenu alloc] initWithTitle:@"keypath menu"];
        
		NSMenuItem *menuItem = [[NSMenuItem alloc] initWithTitle:@"CanOpen Message Type" action:nil keyEquivalent:@""];
		[menuItem setRepresentedObject:[NSExpression expressionForKeyPath:@"canid"]];
		[menuItem setEnabled:YES];
        
		[keypathMenu addItem:menuItem];
        
		keypathPopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
		[keypathPopUp setMenu:keypathMenu];
	}
    
	return keypathPopUp;
}

-(NSPopUpButton *)typePopUp
{
	if(!typePopUp)
	{
		NSMenu *typeMenu = [[NSMenu alloc] initWithTitle:@"type menu"];
        
		NSMenuItem *nmtItem = [[NSMenuItem alloc] initWithTitle:@"NMT" action:nil keyEquivalent:@""];
		[nmtItem setEnabled:YES];
		[nmtItem setTag:0];
        [typeMenu addItem:nmtItem];
        
		NSMenuItem *emergItem = [[NSMenuItem alloc] initWithTitle:@"EMERG" action:nil keyEquivalent:@""];
		[emergItem setEnabled:YES];
		[emergItem setTag:1];
        [typeMenu addItem:emergItem];
        
        NSMenuItem *pdoItem = [[NSMenuItem alloc] initWithTitle:@"PDO" action:nil keyEquivalent:@""];
		[pdoItem setEnabled:YES];
		[pdoItem setTag:2];
        [typeMenu addItem:pdoItem];
        
        NSMenuItem *sdoItem = [[NSMenuItem alloc] initWithTitle:@"SDO" action:nil keyEquivalent:@""];
		[sdoItem setEnabled:YES];
		[sdoItem setTag:3];
        [typeMenu addItem:sdoItem];
        
        NSMenuItem *heartbeatItem = [[NSMenuItem alloc] initWithTitle:@"HEARTBEAT" action:nil keyEquivalent:@""];
		[heartbeatItem setEnabled:YES];
		[heartbeatItem setTag:4];
        [typeMenu addItem:heartbeatItem];
        
        NSMenuItem *lssItem = [[NSMenuItem alloc] initWithTitle:@"LSS" action:nil keyEquivalent:@""];
		[lssItem setEnabled:YES];
		[lssItem setTag:5];
        [typeMenu addItem:lssItem];
        
		typePopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
		[typePopUp setMenu:typeMenu];
	}
    
	return typePopUp;
}

-(NSArray *)templateViews
{
	return [NSArray arrayWithObjects:[self keypathPopUp], [self typePopUp], nil];
}

-(void) setPredicate:(NSPredicate *)predicate
{
	id rightValue = [[(NSComparisonPredicate *)predicate rightExpression] constantValue];
	if([rightValue isKindOfClass:[NSNumber class]])
		[[self typePopUp] selectItemWithTag:[rightValue integerValue]];
}

-(NSPredicate *)predicateWithSubpredicates:(NSArray *) subpredicates
{    
    int min = 0, max = 0;
    switch ([[self typePopUp] selectedTag]) {
        case 0: min = 0; max = 128; break; // MNT
        case 1: min = 129; max = 254; break; // EMERG
        case 2: min = 385; max = 1279; break; // PDO
        case 3: min = 1409; max = 1663; break; // SDO
        case 4: min = 1793; max = 1919; break; // HEARTBEAT
        case 5: min = 2020; max = 2021; break; // LSS
        default: break;
    }
    return [NSPredicate predicateWithFormat:@"canid >= %d AND canid <= %d", min, max];
}

@end

#pragma mark - CanidFormatter class

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

#pragma mark - LogLine class

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
    NSMutableString* flags = [[NSMutableString alloc] initWithCapacity:16];
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
