import Foundation
import UIKit
import GhosttyTerminal

private final class IXLandGhosttyTerminalContainerView: UIView {
    let terminalView: GhosttyTerminal.TerminalView
    private var focusTapRecognizer: UITapGestureRecognizer?

    init(terminalView: GhosttyTerminal.TerminalView) {
        self.terminalView = terminalView
        super.init(frame: .zero)
        backgroundColor = UIColor(red: 0x21 / 255.0, green: 0x21 / 255.0, blue: 0x21 / 255.0, alpha: 1.0)
        isOpaque = true
        terminalView.translatesAutoresizingMaskIntoConstraints = false
        addSubview(terminalView)
        NSLayoutConstraint.activate([
            terminalView.topAnchor.constraint(equalTo: topAnchor),
            terminalView.leadingAnchor.constraint(equalTo: leadingAnchor),
            terminalView.trailingAnchor.constraint(equalTo: trailingAnchor),
            terminalView.bottomAnchor.constraint(equalTo: keyboardLayoutGuide.topAnchor),
        ])

        let focusTapRecognizer = UITapGestureRecognizer(target: self, action: #selector(refocusTerminal))
        focusTapRecognizer.cancelsTouchesInView = false
        focusTapRecognizer.delaysTouchesBegan = false
        focusTapRecognizer.delaysTouchesEnded = false
        addGestureRecognizer(focusTapRecognizer)
        self.focusTapRecognizer = focusTapRecognizer
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    @objc private func refocusTerminal() {
        DispatchQueue.main.async { [weak self] in
            guard let self, window != nil else {
                return
            }
            var responder: UIResponder? = self
            while let current = responder {
                if current.canBecomeFirstResponder {
                    _ = current.becomeFirstResponder()
                    current.reloadInputViews()
                    return
                }
                responder = current.next
            }
        }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        terminalView.fitToSize()
    }

    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        guard let hitView = super.hitTest(point, with: event) else {
            return nil
        }
        if hitView === terminalView || hitView.isDescendant(of: terminalView) {
            return self
        }
        return hitView
    }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        terminalView.fitToSize()
    }
}

@objc public protocol IXLandGhosttyHostTerminalDelegate: AnyObject {
    func ghosttyHostTerminal(_ terminal: IXLandGhosttyHostTerminal, didReceiveInput data: Data)
    @objc(ghosttyHostTerminal:didResizeColumns:rows:)
    func ghosttyHostTerminal(_ terminal: IXLandGhosttyHostTerminal, didResize columns: Int, rows: Int)
}

public final class IXLandGhosttyHostTerminal: NSObject {
    @objc public private(set) var view: UIView!
    @objc public private(set) var terminalView: GhosttyTerminal.TerminalView!
    private var session: InMemoryTerminalSession!
    private var isReceivingOutput = false

    @objc public weak var delegate: (any IXLandGhosttyHostTerminalDelegate)?

    private var controller: TerminalController!

    @MainActor
    @objc public init(fontSize: Double = 14.0) {
        super.init()
        controller = TerminalController(
            theme: TerminalTheme(light: .afterglow, dark: .afterglow)
        )
        controller.setColorScheme(.dark)

        let session = InMemoryTerminalSession(write: { [weak self] data in
            guard let self else { return }
            guard !self.isReceivingOutput else { return }
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

        let terminalView = GhosttyTerminal.TerminalView(frame: .zero)
        terminalView.controller = controller
        terminalView.configuration = options
        terminalView.backgroundColor = .clear
        terminalView.isOpaque = false

        self.terminalView = terminalView
        self.view = IXLandGhosttyTerminalContainerView(terminalView: terminalView)
    }

    @objc public func receiveOutput(_ data: Data) {
        isReceivingOutput = true
        defer { isReceivingOutput = false }
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

    @MainActor
    @objc public func focus() -> Bool {
        guard terminalView.window != nil else {
            return false
        }
        let focused = view.becomeFirstResponder()
        view.reloadInputViews()
        return focused
    }

    @MainActor
    @objc public func updateFontSize(_ fontSize: Double) {
        terminalView.configuration = TerminalSurfaceOptions(
            backend: .inMemory(session),
            fontSize: Float(fontSize),
            context: terminalView.configuration.context
        )
        terminalView.fitToSize()
    }
}
