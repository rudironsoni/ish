//
//  ISHInstrumentation.m
//  iSH
//
//  Minimal instrumentation implementation
//

#import "ISHInstrumentation.h"

@implementation ISHInstrumentation

+ (void)bootstrap {
    // Minimal bootstrap - no filesystem I/O
}

+ (void)activate {
}

+ (BOOL)isActive {
    return YES;
}

+ (void)recordEvent:(ISHInstrumentationEvent)event {
    (void)event;
}

+ (void)beginInterval:(NSString *)name attributes:(NSDictionary *)attributes {
    (void)name;
    (void)attributes;
}

+ (void)endInterval:(NSString *)name attributes:(NSDictionary *)attributes {
    (void)name;
    (void)attributes;
}

@end
