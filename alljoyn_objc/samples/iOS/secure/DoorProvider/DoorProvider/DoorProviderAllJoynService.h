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

#import <Foundation/Foundation.h>
#import "alljoyn/Status.h"

typedef enum {
    STARTED,
    STOPED,
    UNDEFINED
} ServiceState;

@protocol AllJoynStatusMessageListener

@required
- (void)didReceiveAllJoynStatusMessage:(NSString *)message;

@end

@interface DoorProviderAllJoynService : NSObject

@property (nonatomic, readonly) ServiceState serviceState;
@property (nonatomic, readonly) NSString *appName;

- (id)init;
- (id)initWithMessageListener:(id<AllJoynStatusMessageListener>)messageListener;
- (QStatus)startWithName:(NSString *)appName;
- (QStatus)stop;
- (void)sendDoorEvent;
- (QStatus)toggleAutoSignal;

@end
