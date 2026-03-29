//
//  ISHInstrumentationEvents.h
//  iSH
//
//  Canonical semantic string names for instrumentation events.
//  Uses namespaced dotted semantics for event identification.
//

#ifndef ISHInstrumentationEvents_h
#define ISHInstrumentationEvents_h

/*
 * App lifecycle events - app.* namespace
 * These events track the application lifecycle from launch to scene connection.
 */

/** App bootstrap completed successfully */
#define ISH_EVENT_APP_BOOTSTRAP_READY       "app.bootstrap.ready"

/** App launch began - earliest point in app startup */
#define ISH_EVENT_APP_LAUNCH_BEGAN          "app.launch.began"

/** App launch completed successfully */
#define ISH_EVENT_APP_LAUNCH_READY          "app.launch.ready"

/** UI scene connected - app is visible to user */
#define ISH_EVENT_APP_SCENE_CONNECTED       "app.scene.connected"

/*
 * Session events - session.* namespace
 * These events track session lifecycle for Linux/guest execution.
 */

/** Session started - guest environment initialization began */
#define ISH_EVENT_SESSION_STARTED           "session.started"

/** Session ready - guest environment fully initialized */
#define ISH_EVENT_SESSION_READY             "session.ready"

/** Session bootstrap deferred - Task Zero mode, guest startup skipped */
#define ISH_EVENT_SESSION_BOOTSTRAP_DEFERRED "session.bootstrap.deferred"

/*
 * Task events - task.* namespace
 * These events track task creation and execution.
 */

/** Task created - new task structure allocated */
#define ISH_EVENT_TASK_CREATED              "task.created"

/** Task started - task began execution */
#define ISH_EVENT_TASK_STARTED              "task.started"

/*
 * Execution events - exec.* namespace
 * These events track program execution lifecycle.
 */

/** Exec began - execve() system call initiated */
#define ISH_EVENT_EXEC_BEGAN                "exec.began"

/** Exec memory management updated - MM state changed during exec */
#define ISH_EVENT_EXEC_MM_UPDATED           "exec.mm.updated"

/*
 * Emulator events - emulator.* namespace
 * These events track emulator state changes.
 */

/** Emulator started - x86 emulation layer initialized */
#define ISH_EVENT_EMULATOR_STARTED          "emulator.started"

/*
 * TCTI events - tcti.* namespace
 * These events track TCTI (Task Creation and Transformation Interface) operations.
 */

/** TCTI dispatch began - TCTI transformation started */
#define ISH_EVENT_TCTI_DISPATCH_BEGAN       "tcti.dispatch.began"

/*
 * Recovery and error events
 * These events track error conditions and recovery attempts.
 */

/** Fatal boundary crossed - unrecoverable error detected */
#define ISH_EVENT_FATAL_BOUNDARY            "fatal.boundary"

/** Recovery detected - automatic recovery mechanism triggered */
#define ISH_EVENT_RECOVERY_DETECTED         "recovery.detected"

#endif /* ISHInstrumentationEvents_h */
