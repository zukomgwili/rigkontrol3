import CoreMIDI
import Foundation

public enum MIDIEndpointDirection: String, Sendable {
    case source
    case destination
}

public struct MIDIEndpointSnapshot: Equatable, Sendable {
    public let direction: MIDIEndpointDirection
    public let name: String?
    public let manufacturer: String?
    public let model: String?
    public let uniqueID: Int32?

    public init(
        direction: MIDIEndpointDirection,
        name: String?,
        manufacturer: String?,
        model: String?,
        uniqueID: Int32?
    ) {
        self.direction = direction
        self.name = name
        self.manufacturer = manufacturer
        self.model = model
        self.uniqueID = uniqueID
    }
}

public enum MIDIEndpointProbe {
    public static func rigKontrol3Endpoints() -> [MIDIEndpointSnapshot] {
        allEndpoints().filter { endpoint in
            let text = [endpoint.name, endpoint.manufacturer, endpoint.model]
                .compactMap { $0 }
                .joined(separator: " ")
                .lowercased()
            return text.contains("rigkontrol")
                || text.contains("rig kontrol")
                || text.contains("native instruments")
                || text.contains("17cc")
                || text.contains("1940")
        }
    }

    public static func allEndpoints() -> [MIDIEndpointSnapshot] {
        var endpoints: [MIDIEndpointSnapshot] = []
        for index in 0..<MIDIGetNumberOfSources() {
            endpoints.append(snapshot(for: MIDIGetSource(index), direction: .source))
        }
        for index in 0..<MIDIGetNumberOfDestinations() {
            endpoints.append(snapshot(for: MIDIGetDestination(index), direction: .destination))
        }
        return endpoints
    }

    private static func snapshot(for endpoint: MIDIEndpointRef, direction: MIDIEndpointDirection)
        -> MIDIEndpointSnapshot
    {
        MIDIEndpointSnapshot(
            direction: direction,
            name: stringProperty(endpoint, kMIDIPropertyName),
            manufacturer: stringProperty(endpoint, kMIDIPropertyManufacturer),
            model: stringProperty(endpoint, kMIDIPropertyModel),
            uniqueID: integerProperty(endpoint, kMIDIPropertyUniqueID)
        )
    }

    private static func stringProperty(_ object: MIDIObjectRef, _ key: CFString) -> String? {
        var unmanagedValue: Unmanaged<CFString>?
        let status = MIDIObjectGetStringProperty(object, key, &unmanagedValue)
        guard status == noErr, let value = unmanagedValue?.takeRetainedValue() else {
            return nil
        }
        return value as String
    }

    private static func integerProperty(_ object: MIDIObjectRef, _ key: CFString) -> Int32? {
        var value: Int32 = 0
        let status = MIDIObjectGetIntegerProperty(object, key, &value)
        return status == noErr ? value : nil
    }
}
