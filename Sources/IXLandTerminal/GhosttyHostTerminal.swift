import Foundation
import UIKit
import GhosttyTerminal

@objc public protocol IXLandGhosttyHostTerminalDelegate: AnyObject {
    func ghosttyHostTerminal(_ terminal: IXLandGhosttyHostTerminal, didReceiveInput data: Data)
    func ghosttyHostTerminal(_ terminal: IXLandGhosttyHostTerminal, didResize columns: Int, rows: Int)
}

@objcMembers
public final class IXLandGhosttyHostTerminal: NSObject {
    @objc public private(set) var view: UIView
    @objc public private(set) var session: InMemoryTerminalSession
    @objc public private(set) var terminalView: TerminalView

    @objc public weak var delegate: (any IXLandGhosttyHostTerminalDelegate)?

    private let controller: TerminalController

    @objc public init(fontSize: Double = 14.0) {
        controller = TerminalController()

        let session = InMemoryTerminalSession(write: { [weak self] data in
            guard let self else { return }
            self.delegate?.ghosttyHostTerminal(self, didReceiveInput: data)
        }, resize: { [weak self] viewport in
            guard let self else { return }
            self.delegate?.ghosttyHostTerminal(self, didResize: Int(viewport.columns), rows: Int(viewport.rows))
        })
        self.session = session

        let options = TerminalSurfaceOptions(
            backend: .inMemory(session),
            fontSize: Float(fontSize)
        )

        let terminalView = TerminalView()
        terminalView.controller = controller
        terminalView.configuration = options
        terminalView.backgroundColor = .clear
        terminalView.isOpaque = false

        self.terminalView = terminalView
        self.view = terminalView
        super.init()
    }

    @objc public func receiveOutput(_ data: Data) {
        session.receive(data)
    }

    @objc public func receiveOutputString(_ string: String) {
        guard let data = string.data(using: .utf8) else {
            return
        }
        receiveOutput(data)
    }

    @objc public func sendInput(_ data: Data) {
        session.sendInput(data)
    }

    @objc public func updateFontSize(_ fontSize: Double) {
        terminalView.configuration = TerminalSurfaceOptions(
            backend: .inMemory(session),
            fontSize: Float(fontSize),
            context: terminalView.configuration.context
        )
    }
}
