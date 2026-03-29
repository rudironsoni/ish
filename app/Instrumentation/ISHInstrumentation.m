//
//  ISHInstrumentation.m
//  iSH
//
//  App-owned instrumentation implementation
//

#import "ISHInstrumentation.h"

// Forward declare the SinkApple class for weak linking
// The actual ISHInstrumentationSinkApple class is defined in a separate file
// and will be linked when available via task5-project-wiring
@interface ISHInstrumentationSinkApple : NSObject
+ (void)setup;
+ (void)activate;
+ (void)recordEvent:(ISHInstrumentationEvent)event;
+ (void)beginInterval:(NSString *)name attributes:(NSDictionary *)attributes;
+ (void)endInterval:(NSString *)name attributes:(NSDictionary *)attributes;
@end

// Private interface for internal state
@interface ISHInstrumentation ()
@property (nonatomic, assign, getter=isBootstrapped) BOOL bootstrapped;
@property (nonatomic, assign, getter=isActivated) BOOL activated;
@end

@implementation ISHInstrumentation

#pragma mark - Singleton

+ (instancetype)sharedInstrumentation {
    static ISHInstrumentation *sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[self alloc] init];
    });
    return sharedInstance;
}

#pragma mark - Lifecycle

+ (void)bootstrap {
    [[self sharedInstrumentation] performBootstrap];
}

+ (void)activate {
    [[self sharedInstrumentation] performActivate];
}

+ (BOOL)isActive {
    return [[self sharedInstrumentation] isActivated];
}

#pragma mark - Event Recording

+ (void)recordEvent:(ISHInstrumentationEvent)event {
    [[self sharedInstrumentation] recordEventInternal:event];
}

+ (void)beginInterval:(NSString *)name attributes:(NSDictionary *)attributes {
    [[self sharedInstrumentation] beginIntervalInternal:name attributes:attributes];
}

+ (void)endInterval:(NSString *)name attributes:(NSDictionary *)attributes {
    [[self sharedInstrumentation] endIntervalInternal:name attributes:attributes];
}

#pragma mark - Instance Methods

- (instancetype)init {
    self = [super init];
    if (self) {
        _bootstrapped = NO;
        _activated = NO;
    }
    return self;
}

- (void)performBootstrap {
    if (self.bootstrapped) {
        return;
    }

    // Initialize sinks without filesystem I/O, path probing, or recovery
    // Use NSClassFromString to avoid hard dependency until SinkApple is linked
    Class sinkClass = NSClassFromString(@"ISHInstrumentationSinkApple");
    if (sinkClass && [sinkClass respondsToSelector:@selector(setup)]) {
        [sinkClass setup];
    }

    self.bootstrapped = YES;
}

- (void)performActivate {
    if (!self.bootstrapped) {
        [self performBootstrap];
    }

    if (self.activated) {
        return;
    }

    // Activate sinks
    Class sinkClass = NSClassFromString(@"ISHInstrumentationSinkApple");
    if (sinkClass && [sinkClass respondsToSelector:@selector(activate)]) {
        [sinkClass activate];
    }

    // TODO: Activate OpenTelemetry bridge when available

    self.activated = YES;
}

- (void)recordEventInternal:(ISHInstrumentationEvent)event {
    if (!self.activated) {
        return;
    }

    // Forward to Apple sink using runtime lookup
    Class sinkClass = NSClassFromString(@"ISHInstrumentationSinkApple");
    if (sinkClass && [sinkClass respondsToSelector:@selector(recordEvent:)]) {
        [sinkClass recordEvent:event];
    }

    // TODO: Forward to OpenTelemetry bridge when available
}

- (void)beginIntervalInternal:(NSString *)name attributes:(NSDictionary *)attributes {
    if (!self.activated) {
        return;
    }

    if (name.length == 0) {
        return;
    }

    // Forward to Apple sink using runtime lookup
    Class sinkClass = NSClassFromString(@"ISHInstrumentationSinkApple");
    if (sinkClass && [sinkClass respondsToSelector:@selector(beginInterval:attributes:)]) {
        [sinkClass beginInterval:name attributes:attributes];
    }

    // TODO: Forward to OpenTelemetry bridge when available
}

- (void)endIntervalInternal:(NSString *)name attributes:(NSDictionary *)attributes {
    if (!self.activated) {
        return;
    }

    // Forward to Apple sink using runtime lookup
    Class sinkClass = NSClassFromString(@"ISHInstrumentationSinkApple");
    if (sinkClass && [sinkClass respondsToSelector:@selector(endInterval:attributes:)]) {
        [sinkClass endInterval:name attributes:attributes];
    }

    // TODO: Forward to OpenTelemetry bridge when available
}

@end
