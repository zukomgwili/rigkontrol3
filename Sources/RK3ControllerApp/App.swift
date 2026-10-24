import AppKit
import Foundation
import RigKontrol3Core
import ServiceManagement

@main
private struct RK3ControllerMain {
    @MainActor
    static func main() {
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        let delegate = ControllerAppDelegate()
        app.delegate = delegate
        withExtendedLifetime(delegate) { app.run() }
    }
}

@MainActor
private final class ControllerAppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private var statusItem: NSStatusItem?
    private let statusMenuItem = NSMenuItem(title: "Starting…", action: nil, keyEquivalent: "")
    private let feedbackMenuItem = NSMenuItem(title: "LEDs: awaiting host state", action: nil, keyEquivalent: "")
    private let startStopItem = NSMenuItem(title: "Stop Controller", action: nil, keyEquivalent: "")
    private let loginItem = NSMenuItem(title: "Launch at Login", action: nil, keyEquivalent: "")
    private let mainStageModeItem = NSMenuItem(title: "MainStage (toggle buttons)", action: nil, keyEquivalent: "")
    private let standardModeItem = NSMenuItem(title: "Guitar Rig standalone / other hosts", action: nil, keyEquivalent: "")
    private var switchOutputMode: RigKontrolSwitchOutputMode = .pressOnly
    private var session: RigKontrolControllerSession?
    private var status: RigKontrolControllerStatus = .connecting
    private var wantsRunning = true
    private var isTerminating = false
    private var workspaceObservers: [NSObjectProtocol] = []
    private var setupWindow: NSWindow?
    private var setupText: NSTextView?
    private var presses = [Int](repeating: 0, count: 8)
    private var releases = [Int](repeating: 0, count: 8)
    private var expressionRange: ClosedRange<Int>?
    private var latestExpression: Int?
    private var latestRawExpression: Int?
    private var expressionCalibration = ExpressionCalibration()
    private var isCalibrating = false
    private var calibrationRange: ClosedRange<Int>?
    private let calibrationButton = NSButton(title: "Calibrate Pedal…", target: nil, action: nil)
    private let windowStartStopButton = NSButton(title: "Stop Controller", target: nil, action: nil)
    private let clearFeedbackButton = NSButton(title: "Clear Host State", target: nil, action: nil)
    private var hostFeedbackState = RigKontrolLEDFeedbackState()
    private var recentEvents: [String] = []
    private var eventLog: FileHandle?

    func applicationDidFinishLaunching(_ notification: Notification) {
        // LaunchServices normally reuses this process. Also guard direct executable launches.
        if let bundleID = Bundle.main.bundleIdentifier,
            let existing = NSRunningApplication.runningApplications(withBundleIdentifier: bundleID)
                .first(where: { $0.processIdentifier != ProcessInfo.processInfo.processIdentifier })
        {
            existing.activate()
            NSApp.terminate(nil)
            return
        }

        configureEventLog()
        log(["type": "backend", "name": CoreAudioUSBEventReader.backendName,
             "upstreamRevision": CoreAudioUSBEventReader.upstreamRevision])
        switchOutputMode = UserDefaults.standard.string(forKey: "switchOutputMode") == "momentary"
            ? .momentary : .pressOnly
        buildMenu()
        log(["type": "switch-mode", "mode": switchOutputMode.rawValue])
        let center = NSWorkspace.shared.notificationCenter
        workspaceObservers = [
            center.addObserver(forName: NSWorkspace.willSleepNotification, object: nil, queue: .main) {
                [weak self] _ in
                Task { @MainActor in self?.session?.suspend() }
            },
            center.addObserver(forName: NSWorkspace.didWakeNotification, object: nil, queue: .main) {
                [weak self] _ in
                Task { @MainActor in self?.session?.resume() }
            },
            center.addObserver(forName: NSWorkspace.didTerminateApplicationNotification, object: nil, queue: .main) {
                [weak self] notification in
                let application = notification.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication
                guard application?.bundleIdentifier == "com.apple.mainstage3" else { return }
                Task { @MainActor in
                    self?.session?.invalidateHostFeedback()
                    self?.log(["type": "host-stopped", "host": "MainStage"])
                }
            },
        ]
        expressionCalibration = loadCalibration()
        session = RigKontrolControllerSession(
            statusChanged: { [weak self] status in
                Task { @MainActor in self?.updateStatus(status) }
            },
            messageSent: { [weak self] message in
                Task { @MainActor in self?.record(message) }
            },
            rawExpressionChanged: { [weak self] raw in
                Task { @MainActor in self?.recordRawExpression(raw) }
            },
            expressionCalibration: expressionCalibration,
            switchOutputMode: switchOutputMode,
            ledMode: .hostFeedback,
            feedbackChanged: { [weak self] state in
                Task { @MainActor in self?.recordHostFeedback(state) }
            },
            feedbackMessageReceived: { [weak self] message in
                Task { @MainActor in self?.log(["type": "host-midi", "bytes": message.bytes.map(Int.init)]) }
            }
        )
        session?.start()
        updateLoginItem()
        if CommandLine.arguments.contains("--show-setup")
            || !UserDefaults.standard.bool(forKey: "hasShownControllerSetup")
        {
            showSetup()
            UserDefaults.standard.set(true, forKey: "hasShownControllerSetup")
        }

        // Bounded, opt-in app smoke run; regular launches have no time limit.
        if let raw = argument(after: "--smoke-test-seconds"),
            let seconds = Double(raw), seconds.isFinite, seconds > 0
        {
            DispatchQueue.main.asyncAfter(deadline: .now() + seconds) { NSApp.terminate(nil) }
        }
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        showSetup()
        return true
    }

    private func buildMenu() {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        statusItem = item
        if let button = item.button {
            button.title = " RK3"
            button.image = NSImage(systemSymbolName: "slider.vertical.3", accessibilityDescription: "Rig Kontrol 3")
            button.image?.isTemplate = true
        }
        let menu = NSMenu()
        menu.delegate = self
        statusMenuItem.isEnabled = false
        menu.addItem(statusMenuItem)
        feedbackMenuItem.isEnabled = false
        menu.addItem(feedbackMenuItem)
        menu.addItem(.separator())
        startStopItem.target = self
        startStopItem.action = #selector(toggleController)
        menu.addItem(startStopItem)
        let setupItem = NSMenuItem(title: "Setup & Test…", action: #selector(showSetup), keyEquivalent: "")
        setupItem.target = self
        menu.addItem(setupItem)
        let modeItem = NSMenuItem(title: "Control Mode", action: nil, keyEquivalent: "")
        let modeMenu = NSMenu()
        mainStageModeItem.target = self
        mainStageModeItem.action = #selector(selectMainStageMode)
        standardModeItem.target = self
        standardModeItem.action = #selector(selectStandardMode)
        modeMenu.addItem(mainStageModeItem)
        modeMenu.addItem(standardModeItem)
        modeItem.submenu = modeMenu
        menu.addItem(modeItem)
        updateControlModeItems()
        menu.addItem(.separator())
        loginItem.target = self
        loginItem.action = #selector(toggleLogin)
        menu.addItem(loginItem)
        menu.addItem(.separator())
        let quitItem = NSMenuItem(title: "Quit Rig Kontrol 3", action: #selector(quit), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)
        item.menu = menu
        // Give the setup window normal keyboard quit behavior as well as the status menu.
        let mainMenu = NSMenu()
        let applicationItem = NSMenuItem()
        let applicationMenu = NSMenu()
        let keyboardQuit = NSMenuItem(title: "Quit Rig Kontrol 3", action: #selector(quit), keyEquivalent: "q")
        keyboardQuit.target = self
        applicationMenu.addItem(keyboardQuit)
        applicationItem.submenu = applicationMenu
        mainMenu.addItem(applicationItem)
        NSApp.mainMenu = mainMenu
    }

    func menuWillOpen(_ menu: NSMenu) { updateLoginItem() }

    @objc private func selectMainStageMode() { setControlMode(.pressOnly) }
    @objc private func selectStandardMode() { setControlMode(.momentary) }

    private func setControlMode(_ mode: RigKontrolSwitchOutputMode) {
        guard switchOutputMode != mode else { return }
        switchOutputMode = mode
        UserDefaults.standard.set(mode == .pressOnly ? "pressOnly" : "momentary", forKey: "switchOutputMode")
        session?.invalidateHostFeedback()
        session?.setSwitchOutputMode(mode)
        updateControlModeItems()
        log(["type": "switch-mode", "mode": mode == .pressOnly ? "pressOnly" : "momentary"])
        refreshSetup()
    }

    private func updateControlModeItems() {
        mainStageModeItem.state = switchOutputMode == .pressOnly ? .on : .off
        standardModeItem.state = switchOutputMode == .momentary ? .on : .off
    }

    @objc private func toggleController() {
        wantsRunning.toggle()
        startStopItem.title = wantsRunning ? "Stop Controller" : "Start Controller"
        windowStartStopButton.title = startStopItem.title
        if wantsRunning { session?.start() } else { session?.stop() }
    }

    @objc private func toggleLogin() {
        do {
            if SMAppService.mainApp.status == .enabled
                || SMAppService.mainApp.status == .requiresApproval
            {
                try SMAppService.mainApp.unregister()
            } else {
                try SMAppService.mainApp.register()
            }
            updateLoginItem()
            if SMAppService.mainApp.status == .requiresApproval {
                SMAppService.openSystemSettingsLoginItems()
            }
        } catch {
            showError("Could not change launch at login", detail: error.localizedDescription)
            updateLoginItem()
        }
    }

    private func updateLoginItem() {
        switch SMAppService.mainApp.status {
        case .enabled:
            loginItem.state = .on
            loginItem.title = "Launch at Login"
        case .requiresApproval:
            loginItem.state = .mixed
            loginItem.title = "Launch at Login (Approval Required)"
        default:
            loginItem.state = .off
            loginItem.title = "Launch at Login"
        }
    }

    private func updateStatus(_ value: RigKontrolControllerStatus) {
        status = value
        calibrationButton.isEnabled = value == .connected
        if value != .connected, isCalibrating {
            isCalibrating = false
            calibrationRange = nil
            calibrationButton.title = "Calibrate Pedal…"
            log(["type": "calibration-cancelled", "reason": "connection changed"])
        }
        let text = statusText
        statusMenuItem.title = text
        statusItem?.button?.toolTip = "Rig Kontrol 3 — \(text)"
        statusItem?.button?.title = value == .connected ? " RK3 ●" : " RK3"
        log(["type": "status", "status": text])
        refreshSetup()
    }

    private func recordHostFeedback(_ state: RigKontrolLEDFeedbackState) {
        hostFeedbackState = state
        feedbackMenuItem.title = state.count == 0
            ? "LEDs: awaiting host state" : "LEDs: \(state.count)/8 host states received"
        log(["type": "host-state", "knownSwitches": Dictionary(uniqueKeysWithValues: state.knownSwitches.map { (String($0.key), $0.value) })])
        refreshSetup()
    }

    @objc private func clearHostFeedback() {
        session?.invalidateHostFeedback()
    }

    private func loadCalibration() -> ExpressionCalibration {
        let defaults = UserDefaults.standard
        guard let minimum = defaults.object(forKey: "expressionRawMinimum") as? Int,
            let maximum = defaults.object(forKey: "expressionRawMaximum") as? Int,
            minimum >= 0, maximum <= 65535, maximum - minimum >= 100
        else { return ExpressionCalibration() }
        return ExpressionCalibration.forMeasuredTravel(rawMinimum: minimum, rawMaximum: maximum)
    }

    private func recordRawExpression(_ raw: Int) {
        latestRawExpression = raw
        guard isCalibrating else { return }
        if let range = calibrationRange {
            calibrationRange = min(range.lowerBound, raw)...max(range.upperBound, raw)
        } else {
            calibrationRange = raw...raw
        }
        refreshSetup()
    }

    @objc private func toggleCalibration() {
        guard status == .connected else { return }
        if !isCalibrating {
            isCalibrating = true
            calibrationRange = latestRawExpression.map { $0...$0 }
            calibrationButton.title = "Finish Calibration"
        } else {
            guard let range = calibrationRange, range.upperBound - range.lowerBound >= 100 else {
                showError("Sweep the pedal through its full travel", detail: "Move fully heel-to-toe and back, then click Finish Calibration again.")
                return
            }
            expressionCalibration = ExpressionCalibration.forMeasuredTravel(
                rawMinimum: range.lowerBound, rawMaximum: range.upperBound)
            session?.setExpressionCalibration(expressionCalibration)
            UserDefaults.standard.set(range.lowerBound, forKey: "expressionRawMinimum")
            UserDefaults.standard.set(range.upperBound, forKey: "expressionRawMaximum")
            isCalibrating = false
            calibrationButton.title = "Calibrate Pedal…"
            expressionRange = nil
            latestExpression = nil
            log(["type": "calibration", "rawMinimum": range.lowerBound, "rawMaximum": range.upperBound,
                 "effectiveMinimum": expressionCalibration.rawMinimum, "effectiveMaximum": expressionCalibration.rawMaximum])
        }
        refreshSetup()
    }

    private var statusText: String {
        switch status {
        case .stopped: "Controller stopped"
        case .connecting: "Connecting to Rig Kontrol 3…"
        case .connected: "Connected — MIDI ready"
        case .waitingForDevice: "Waiting for Rig Kontrol 3…"
        case .sleeping: "Paused while Mac sleeps"
        case .failed(let detail): "Retrying — \(detail)"
        }
    }

    private func record(_ message: MIDIMessage) {
        guard message.bytes.count == 3 else { return }
        let controller = Int(message.bytes[1])
        let value = Int(message.bytes[2])
        let text: String
        if (20...27).contains(controller) {
            let index = controller - 20
            if value > 0 { presses[index] += 1 } else { releases[index] += 1 }
            text = "Switch \(index + 1): \(value > 0 ? "pressed (127)" : "release/reset (0)")"
        } else if controller == 11 {
            latestExpression = value
            if let range = expressionRange {
                expressionRange = min(range.lowerBound, value)...max(range.upperBound, value)
            } else {
                expressionRange = value...value
            }
            text = "Wah pedal: \(value)"
        } else {
            return
        }
        recentEvents.append(text)
        recentEvents = Array(recentEvents.suffix(12))
        log(["type": "midi", "bytes": message.bytes.map(Int.init), "event": text])
        refreshSetup()
    }

    @objc private func showSetup() {
        if setupWindow == nil {
            let window = NSWindow(
                contentRect: NSRect(x: 0, y: 0, width: 580, height: 640),
                styleMask: [.titled, .closable, .resizable, .miniaturizable],
                backing: .buffered, defer: false
            )
            window.title = "Rig Kontrol 3 — Setup & Test"
            window.minSize = NSSize(width: 480, height: 420)
            window.isReleasedWhenClosed = false
            let bounds = window.contentView!.bounds
            let scroll = NSScrollView(frame: NSRect(x: 0, y: 50, width: bounds.width, height: bounds.height - 50))
            scroll.autoresizingMask = [.width, .height]
            scroll.hasVerticalScroller = true
            let text = NSTextView(frame: scroll.bounds)
            text.isEditable = false
            text.isSelectable = true
            text.isRichText = false
            text.font = .monospacedSystemFont(ofSize: 13, weight: .regular)
            text.textContainerInset = NSSize(width: 18, height: 18)
            text.autoresizingMask = [.width]
            text.isVerticallyResizable = true
            text.isHorizontallyResizable = false
            text.textContainer?.widthTracksTextView = true
            scroll.documentView = text
            window.contentView?.addSubview(scroll)
            calibrationButton.bezelStyle = .rounded
            calibrationButton.target = self
            calibrationButton.action = #selector(toggleCalibration)
            calibrationButton.isEnabled = status == .connected
            windowStartStopButton.bezelStyle = .rounded
            windowStartStopButton.target = self
            windowStartStopButton.action = #selector(toggleController)
            clearFeedbackButton.bezelStyle = .rounded
            clearFeedbackButton.target = self
            clearFeedbackButton.action = #selector(clearHostFeedback)
            let buttons = NSStackView(views: [calibrationButton, windowStartStopButton, clearFeedbackButton])
            buttons.orientation = .horizontal
            buttons.spacing = 10
            buttons.translatesAutoresizingMaskIntoConstraints = false
            window.contentView?.addSubview(buttons)
            if let content = window.contentView {
                NSLayoutConstraint.activate([
                    buttons.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 18),
                    buttons.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -12),
                    buttons.trailingAnchor.constraint(lessThanOrEqualTo: content.trailingAnchor, constant: -18),
                ])
            }
            window.center()
            setupWindow = window
            setupText = text
        }
        refreshSetup()
        setupWindow?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    private func refreshSetup() {
        guard let text = setupText else { return }
        let range = expressionRange.map { "\($0.lowerBound)…\($0.upperBound)" } ?? "not swept yet"
        let switchLines = (0..<8).map {
            let state = hostFeedbackState.knownSwitches[$0 + 1].map { $0 ? "ON" : "off" } ?? "unknown"
            return "  \($0 + 1)      \(20 + $0)       \(presses[$0]) / \(releases[$0])       \(state)"
        }.joined(separator: "\n")
        let scrollPosition = text.enclosingScrollView?.contentView.bounds.origin
        let hostSetup: String
        if switchOutputMode == .pressOnly {
            hostSetup = """
            MAINSTAGE MODE — ONE COMMAND PER PRESS

            1. Choose your separate audio interface in MainStage.
               Keep RK3 audio unused in other apps too.
            2. Load Guitar Rig 6 FX on an audio channel.
            3. Assign screen buttons to RigKontrol3 Bridge,
               channel 16, CC20–27, Type: Single Value.
            4. Map each button to its actual effect on/off
               parameter. Keep Show Input Value OFF and MIDI Thru
               set to Do not pass through.
            5. Set Send Value to: RigKontrol3 Bridge.
               Feedback: channel 16, CC20–27, 127=ON and 0=off.
            6. Map pedal CC11 to wah. Save and verify the concert.

            Switch release sends no MIDI in this mode.
            """
        } else {
            hostSetup = """
            GUITAR RIG STANDALONE / OTHER HOSTS

            Enable RigKontrol3 Bridge as a MIDI input, channel 16.
            Switches send CC20–27: 127 on press, 0 on release.
            In Guitar Rig, learn each CC on a Macro button with
            Hold Mode off, then assign its target to effect power.
            Learn pedal CC11 on the continuous wah parameter.

            This mode requires the host's toggle configuration.
            Standalone Guitar Rig effect-state feedback has not
            been established. Unknown LED states stay dark.
            """
        }
        text.string = """
        \(statusText)
        USB backend: \(CoreAudioUSBEventReader.backendName)

        \(hostSetup)

        MIDI SENT (SINCE APP LAUNCH)

        Switch  MIDI CC  Press / reset   Host LED state
        \(switchLines)

        Wah CC11: \(latestExpression.map(String.init) ?? "—")
        Observed range: \(range) (full travel: 0…127)
        Calibrated raw travel: \(expressionCalibration.rawMinimum)…\(expressionCalibration.rawMaximum)
        \(isCalibrating ? "CALIBRATING: sweep fully heel-to-toe and back, then\nclick Finish Calibration. Raw range: \(calibrationRange.map { "\($0.lowerBound)…\($0.upperBound)" } ?? "waiting…")" : "Use Calibrate Pedal if normal travel misses 0 or 127.")

        LED state received: \(hostFeedbackState.count)/8 controls.
        Unknown states stay dark until the host reports them.
        LEDs follow host feedback, not physical presses.
        Release all switches after connecting. A switch held
        on attachment is ignored until released and pressed.
        \(switchOutputMode == .momentary ? "After a lost release, a new press may send an extra\nzero first to restore the host's switch detection." : "MainStage receives one 127 per deliberate press;\nrelease and reconnect send no switch commands.")

        After changing mappings, clear host state and refresh
        MainStage's patch feedback. Quit MainStage to clear
        cached states. Initial/patch feedback must be verified.

        TEST RECOVERY

        Unplug and reconnect USB, then test again. Also try
        sleeping and waking the Mac. Recovery must not toggle
        effects. Try reconnecting with a switch held down.

        RECENT INPUT
        \(recentEvents.isEmpty ? "Waiting for your first pedal action…" : recentEvents.joined(separator: "\n"))
        """
        if let scrollPosition {
            text.enclosingScrollView?.contentView.scroll(to: scrollPosition)
        }
    }

    @objc private func quit() { NSApp.terminate(nil) }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let session else { return .terminateNow }
        guard !isTerminating else { return .terminateLater }
        isTerminating = true
        session.shutdown()
        DispatchQueue.global().async { [self] in
            let stopped = session.waitUntilShutdown(timeout: 5)
            // AppKit can run a nested loop while awaiting a termination reply.
            // A run-loop callback also works when terminate was invoked by GCD.
            RunLoop.main.perform(inModes: [.common]) {
                MainActor.assumeIsolated {
                    self.log(["type": "shutdown", "clean": stopped])
                    NSApp.reply(toApplicationShouldTerminate: true)
                }
            }
        }
        return .terminateLater
    }

    func applicationWillTerminate(_ notification: Notification) {
        for observer in workspaceObservers {
            NSWorkspace.shared.notificationCenter.removeObserver(observer)
        }
        try? eventLog?.close()
    }

    private func showError(_ title: String, detail: String) {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = detail
        alert.runModal()
    }

    private func argument(after flag: String) -> String? {
        let args = CommandLine.arguments
        guard let index = args.firstIndex(of: flag), args.indices.contains(index + 1) else { return nil }
        return args[index + 1]
    }

    private func configureEventLog() {
        guard let path = argument(after: "--event-log") else { return }
        if !FileManager.default.fileExists(atPath: path) {
            guard FileManager.default.createFile(atPath: path, contents: nil) else { return }
        }
        eventLog = try? FileHandle(forWritingTo: URL(fileURLWithPath: path))
        _ = try? eventLog?.seekToEnd()
        log(["type": "launch", "pid": ProcessInfo.processInfo.processIdentifier])
    }

    private func log(_ fields: [String: Any]) {
        guard let eventLog else { return }
        var record = fields
        record["timestamp"] = ISO8601DateFormatter().string(from: Date())
        guard var data = try? JSONSerialization.data(withJSONObject: record, options: [.sortedKeys]) else { return }
        data.append(0x0A)
        try? eventLog.write(contentsOf: data)
    }
}
