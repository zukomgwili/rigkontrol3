import CLibusb
import Foundation

enum LibUSBBulkInputReadResult {
    case packet(LibUSBBulkReadPacket)
    case idle
    case failed(String)
}

/// The USB owner must stop and join this reader before releasing its handle.
/// Only the reader calls `readPacket`; all mutable state is protected by `condition`.
final class LibUSBBulkInputPump: @unchecked Sendable {
    private let condition = NSCondition()
    private let firstReadFinished = DispatchSemaphore(value: 0)
    private let readPacket: () -> LibUSBBulkInputReadResult
    private let capacity: Int
    private var packets: [LibUSBBulkReadPacket] = []
    private var stopRequested = false
    private var finished = false
    private var readFailure: String?

    init(capacity: Int = 256, readPacket: @escaping () -> LibUSBBulkInputReadResult) {
        precondition(capacity > 0)
        self.capacity = capacity
        self.readPacket = readPacket
    }

    convenience init(
        handle: OpaquePointer, endpointAddress: UInt8, packetSize: Int,
        timeoutMilliseconds: UInt32
    ) {
        self.init {
            var buffer = [UInt8](repeating: 0, count: packetSize)
            var transferred: Int32 = 0
            let status = buffer.withUnsafeMutableBufferPointer { pointer in
                libusb_bulk_transfer(
                    handle, endpointAddress, pointer.baseAddress, Int32(packetSize),
                    &transferred, timeoutMilliseconds)
            }
            if status == Int32(LIBUSB_SUCCESS.rawValue) {
                guard transferred > 0 else { return .idle }
                return .packet(LibUSBBulkReadPacket(
                    endpointAddress: Int(endpointAddress), timestamp: Date(),
                    bytes: Array(buffer.prefix(Int(transferred)))))
            }
            if status == Int32(LIBUSB_ERROR_TIMEOUT.rawValue) { return .idle }
            let name = libusb_error_name(status).map { String(cString: $0) }
                ?? "LIBUSB_ERROR_\(status)"
            return .failed(name)
        }
    }

    /// Wait for the first bounded read, including an ordinary cold-plug timeout.
    /// This ensures input has actually been attempted before the first OUT command.
    func start() -> String? {
        DispatchQueue.global(qos: .userInitiated).async { [self] in readContinuously() }
        if firstReadFinished.wait(timeout: .now() + .milliseconds(250)) == .timedOut {
            condition.lock()
            if readFailure == nil { readFailure = "USB input reader did not start within 250 ms" }
            stopRequested = true
            condition.broadcast()
            condition.unlock()
        }
        return failure
    }

    var failure: String? {
        condition.lock()
        defer { condition.unlock() }
        return readFailure
    }

    /// Called only by the owner. Its short wait keeps stop/sleep responsive.
    func nextPacket() -> LibUSBBulkReadPacket? {
        condition.lock()
        defer { condition.unlock() }
        if packets.isEmpty, readFailure == nil, !finished {
            _ = condition.wait(until: Date().addingTimeInterval(0.1))
        }
        guard readFailure == nil, !packets.isEmpty else { return nil }
        return packets.removeFirst()
    }

    func stopAndWait() {
        condition.lock()
        stopRequested = true
        condition.broadcast()
        while !finished { condition.wait() }
        condition.unlock()
    }

    private func readContinuously() {
        var isFirstRead = true
        defer {
            condition.lock()
            finished = true
            condition.broadcast()
            condition.unlock()
            if isFirstRead { firstReadFinished.signal() }
        }
        while true {
            condition.lock()
            let shouldStop = stopRequested
            condition.unlock()
            if shouldStop { return }

            let result = readPacket()
            condition.lock()
            switch result {
            case .packet(let packet):
                if packets.count == capacity {
                    readFailure = "USB input queue overflow (\(capacity) packets); controller stream stopped"
                    stopRequested = true
                } else {
                    packets.append(packet)
                }
            case .idle:
                break
            case .failed(let error):
                readFailure = error
                stopRequested = true
            }
            condition.broadcast()
            condition.unlock()
            if isFirstRead {
                isFirstRead = false
                firstReadFinished.signal()
            }
        }
    }
}
