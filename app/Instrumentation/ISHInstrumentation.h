//
//  ISHInstrumentation.h
//  iSH
//
//  Minimal instrumentation facade for ISH
//

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, ISHInstrumentationEvent) {
    ISHInstrumentationEventBootstrapReady,
    ISHInstrumentationEventLaunchBegan,
    ISHInstrumentationEventLaunchReady,
    ISHInstrumentationEventSceneConnected,
    ISHInstrumentationEventSessionStarted,
    ISHInstrumentationEventSessionReady,
    ISHInstrumentationEventSessionBootstrapDeferred,
    ISHInstrumentationEventSessionBootstrapReady,
    ISHInstrumentationEventSessionExecReady,
    ISHInstrumentationEventGuestThreadStart,
    ISHInstrumentationEventRecoveryDetected,
    ISHInstrumentationEventShutdownClean
};

@interface ISHInstrumentation : NSObject

+ (void)bootstrap;
+ (void)activate;
+ (BOOL)isActive;
+ (void)recordEvent:(ISHInstrumentationEvent)event;
+ (void)beginInterval:(NSString *)name attributes:(NSDictionary *)attributes;
+ (void)endInterval:(NSString *)name attributes:(NSDictionary *)attributes;

@end
