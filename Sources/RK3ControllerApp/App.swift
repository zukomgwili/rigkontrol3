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
    private let startStopItem = NSMenuItem(title: "Stop Controller", action: nil, keyEquivalent: "")
    private let loginItem = NSMenuItem(title: "Launch at Login", action: nil, keyEquivalent: "")
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
        buildMenu()
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
            expressionCalibration: expressionCalibration
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
        menu.addItem(.separator())
        startStopItem.target = self
        startStopItem.action = #selector(toggleController)
        menu.addItem(startStopItem)
        let setupItem = NSMenuItem(title: "Setup & Test…", action: #selector(showSetup), keyEquivalent: "")
        setupItem.target = self
        menu.addItem(setupItem)
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

    private func loadCalibration() -> ExpressionCalibration {
        let defaults = UserDefaults.standard
        guard let minimum = defaults.object(forKey: "expressionRawMinimum") as? Int,
            let maximum = defaults.object(forKey: "expressionRawMaximum") as? Int,
            minimum >= 0, maximum <= 65535, maximum - minimum >= 100
        else { return ExpressionCalibration() }
        return ExpressionCalibration(rawMinimum: minimum, rawMaximum: maximum)
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
            expressionCalibration = ExpressionCalibration(
                rawMinimum: range.lowerBound, rawMaximum: range.upperBound)
            session?.setExpressionCalibration(expressionCalibration)
            UserDefaults.standard.set(range.lowerBound, forKey: "expressionRawMinimum")
            UserDefaults.standard.set(range.upperBound, forKey: "expressionRawMaximum")
            isCalibrating = false
            calibrationButton.title = "Calibrate Pedal…"
            expressionRange = nil
            latestExpression = nil
            log(["type": "calibration", "rawMinimum": range.lowerBound, "rawMaximum": range.upperBound])
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
            calibrationButton.frame = NSRect(x: 18, y: 12, width: 200, height: 28)
            calibrationButton.bezelStyle = .rounded
            calibrationButton.target = self
            calibrationButton.action = #selector(toggleCalibration)
            calibrationButton.isEnabled = status == .connected
            window.contentView?.addSubview(calibrationButton)
            windowStartStopButton.frame = NSRect(x: 230, y: 12, width: 150, height: 28)
            windowStartStopButton.bezelStyle = .rounded
            windowStartStopButton.target = self
            windowStartStopButton.action = #selector(toggleController)
            window.contentView?.addSubview(windowStartStopButton)
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
            "  \($0 + 1)      \(20 + $0)       \(presses[$0]) / \(releases[$0])"
        }.joined(separator: "\n")
        let scrollPosition = text.enclosingScrollView?.contentView.bounds.origin
        text.string = """
        \(statusText)

        GUITAR RIG 6 SETUP

        1. Choose your separate audio interface in Guitar Rig.
           Keep RK3 audio unused in other apps too.
        2. Preferences → MIDI: enable RigKontrol3 Bridge.
           Receive on channel 16 or Omni.
        3. MIDI Learn an effect's on/off control, then press
           the switch you want. Configure the host so one
           press toggles once; release must not toggle again.
           If it takes two presses, check Hold Mode.
        4. Learn wah position, then sweep the pedal.
           Use one of switches 1–8 for wah on/off.
        5. Save the preset and verify assignments after reload.

        LIVE INPUT (SINCE APP LAUNCH)

        Switch  MIDI CC  Presses / release-reset messages
        \(switchLines)

        Wah CC11: \(latestExpression.map(String.init) ?? "—")
        Observed range: \(range) (full travel: 0…127)
        Calibrated raw travel: \(expressionCalibration.rawMinimum)…\(expressionCalibration.rawMaximum)
        \(isCalibrating ? "CALIBRATING: sweep fully heel-to-toe and back, then\nclick Finish Calibration. Raw range: \(calibrationRange.map { "\($0.lowerBound)…\($0.upperBound)" } ?? "waiting…")" : "Use Calibrate Pedal if normal travel misses 0 or 127.")

        LEDs indicate physical presses, not effect state.
        Release all switches after connecting. A switch held
        on attachment is ignored until released and pressed.
        After a lost release, a new press may send an extra
        zero first to restore the host's switch detection.

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
