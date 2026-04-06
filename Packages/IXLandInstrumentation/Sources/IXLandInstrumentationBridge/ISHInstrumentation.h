//
//  ISHInstrumentation.h
//  iSH
//
//  Minimal instrumentation facade for ISH
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/**
 * ISHInstrumentation
 *
 * App-owned instrumentation facade. All semantic events and intervals
 * flow through this facade to the concrete sink(s).
 *
 * This facade uses string-based semantic event names that are forwarded
 * directly to Apple Unified Logging without lossy enum conversion.
 */
@interface ISHInstrumentation : NSObject

#pragma mark - Lifecycle

/**
 * Bootstrap the instrumentation system.
 * Called once during app startup before any events are recorded.
 */
+ (void)bootstrap;

/**
 * Activate instrumentation after bootstrap is complete.
 * Events will begin recording after this call.
 */
+ (void)activate;

/**
 * Check if instrumentation is currently active.
 */
+ (BOOL)isActive;

#pragma mark - Event Recording

/**
 * Record a semantic event by name.
 *
 * @param eventName The semantic event name (e.g., "session.bootstrap.ready")
 */
+ (void)recordEvent:(NSString *)eventName;

/**
 * Record a semantic event with attributes.
 *
 * @param eventName The semantic event name
 * @param attributes Key-value attributes for the event
 */
+ (void)recordEvent:(NSString *)eventName attributes:(nullable NSDictionary *)attributes;

#pragma mark - Interval Tracking

/**
 * Begin an interval and return a correlation identifier.
 *
 * @param name The interval name (e.g., "session.bootstrap")
 * @param attributes Optional initial attributes
 * @return A unique interval identifier for correlation
 */
+ (uint64_t)beginInterval:(NSString *)name attributes:(nullable NSDictionary *)attributes;

/**
 * End an interval using the correlation identifier.
 *
 * @param intervalId The identifier returned by beginInterval
 * @param attributes Optional final attributes
 */
+ (void)endInterval:(uint64_t)intervalId attributes:(nullable NSDictionary *)attributes;

@end

NS_ASSUME_NONNULL_END
