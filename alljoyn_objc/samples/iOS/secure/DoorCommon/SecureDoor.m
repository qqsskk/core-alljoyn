////////////////////////////////////////////////////////////////////////////////
// Copyright AllSeen Alliance. All rights reserved.
//
//    Permission to use, copy, modify, and/or distribute this software for any
//    purpose with or without fee is hereby granted, provided that the above
//    copyright notice and this permission notice appear in all copies.
//
//    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
//    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
//    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
//    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
//    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
//    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
//    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
//  ALLJOYN MODELING TOOL - GENERATED CODE
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
//  SecureDoor.m
//
////////////////////////////////////////////////////////////////////////////////

#import "SecureDoor.h"

////////////////////////////////////////////////////////////////////////////////
//
//  Objective-C Bus Object implementation for Door
//
////////////////////////////////////////////////////////////////////////////////

@interface Door ()

@property (nonatomic, weak) id<AllJoynStatusMessageListener> messageListener;

@end

@implementation Door

- (id)initWithPath:(NSString *)path
{
    self = [super initWithPath:path];
    if (self) {
        _State = NO;
    }
    return self;
}

- (id)initWithBusAttachment:(AJNBusAttachment *)busAttachment onPath:(NSString *)path andMessageListener:(id<AllJoynStatusMessageListener>) messageListener
{
    self = [super initWithBusAttachment:busAttachment onPath:path];
    if (self) {
        _State = NO;
        _messageListener = messageListener;
    }
    return self;
}

- (BOOL)open:(AJNMessage *)methodCallMessage
{
    [_messageListener didReceiveAllJoynStatusMessage:@"recive open door \n"];
    _State = YES;
    return YES;
}

- (BOOL)close:(AJNMessage *)methodCallMessage
{
    [_messageListener didReceiveAllJoynStatusMessage:@"recive close door \n"];
    _State = NO;
    return NO;
}

- (BOOL)getState:(AJNMessage *)methodCallMessage
{
    [_messageListener didReceiveAllJoynStatusMessage:@"recive getState \n"];
    [_messageListener didReceiveAllJoynStatusMessage:[NSString stringWithFormat: @"state is %i", _State]];
    return _State;
}

@end

////////////////////////////////////////////////////////////////////////////////
