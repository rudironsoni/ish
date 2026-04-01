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
    ISHInstrumentationEventShutdownClean,
    // Proof point events
    ISHInstrumentationEventTaskProofStartEnter,
    ISHInstrumentationEventTaskProofBeforePthread,
    ISHInstrumentationEventTaskProofAfterPthread,
    ISHInstrumentationEventTaskProofThreadEntry,
    ISHInstrumentationEventTaskProofBeforeCurrentSet,
    ISHInstrumentationEventTaskProofAfterCurrentSet,
    ISHInstrumentationEventTaskProofRunCurrentEnter,
    ISHInstrumentationEventTaskProofBeforeGuestCpu,
    // Paired diagnostic proof points for narrowing failure boundary
    ISHInstrumentationEventTaskProofAfterThreadEntry,
    ISHInstrumentationEventTaskProofBeforeTaskRunCurrent,
    ISHInstrumentationEventTaskProofTaskRunCurrentEntry,
    // APPSIM-004 Stage 1: /bin/login exec verification
    ISHInstrumentationEventLoginExecEntry,
    ISHInstrumentationEventLoginExecSuccess,
    ISHInstrumentationEventLoginExecFailure,
    ISHInstrumentationEventLoginPidAlive,
    // APPSIM-004 Stage 2: PTY byte detection
    ISHInstrumentationEventPtyMasterWrite,
    ISHInstrumentationEventPtySlaveWrite,
    ISHInstrumentationEventPtyMasterRead,
    ISHInstrumentationEventTerminalOutputQueued,
    ISHInstrumentationEventTerminalRefreshTriggered,
    // APPSIM-004 Stage 3A: Guest exec continuity and first output
    ISHInstrumentationEventGuestExecTarget,
    ISHInstrumentationEventGuestExecSuccess,
    ISHInstrumentationEventGuestPidAliveAfterExec,
    ISHInstrumentationEventGuestWriteAttempt,
    ISHInstrumentationEventGuestIoctlAttempt,
    ISHInstrumentationEventGuestReadAttempt,
    // APPSIM-004 Stage 3B: stdio wiring proof
    ISHInstrumentationEventStdioFd0Target,
    ISHInstrumentationEventStdioFd1Target,
    ISHInstrumentationEventStdioFd2Target,
    ISHInstrumentationEventStdioPtySlaveBound,
 ISHInstrumentationEventStdioTtySessionState,
    // TCTI trace events
    ISHInstrumentationEventTctiEntryX28Before,
    ISHInstrumentationEventTctiEntryQword0,
    ISHInstrumentationEventTctiEntryX27After,
    ISHInstrumentationEventTctiEntryX28After,
    ISHInstrumentationEventTctiEntryQword1,
    ISHInstrumentationEventGadgetEntryX28,
    ISHInstrumentationEventGadgetFaultAddr,
    // Keyboard input path instrumentation (APPSIM-003)
    ISHInstrumentationEventTerminalSendInputEnter,
    ISHInstrumentationEventTerminalBeforeTtyInput,
    ISHInstrumentationEventTtyInputEntry,
    ISHInstrumentationEventTerminalAfterTtyInput,
};

@interface ISHInstrumentation : NSObject

+ (void)bootstrap;
+ (void)activate;
+ (BOOL)isActive;
+ (void)recordEvent:(ISHInstrumentationEvent)event;
+ (void)beginInterval:(NSString *)name attributes:(NSDictionary *)attributes;
+ (void)endInterval:(NSString *)name attributes:(NSDictionary *)attributes;

@end
