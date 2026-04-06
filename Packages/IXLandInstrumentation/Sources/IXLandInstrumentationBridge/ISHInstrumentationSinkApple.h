//
//  ISHInstrumentationSinkApple.h
//  iSH
//
//  Concrete Apple Unified Logging sink for ISHInstrumentation.
//  This is the single authoritative external logging sink for normal runtime observability.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/**
 * ISHInstrumentationSinkApple
 *
 * Concrete sink implementation that publishes all instrumentation events
 * to Apple Unified Logging (OSLog). This is the single authoritative
 * external sink for normal runtime observability.
 *
 * Subsystem: app.ixland.terminal
 * Categories: app, session, kernel, task, exec, emulator, tcti, terminal, crash
 */
@interface ISHInstrumentationSinkApple : NSObject

/**
 * Setup the sink. Called once during bootstrap.
 */
+ (void)setup;

/**
 * Activate the sink. Called once instrumentation should begin recording.
 */
+ (void)activate;

/**
 * Record a semantic event with optional attributes.
 *
 * @param eventName The semantic event name (e.g., "session.bootstrap.ready")
 * @param attributes Optional key-value attributes for the event
 */
+ (void)recordEvent:(NSString *)eventName attributes:(nullable NSDictionary *)attributes;

/**
 * Record an event by C string name (convenience for bridge layer).
 *
 * @param eventName The semantic event name as a C string
 */
+ (void)recordEventWithName:(const char *)eventName;

/**
 * Begin an interval and return a correlation identifier.
 *
 * @param intervalName The interval name (used for categorization)
 * @param attributes Optional initial attributes
 * @return A unique interval identifier for correlation with endInterval
 */
+ (uint64_t)beginInterval:(NSString *)intervalName attributes:(nullable NSDictionary *)attributes;

/**
 * End an interval using the correlation identifier from beginInterval.
 *
 * @param intervalId The identifier returned by beginInterval
 * @param attributes Optional final attributes (merged with begin attributes)
 */
+ (void)endInterval:(uint64_t)intervalId attributes:(nullable NSDictionary *)attributes;

@end

NS_ASSUME_NONNULL_END
