//
//  ISHInstrumentationSinkApple.h
//  iSH
//
//  Apple sink for os_log and signpost logging
//

#import <Foundation/Foundation.h>
#import "ISHInstrumentation.h"

NS_ASSUME_NONNULL_BEGIN

/**
 * ISHInstrumentationSinkApple owns os_log logging and signposts.
 * It does NOT own bootstrap, app lifecycle, OTel export, or MetricKit subscriber registration.
 */
@interface ISHInstrumentationSinkApple : NSObject

/**
 * Setup the Apple sink. Called during bootstrap.
 * No filesystem I/O or path probing.
 */
+ (void)setup;

/**
 * Activate the sink. Called during activation.
 */
+ (void)activate;

/**
 * Record a semantic event.
 */
+ (void)recordEvent:(ISHInstrumentationEvent)event;

/**
 * Begin a timed interval.
 */
+ (void)beginInterval:(NSString *)name attributes:(nullable NSDictionary *)attributes;

/**
 * End a timed interval.
 */
+ (void)endInterval:(NSString *)name attributes:(nullable NSDictionary *)attributes;

@end

NS_ASSUME_NONNULL_END
