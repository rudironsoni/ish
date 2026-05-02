import Foundation
import UIKit
import GhosttyTerminal

@objc public protocol IXLandGhosttyHostTerminalDelegate: AnyObject {
    func ghosttyHostTerminal(_ terminal: IXLandGhosttyHostTerminal, didReceiveInput data: Data)
    @objc(ghosttyHostTerminal:didResizeColumns:rows:)
    func ghosttyHostTerminal(_ terminal: IXLandGhosttyHostTerminal, didResize columns: Int, rows: Int)
}

public final class IXLandGhosttyHostTerminal: NSObject, UIGestureRecognizerDelegate {
    @objc public private(set) var view: UIView!
    @objc public private(set) var terminalView: GhosttyTerminal.TerminalView!
    private var session: InMemoryTerminalSession!
    private var isReceivingOutput = false
    private var navigationButton: IXLandGhosttyNavigationButton?
    private var focusTapRecognizer: UITapGestureRecognizer?

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
            Task { @MainActor [weak self] in
                guard let self else { return }
                guard !self.isReceivingOutput else { return }
                self.delegate?.ghosttyHostTerminal(self, didReceiveInput: data)
            }
        }, resize: { [weak self] viewport in
            let columns = Int(viewport.columns)
            let rows = Int(viewport.rows)
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.delegate?.ghosttyHostTerminal(self, didResize: columns, rows: rows)
            }
        })
        self.session = session

        let options = TerminalSurfaceOptions(
            backend: .inMemory(session),
            fontSize: Float(fontSize)
        )

        let terminalView = GhosttyTerminal.TerminalView(frame: .zero)
        terminalView.controller = controller
        terminalView.configuration = options
        terminalView.backgroundColor = UIColor(red: 0x21 / 255.0, green: 0x21 / 255.0, blue: 0x21 / 255.0, alpha: 1.0)
        terminalView.isOpaque = true
        #if !targetEnvironment(macCatalyst)
        terminalView.inputAccessoryStyle = TerminalInputAccessoryStyle(
            regularBackground: UIColor(white: 0.16, alpha: 0.94),
            regularForeground: .white,
            activeBackground: UIColor(red: 0.16, green: 0.48, blue: 0.95, alpha: 1.0),
            activeForeground: .white
        )
        #endif

        self.terminalView = terminalView
        self.view = terminalView
        installFocusTapRecognizer()
        installNavigationAccessoryButton()
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
        let focused = terminalView.becomeFirstResponder()
        terminalView.reloadInputViews()
        installNavigationAccessoryButton()
        return focused
    }

    @MainActor
    private func installFocusTapRecognizer() {
        guard focusTapRecognizer == nil else {
            return
        }
        let recognizer = UITapGestureRecognizer(target: self, action: #selector(handleTerminalSurfaceTap(_:)))
        recognizer.cancelsTouchesInView = true
        recognizer.delaysTouchesEnded = true
        recognizer.delegate = self
        terminalView.addGestureRecognizer(recognizer)
        focusTapRecognizer = recognizer
    }

    @MainActor
    @objc private func handleTerminalSurfaceTap(_ recognizer: UITapGestureRecognizer) {
        guard recognizer.state == .ended else {
            return
        }
        _ = focus()
    }

    public func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
                                  shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool {
        return false
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

    @MainActor
    private func installNavigationAccessoryButton() {
        guard let accessoryView = terminalView.inputAccessoryView,
              let stackView = findAccessoryStack(in: accessoryView)
        else {
            return
        }

        let arrowLabels: Set<String> = ["Left", "Right", "Up", "Down"]
        var insertionIndex: Int?
        for (index, view) in stackView.arrangedSubviews.enumerated().reversed() {
            guard let label = view.accessibilityLabel, arrowLabels.contains(label) else {
                continue
            }
            insertionIndex = min(insertionIndex ?? index, index)
            stackView.removeArrangedSubview(view)
            view.removeFromSuperview()
        }

        let button: IXLandGhosttyNavigationButton
        if let navigationButton {
            button = navigationButton
        } else {
            button = IXLandGhosttyNavigationButton { [weak self] direction in
                self?.sendNavigationInput(direction)
            }
            navigationButton = button
        }

        guard button.superview == nil else {
            return
        }
        let index = min(insertionIndex ?? stackView.arrangedSubviews.count,
                        stackView.arrangedSubviews.count)
        stackView.insertArrangedSubview(button, at: index)
    }

    @MainActor
    private func findAccessoryStack(in view: UIView) -> UIStackView? {
        if let stack = view as? UIStackView {
            return stack
        }
        for subview in view.subviews {
            if let stack = findAccessoryStack(in: subview) {
                return stack
            }
        }
        return nil
    }

    private func sendNavigationInput(_ direction: IXLandGhosttyNavigationDirection) {
        let bytes: [UInt8]
        switch direction {
        case .up:
            bytes = [0x1B, 0x5B, 0x41]
        case .down:
            bytes = [0x1B, 0x5B, 0x42]
        case .left:
            bytes = [0x1B, 0x5B, 0x44]
        case .right:
            bytes = [0x1B, 0x5B, 0x43]
        }
        session.sendInput(Data(bytes))
    }
}

private enum IXLandGhosttyNavigationDirection {
    case up
    case down
    case left
    case right
}

private final class IXLandGhosttyNavigationButton: UIControl {
    private let handler: (IXLandGhosttyNavigationDirection) -> Void
    private let arrowLabels: [IXLandGhosttyNavigationDirection: UILabel] = [
        .up: UILabel(),
        .down: UILabel(),
        .left: UILabel(),
        .right: UILabel(),
    ]
    private var startPoint: CGPoint = .zero
    private var activeDirection: IXLandGhosttyNavigationDirection? {
        didSet {
            guard activeDirection != oldValue else { return }
            updateArrowEmphasis()
            repeatTimer?.invalidate()
            repeatTimer = nil
            guard let activeDirection else { return }
            handler(activeDirection)
            repeatTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: false) { [weak self] _ in
                guard let self, let direction = self.activeDirection else { return }
                self.handler(direction)
                self.repeatTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
                    guard let self, let direction = self.activeDirection else { return }
                    self.handler(direction)
                }
            }
        }
    }
    private var repeatTimer: Timer?

    init(handler: @escaping (IXLandGhosttyNavigationDirection) -> Void) {
        self.handler = handler
        super.init(frame: .zero)
        translatesAutoresizingMaskIntoConstraints = false
        accessibilityLabel = "Arrow Keys"
        accessibilityHint = "Drag up, down, left, or right"
        accessibilityTraits = [.button, .allowsDirectInteraction]

        layer.cornerRadius = 18
        layer.cornerCurve = .continuous
        clipsToBounds = true
        backgroundColor = UIColor(white: 0.16, alpha: 0.94)

        NSLayoutConstraint.activate([
            widthAnchor.constraint(equalToConstant: 36),
            heightAnchor.constraint(equalToConstant: 36),
        ])

        configureArrow(.up, text: "↑")
        configureArrow(.down, text: "↓")
        configureArrow(.left, text: "←")
        configureArrow(.right, text: "→")
        updateArrowEmphasis()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func configureArrow(_ direction: IXLandGhosttyNavigationDirection, text: String) {
        guard let label = arrowLabels[direction] else { return }
        label.translatesAutoresizingMaskIntoConstraints = false
        label.text = text
        label.textColor = .white
        label.font = .systemFont(ofSize: 13, weight: .semibold)
        label.textAlignment = .center
        addSubview(label)

        switch direction {
        case .up:
            NSLayoutConstraint.activate([
                label.centerXAnchor.constraint(equalTo: centerXAnchor),
                label.topAnchor.constraint(equalTo: topAnchor, constant: 1),
            ])
        case .down:
            NSLayoutConstraint.activate([
                label.centerXAnchor.constraint(equalTo: centerXAnchor),
                label.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -1),
            ])
        case .left:
            NSLayoutConstraint.activate([
                label.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 3),
                label.centerYAnchor.constraint(equalTo: centerYAnchor),
            ])
        case .right:
            NSLayoutConstraint.activate([
                label.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -3),
                label.centerYAnchor.constraint(equalTo: centerYAnchor),
            ])
        }
    }

    override var isHighlighted: Bool {
        didSet {
            backgroundColor = isHighlighted
                ? UIColor(white: 0.26, alpha: 0.96)
                : UIColor(white: 0.16, alpha: 0.94)
        }
    }

    override func beginTracking(_ touch: UITouch, with event: UIEvent?) -> Bool {
        startPoint = touch.location(in: self)
        isHighlighted = true
        activeDirection = nil
        return true
    }

    override func continueTracking(_ touch: UITouch, with event: UIEvent?) -> Bool {
        let point = touch.location(in: self)
        let delta = CGPoint(x: point.x - startPoint.x, y: point.y - startPoint.y)
        guard hypot(delta.x, delta.y) >= 18 else {
            activeDirection = nil
            return true
        }
        if abs(delta.x) > abs(delta.y) {
            activeDirection = delta.x > 0 ? .right : .left
        } else {
            activeDirection = delta.y > 0 ? .down : .up
        }
        return true
    }

    override func endTracking(_ touch: UITouch?, with event: UIEvent?) {
        stopTracking()
    }

    override func cancelTracking(with event: UIEvent?) {
        stopTracking()
    }

    private func stopTracking() {
        isHighlighted = false
        activeDirection = nil
        repeatTimer?.invalidate()
        repeatTimer = nil
    }

    private func updateArrowEmphasis() {
        for (direction, label) in arrowLabels {
            label.alpha = activeDirection == nil || activeDirection == direction ? 1.0 : 0.28
        }
    }
}
