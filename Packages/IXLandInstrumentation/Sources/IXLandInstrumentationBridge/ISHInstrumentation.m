//
//  ISHInstrumentation.m
//  iSH
//
//  App-owned instrumentation implementation
//

#import "ISHInstrumentation.h"
#import "ISHInstrumentationSinkApple.h"

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

+ (void)recordEvent:(NSString *)eventName {
    [[self sharedInstrumentation] recordEventInternal:eventName attributes:nil];
}

+ (void)recordEvent:(NSString *)eventName attributes:(NSDictionary *)attributes {
    [[self sharedInstrumentation] recordEventInternal:eventName attributes:attributes];
}

#pragma mark - Interval Tracking

+ (uint64_t)beginInterval:(NSString *)name attributes:(NSDictionary *)attributes {
    return [[self sharedInstrumentation] beginIntervalInternal:name attributes:attributes];
}

+ (void)endInterval:(uint64_t)intervalId attributes:(NSDictionary *)attributes {
    [[self sharedInstrumentation] endIntervalInternal:intervalId attributes:attributes];
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

    // Initialize the concrete Apple sink
    [ISHInstrumentationSinkApple setup];

    self.bootstrapped = YES;
}

- (void)performActivate {
    if (!self.bootstrapped) {
        [self performBootstrap];
    }

    if (self.activated) {
        return;
    }

    // Activate the concrete Apple sink
    [ISHInstrumentationSinkApple activate];

    self.activated = YES;
}

- (void)recordEventInternal:(NSString *)eventName attributes:(NSDictionary *)attributes {
    if (!self.activated) {
        return;
    }

    if (!eventName || eventName.length == 0) {
        return;
    }

    // Forward to the concrete Apple sink with string-based event name
    [ISHInstrumentationSinkApple recordEvent:eventName attributes:attributes];
}

- (uint64_t)beginIntervalInternal:(NSString *)name attributes:(NSDictionary *)attributes {
    if (!self.activated) {
        return 0;
    }

    if (!name || name.length == 0) {
        return 0;
    }

    // Forward to the concrete Apple sink
    return [ISHInstrumentationSinkApple beginInterval:name attributes:attributes];
}

- (void)endIntervalInternal:(uint64_t)intervalId attributes:(NSDictionary *)attributes {
    if (!self.activated) {
        return;
    }

    if (intervalId == 0) {
        return;
    }

    // Forward to the concrete Apple sink with proper interval ID
    [ISHInstrumentationSinkApple endInterval:intervalId attributes:attributes];
}

@end
