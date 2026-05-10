import Foundation
import UIKit
import GhosttyTerminal

private func ixlandColor(from hex: String) -> UIColor? {
    guard hex.hasPrefix("#") else { return nil }
    var value: UInt64 = 0
    let scanner = Scanner(string: hex)
    scanner.currentIndex = hex.index(after: hex.startIndex)
    guard scanner.scanHexInt64(&value), scanner.currentIndex == hex.endIndex else {
        return nil
    }

    let red: UInt64
    let green: UInt64
    let blue: UInt64
    let alpha: UInt64
    switch hex.count {
    case 4:
        red = ((value & 0xF00) >> 8) * 0x11
        green = ((value & 0x0F0) >> 4) * 0x11
        blue = (value & 0x00F) * 0x11
        alpha = 0xFF
    case 5:
        red = ((value & 0xF000) >> 12) * 0x11
        green = ((value & 0x0F00) >> 8) * 0x11
        blue = ((value & 0x00F0) >> 4) * 0x11
        alpha = (value & 0x000F) * 0x11
    case 7:
        red = (value & 0xFF0000) >> 16
        green = (value & 0x00FF00) >> 8
        blue = value & 0x0000FF
        alpha = 0xFF
    case 9:
        red = (value & 0xFF000000) >> 24
        green = (value & 0x00FF0000) >> 16
        blue = (value & 0x0000FF00) >> 8
        alpha = value & 0x000000FF
    default:
        return nil
    }

    return UIColor(
        red: CGFloat(red) / 255.0,
        green: CGFloat(green) / 255.0,
        blue: CGFloat(blue) / 255.0,
        alpha: CGFloat(alpha) / 255.0
    )
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
    private var hasViewportMetrics = false
    private var navigationButton: IXLandGhosttyNavigationButton?
    private var pendingTerminalControlBytes = Data()
    private var pendingGuestOutputControlBytes = Data()
    private var pendingOutputBytes = Data()
    private var renderTickScheduled = false
    private var renderTickNeedsMetricsSync = false
    private var renderTickNeedsFollowup = false
    private var estimatedCursorRow = 1
    private var estimatedCursorColumn = 1
    private var pendingEscapeBytes = Data()

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
                let userData = self.filterTerminalControlRequests(from: data)
                if userData.isEmpty {
                    return
                }
                self.delegate?.ghosttyHostTerminal(self, didReceiveInput: userData)
            }
        }, resize: { [weak self] viewport in
            let columns = Int(viewport.columns)
            let rows = Int(viewport.rows)
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.hasViewportMetrics = columns > 0 && rows > 0
                self.flushPendingOutputIfPossible()
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
        terminalView.backgroundColor = .black
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
        installNavigationAccessoryButton()
    }

    @MainActor
    @objc public func receiveOutput(_ data: Data) {
        isReceivingOutput = true
        defer { isReceivingOutput = false }
        guard !data.isEmpty else {
            return
        }

        let filteredData = filterGuestOutputControlRequests(from: data)
        guard !filteredData.isEmpty else {
            return
        }

        if !canRenderOutput {
            updateEstimatedCursorPosition(with: filteredData)
            pendingOutputBytes.append(filteredData)
            if terminalView.window != nil {
                requestRenderTick(metricsMayBeDirty: true, needsFollowup: false)
            }
            return
        }

        updateEstimatedCursorPosition(with: filteredData)
        session.receive(filteredData)
        requestRenderTick(metricsMayBeDirty: false, needsFollowup: true)
    }

    @MainActor
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
        terminalView.isUserInteractionEnabled = true
        let focused = terminalView.becomeFirstResponder()
        terminalView.reloadInputViews()
        installNavigationAccessoryButton()
        requestRenderTick(metricsMayBeDirty: true, needsFollowup: false)
        flushPendingOutputIfPossible()
        return focused
    }

    @MainActor
    @objc public func surfaceDidLayout() {
        requestRenderTick(metricsMayBeDirty: true, needsFollowup: false)
        flushPendingOutputIfPossible()
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
    @objc public func updateAppearance(
        foregroundHex: String,
        backgroundHex: String,
        cursorHex: String?,
        paletteOverrides: [String]?,
        darkAppearance: Bool
    ) {
        let config = TerminalConfiguration { builder in
            builder.withForeground(foregroundHex)
            builder.withBackground(backgroundHex)
            if let cursorHex, !cursorHex.isEmpty {
                builder.withCursorColor(cursorHex)
            }
            for (index, color) in (paletteOverrides ?? []).enumerated() {
                builder.withPalette(index, color: color)
            }
        }
        controller.setTheme(TerminalTheme(light: config, dark: config))
        controller.setColorScheme(darkAppearance ? .dark : .light)
        terminalView.backgroundColor = ixlandColor(from: backgroundHex) ?? (darkAppearance ? .black : .white)
        terminalView.isOpaque = true
    }

    @MainActor
    private func flushPendingOutputIfPossible() {
        guard canRenderOutput, !pendingOutputBytes.isEmpty else {
            return
        }
        let bufferedOutput = pendingOutputBytes
        pendingOutputBytes.removeAll(keepingCapacity: true)
        session.receive(bufferedOutput)
        requestRenderTick(metricsMayBeDirty: true, needsFollowup: true)
    }

    @MainActor
    private var canRenderOutput: Bool {
        guard terminalView.window != nil else {
            return false
        }
        let bounds = terminalView.bounds.integral
        guard bounds.width > 0, bounds.height > 0 else {
            return false
        }
        return hasViewportMetrics
    }

    @MainActor
    private func requestRenderTick(metricsMayBeDirty: Bool, needsFollowup: Bool) {
        guard terminalView.window != nil else {
            return
        }
        renderTickNeedsMetricsSync = renderTickNeedsMetricsSync || metricsMayBeDirty
        renderTickNeedsFollowup = renderTickNeedsFollowup || needsFollowup
        if renderTickScheduled {
            return
        }
        renderTickScheduled = true
        runRenderTick()
    }

    @MainActor
    private func runRenderTick() {
        while renderTickScheduled {
            renderTickScheduled = false
            guard terminalView.window != nil else {
                return
            }
            let shouldRunFollowup = renderTickNeedsFollowup
            renderTickNeedsMetricsSync = false
            renderTickNeedsFollowup = false
            terminalView.fitToSize()
            if shouldRunFollowup {
                renderTickScheduled = true
            }
        }
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

    private func filterTerminalControlRequests(from data: Data) -> Data {
        var scanData = Data()
        scanData.append(pendingTerminalControlBytes)
        scanData.append(data)
        pendingTerminalControlBytes.removeAll(keepingCapacity: true)

        var output = Data()
        var index = scanData.startIndex

        while index < scanData.endIndex {
            let remaining = scanData[index...]
            let byte = scanData[index]
            let isCsi = byte == 0x9B
            let isEscCsi = remaining.count >= 2 && byte == 0x1B && scanData[scanData.index(after: index)] == 0x5B
            if isCsi || isEscCsi {
                var cursor = scanData.index(index, offsetBy: isCsi ? 1 : 2)
                while cursor < scanData.endIndex {
                    let finalByte = scanData[cursor]
                    if finalByte >= 0x40 && finalByte <= 0x7E {
                        break
                    }
                    cursor = scanData.index(after: cursor)
                }

                if cursor == scanData.endIndex {
                    pendingTerminalControlBytes.append(remaining)
                    break
                }

                if scanData[cursor] == 0x6E,
                   terminalControlSequence(scanData, start: index, end: cursor) == "[6n" {
                    let response = "\u{1B}[\(estimatedCursorRow);\(estimatedCursorColumn)R"
                    if let responseData = response.data(using: .utf8) {
                        delegate?.ghosttyHostTerminal(self, didReceiveInput: responseData)
                    }
                    index = scanData.index(after: cursor)
                    continue
                }
            }

            output.append(scanData[index])
            index = scanData.index(after: index)
        }

        return output
    }

    private func filterGuestOutputControlRequests(from data: Data) -> Data {
        var scanData = Data()
        scanData.append(pendingGuestOutputControlBytes)
        scanData.append(data)
        pendingGuestOutputControlBytes.removeAll(keepingCapacity: true)

        var output = Data()
        var index = scanData.startIndex

        while index < scanData.endIndex {
            let remaining = scanData[index...]
            let byte = scanData[index]
            let isCsi = byte == 0x9B
            let isEscCsi = remaining.count >= 2 && byte == 0x1B && scanData[scanData.index(after: index)] == 0x5B
            if isCsi || isEscCsi {
                var cursor = scanData.index(index, offsetBy: isCsi ? 1 : 2)
                while cursor < scanData.endIndex {
                    let finalByte = scanData[cursor]
                    if finalByte >= 0x40 && finalByte <= 0x7E {
                        break
                    }
                    cursor = scanData.index(after: cursor)
                }

                if cursor == scanData.endIndex {
                    pendingGuestOutputControlBytes.append(remaining)
                    break
                }

                if scanData[cursor] == 0x6E,
                   terminalControlSequence(scanData, start: index, end: cursor) == "[6n" {
                    let response = "\u{1B}[\(estimatedCursorRow);\(estimatedCursorColumn)R"
                    if let responseData = response.data(using: .utf8) {
                        delegate?.ghosttyHostTerminal(self, didReceiveInput: responseData)
                    }
                    index = scanData.index(after: cursor)
                    continue
                }
            }

            output.append(scanData[index])
            index = scanData.index(after: index)
        }

        return output
    }

    private func terminalControlSequence(_ data: Data, start: Data.Index, end: Data.Index) -> String {
        let payloadStart: Data.Index
        if data[start] == 0x9B {
            payloadStart = data.index(after: start)
        } else {
            payloadStart = data.index(start, offsetBy: 2)
        }
        let payload = data[payloadStart...end]
        return String(decoding: payload, as: UTF8.self)
    }

    private func updateEstimatedCursorPosition(with data: Data) {
        var scanData = Data()
        scanData.append(pendingEscapeBytes)
        scanData.append(data)
        pendingEscapeBytes.removeAll(keepingCapacity: true)

        var index = scanData.startIndex
        while index < scanData.endIndex {
            let byte = scanData[index]
            switch byte {
            case 0x0D:
                estimatedCursorColumn = 1
                index = scanData.index(after: index)
            case 0x0A:
                estimatedCursorRow += 1
                estimatedCursorColumn = 1
                index = scanData.index(after: index)
            case 0x08:
                estimatedCursorColumn = max(1, estimatedCursorColumn - 1)
                index = scanData.index(after: index)
            case 0x09:
                let nextTabStop = ((estimatedCursorColumn - 1) / 8 + 1) * 8 + 1
                estimatedCursorColumn = max(1, nextTabStop)
                index = scanData.index(after: index)
            case 0x1B:
                guard index < scanData.index(before: scanData.endIndex) else {
                    pendingEscapeBytes.append(scanData[index...])
                    return
                }
                let next = scanData[scanData.index(after: index)]
                if next == 0x5B {
                    var cursor = scanData.index(index, offsetBy: 2)
                    while cursor < scanData.endIndex {
                        let finalByte = scanData[cursor]
                        if finalByte >= 0x40 && finalByte <= 0x7E {
                            applyCSI(sequence: scanData, start: index, end: cursor)
                            index = scanData.index(after: cursor)
                            break
                        }
                        cursor = scanData.index(after: cursor)
                    }
                    if cursor == scanData.endIndex {
                        pendingEscapeBytes.append(scanData[index...])
                        return
                    }
                } else {
                    index = scanData.index(index, offsetBy: 2)
                }
            default:
                if byte >= 0x20 && byte != 0x7F {
                    estimatedCursorColumn += 1
                }
                index = scanData.index(after: index)
            }
        }
    }

    private func applyCSI(sequence data: Data, start: Data.Index, end: Data.Index) {
        let body = terminalControlSequence(data, start: start, end: end)
        guard let finalByte = body.last else {
            return
        }
        let paramsText = String(body.dropLast())
        let params = paramsText
            .split(separator: ";", omittingEmptySubsequences: false)
            .map { part -> Int in
                if part.isEmpty {
                    return 0
                }
                return Int(part) ?? 0
            }

        switch finalByte {
        case "A":
            estimatedCursorRow = max(1, estimatedCursorRow - max(1, params.first ?? 1))
        case "B", "e":
            estimatedCursorRow += max(1, params.first ?? 1)
        case "C", "a":
            estimatedCursorColumn += max(1, params.first ?? 1)
        case "D":
            estimatedCursorColumn = max(1, estimatedCursorColumn - max(1, params.first ?? 1))
        case "E":
            estimatedCursorRow += max(1, params.first ?? 1)
            estimatedCursorColumn = 1
        case "F":
            estimatedCursorRow = max(1, estimatedCursorRow - max(1, params.first ?? 1))
            estimatedCursorColumn = 1
        case "G":
            estimatedCursorColumn = max(1, params.first ?? 1)
        case "H", "f":
            estimatedCursorRow = max(1, params.first ?? 1)
            estimatedCursorColumn = max(1, params.dropFirst().first ?? 1)
        default:
            break
        }
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
