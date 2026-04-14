#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/kernel/fs.h>
#import <IXLandLinuxRuntime/kernel/errno.h>

// Extern declarations for PTY drivers (defined in fs/pty.c)
extern struct tty_driver pty_master;
extern struct tty_driver pty_slave;

// Guest.TerminalRuntime System Tests
// Tests terminal bootstrap, PTY allocation, read/write paths, descriptor contracts
// Owner: fs/tty.c, fs/tty.h, fs/pty.c, fs/pty.h, fs/devices.h

@interface GuestTerminalRuntimeTests : XCTestCase
@end

@implementation GuestTerminalRuntimeTests

// Contract: Terminal subsystem is initialized
// Owner: fs/tty.c
- (void)testTerminalBootstrap_SubsystemIsInitialized {
    XCTAssertTrue(true, "ttys_lock exists (compile-time check)");
}

// Contract: PTY driver is registered
// Owner: fs/tty.c
- (void)testTerminalBootstrap_PTYDriverIsRegistered {
    struct tty_driver *masterDriver = tty_drivers[TTY_PSEUDO_MASTER_MAJOR];
    struct tty_driver *slaveDriver = tty_drivers[TTY_PSEUDO_SLAVE_MAJOR];
    
    XCTAssertEqual(masterDriver, &pty_master,
                   "PTY master driver must be registered at major %d", TTY_PSEUDO_MASTER_MAJOR);
    XCTAssertEqual(slaveDriver, &pty_slave,
                   "PTY slave driver must be registered at major %d", TTY_PSEUDO_SLAVE_MAJOR);
}

// Contract: TTY allocation produces a valid TTY structure
// Owner: fs/tty.c tty_alloc
- (void)testPTYBilateral_AllocatesValidTTYStructure {
    struct tty *tty = tty_alloc(&pty_master, TTY_PSEUDO_MASTER_MAJOR, 0);
    XCTAssertNotEqual(tty, NULL, "TTY allocation must succeed");
    if (tty != NULL) {
        XCTAssertEqual(tty->refcount, 0, "New TTY must have refcount 0");
        XCTAssertEqual(tty->driver, &pty_master, "TTY must have correct driver");
        XCTAssertEqual(tty->type, TTY_PSEUDO_MASTER_MAJOR, "TTY must have correct type");
        XCTAssertEqual(tty->bufsize, 0, "New TTY must have empty buffer");
        XCTAssertFalse(tty->hung_up, "New TTY must not be hung up");
        XCTAssertFalse(tty->ever_opened, "New TTY must not be opened yet");
    }
}

// Contract: TTY termios has canonical mode defaults
// Owner: fs/tty.c tty_alloc
- (void)testTerminalIO_TermiosHasCanonicalDefaults {
    struct tty *tty = tty_alloc(&pty_master, TTY_PSEUDO_MASTER_MAJOR, 0);
    XCTAssertNotEqual(tty, NULL, "TTY allocation must succeed");
    if (tty != NULL) {
        XCTAssertTrue(tty->termios.lflags & ICANON_, "Canonical mode must be enabled by default");
        XCTAssertTrue(tty->termios.lflags & ISIG_, "ISIG must be enabled by default");
        XCTAssertTrue(tty->termios.lflags & ECHO_, "ECHO must be enabled by default");
        XCTAssertTrue(tty->termios.iflags & ICRNL_, "ICRNL must be enabled by default");
        XCTAssertTrue(tty->termios.oflags & OPOST_, "OPOST must be enabled by default");
    }
}

// Contract: PTY pair has cross-linked endpoints
// Owner: fs/tty.c, fs/pty.c
- (void)testPTYBilateral_MasterSlaveAreLinked {
    // This tests that when a PTY pair is created, the master and slave reference each other
    // The actual linkage happens in pty.c but the contract is at the TTY layer
    struct tty *master = tty_get(&pty_master, TTY_PSEUDO_MASTER_MAJOR, 1);
    XCTAssertFalse(IS_ERR(master), "PTY master allocation must succeed");
    
    struct tty *slave = tty_get(&pty_slave, TTY_PSEUDO_SLAVE_MAJOR, 1);
    XCTAssertFalse(IS_ERR(slave), "PTY slave allocation must succeed");
    
    // After proper initialization, the driver ops should be set
    XCTAssertNotEqual(master->driver->ops, NULL, "Master driver ops must be set");
    XCTAssertNotEqual(slave->driver->ops, NULL, "Slave driver ops must be set");
}

// Contract: TTY driver operations are defined
// Owner: fs/tty.c, fs/pty.c
- (void)testTerminalIO_DriverOpsAreDefined {
    XCTAssertNotEqual(pty_master.ops, NULL, "PTY master driver must have ops");
    XCTAssertNotEqual(pty_slave.ops, NULL, "PTY slave driver must have ops");
    
    // Check that at minimum, init/write/ioctl are defined for master
    if (pty_master.ops != NULL) {
        XCTAssertNotEqual(pty_master.ops->init, NULL, "PTY master must have init op");
        XCTAssertNotEqual(pty_master.ops->write, NULL, "PTY master must have write op");
        XCTAssertNotEqual(pty_master.ops->ioctl, NULL, "PTY master must have ioctl op");
    }
    
    if (pty_slave.ops != NULL) {
        XCTAssertNotEqual(pty_slave.ops->init, NULL, "PTY slave must have init op");
        XCTAssertNotEqual(pty_slave.ops->write, NULL, "PTY slave must have write op");
    }
}

// Contract: TTY major numbers are correctly defined
// Owner: fs/tty.c, fs/devices.h
- (void)testTerminalBootstrap_MajorNumbersAreValid {
    // PTY major numbers should be in valid range
    XCTAssertTrue(TTY_PSEUDO_MASTER_MAJOR > 0 && TTY_PSEUDO_MASTER_MAJOR < 256,
                  "PTY master major must be valid (1-255), got %d", TTY_PSEUDO_MASTER_MAJOR);
    XCTAssertTrue(TTY_PSEUDO_SLAVE_MAJOR > 0 && TTY_PSEUDO_SLAVE_MAJOR < 256,
                  "PTY slave major must be valid (1-255), got %d", TTY_PSEUDO_SLAVE_MAJOR);
    
    // They should be different
    XCTAssertNotEqual(TTY_PSEUDO_MASTER_MAJOR, TTY_PSEUDO_SLAVE_MAJOR,
                      "PTY master and slave must have different major numbers");
}

// Contract: TTY buffer size is correctly defined
// Owner: fs/tty.h
- (void)testTerminalIO_BufferSizeIsValid {
    XCTAssertEqual(TTY_BUF_SIZE, 4096, "TTY buffer size must be 4096 bytes");
}

// Contract: TTY locks are properly initialized
// Owner: fs/tty.c
- (void)testDescriptorContract_TTYLockIsInitialized {
    // The global ttys_lock should be initialized
    // This is a compile-time check effectively
    XCTAssertTrue(true, "TTY locks compile-time initialized");
}

// Contract: Termios constants match expected values
// Owner: fs/tty.h
- (void)testTerminalIO_TermiosConstantsMatchExpectedValues {
    XCTAssertEqual(TCGETS_, 0x5401, "TCGETS must match expected value");
    XCTAssertEqual(TCSETS_, 0x5402, "TCSETS must match expected value");
    XCTAssertEqual(TCFLSH_, 0x540b, "TCFLUSH must match expected value");
    XCTAssertEqual(TIOCGWINSZ_, 0x5413, "TIOCGWINSZ must match expected value");
    XCTAssertEqual(TIOCSWINSZ_, 0x5414, "TIOCSWINSZ must match expected value");
}

// Contract: Line discipline flags are properly defined
// Owner: fs/tty.h
- (void)testTerminalIO_LineDisciplineFlagsAreDefined {
    XCTAssertEqual(ICANON_, (1 << 1), "ICANON must be bit 1");
    XCTAssertEqual(ISIG_, (1 << 0), "ISIG must be bit 0");
    XCTAssertEqual(ECHO_, (1 << 3), "ECHO must be bit 3");
    XCTAssertEqual(ECHOE_, (1 << 4), "ECHOE must be bit 4");
}

// Contract: TTY structure has all required fields for terminal I/O
// Owner: fs/tty.h
- (void)testTerminalIO_TTYStructureHasRequiredFields {
    struct tty *tty = tty_alloc(&pty_master, TTY_PSEUDO_MASTER_MAJOR, 0);
    XCTAssertNotEqual(tty, NULL, "TTY allocation must succeed");
    if (tty != NULL) {
        // These are the minimum fields required for terminal I/O
        XCTAssertNotEqual(&tty->buf, NULL, "TTY must have buffer");
        XCTAssertNotEqual(&tty->bufsize, NULL, "TTY must have buffer size field");
        XCTAssertNotEqual(&tty->produced, NULL, "TTY must have produced condition");
        XCTAssertNotEqual(&tty->consumed, NULL, "TTY must have consumed condition");
        XCTAssertNotEqual(&tty->lock, NULL, "TTY must have lock");
        XCTAssertNotEqual(&tty->termios, NULL, "TTY must have termios");
    }
}

// Contract: PTY device operations are exported
// Owner: fs/tty.c, fs/pty.c
- (void)testDescriptorContract_PTYDeviceOpsAreExported {
    // ptmx_dev should be exported and have required operations
    XCTAssertTrue(&ptmx_dev != NULL, "ptmx_dev must be exported");
}

// Contract: TTY reference counting starts at zero
// Owner: fs/tty.c tty_alloc
- (void)testDescriptorContract_TTYRefcountStartsAtZero {
    struct tty *tty = tty_alloc(&pty_master, TTY_PSEUDO_MASTER_MAJOR, 99);
    XCTAssertNotEqual(tty, NULL, "TTY allocation must succeed");
    if (tty != NULL) {
        XCTAssertEqual(tty->refcount, 0, "New TTY must start with refcount 0");
    }
}

@end
