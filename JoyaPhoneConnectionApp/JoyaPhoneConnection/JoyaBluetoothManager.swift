//
//  JoyaBluetoothManager.swift
//  JoyaPhoneConnection
//

import CoreBluetooth
import Combine
import Foundation
import SwiftUI

struct LogEntry: Identifiable {
    let id = UUID()
    let date: Date
    let message: String
}

enum JoyaConnectionState: Equatable {
    case bluetoothOff
    case idle
    case scanning
    case connecting
    case discovering
    case connected
    case disconnected
    case failed(String)

    var title: String {
        switch self {
        case .bluetoothOff:
            return "Bluetooth apagado"
        case .idle:
            return "Sin dispositivo conectado"
        case .scanning:
            return "Buscando Joya"
        case .connecting:
            return "Conectando"
        case .discovering:
            return "Preparando conexion"
        case .connected:
            return "Joya conectado"
        case .disconnected:
            return "Joya desconectado"
        case .failed:
            return "No se pudo conectar"
        }
    }

    var detail: String {
        switch self {
        case .bluetoothOff:
            return "Activa Bluetooth para seguir."
        case .idle:
            return "Toca conectar dispositivo para empezar."
        case .scanning:
            return "Presiona dos veces el boton de Joya para despertarlo."
        case .connecting:
            return "Encontramos un Joya disponible."
        case .discovering:
            return "Configurando el canal de mensajes."
        case .connected:
            return "Listo, tu dispositivo se conecto."
        case .disconnected:
            return "Puedes volver a conectar cuando quieras."
        case .failed(let message):
            return message
        }
    }

    var logName: String {
        switch self {
        case .bluetoothOff:
            return "bluetoothOff"
        case .idle:
            return "idle"
        case .scanning:
            return "scanning"
        case .connecting:
            return "connecting"
        case .discovering:
            return "discovering"
        case .connected:
            return "connected"
        case .disconnected:
            return "disconnected"
        case .failed(let message):
            return "failed(\(message))"
        }
    }
}

enum JoyaActivityState: Equatable {
    case none
    case routine
    case emergency

    var title: String {
        switch self {
        case .none:
            return "No compartiendo"
        case .routine:
            return "Rutina iniciada"
        case .emergency:
            return "Emergencia activa"
        }
    }
}

// Firmware v2 uses compact binary commands over the custom Joya GATT service.
private enum JoyaIncomingCommand: UInt8 {
    case startRoutine = 0x02
    case endRoutine = 0x03
    case emergency = 0x04
    case ack = 0x05
    case nack = 0x06

    var logName: String {
        switch self {
        case .startRoutine:
            return "START_ROUTINE"
        case .endRoutine:
            return "END_ROUTINE"
        case .emergency:
            return "EMERGENCY"
        case .ack:
            return "ACK"
        case .nack:
            return "NACK"
        }
    }
}

private enum JoyaOutgoingCommand: UInt8 {
    case ackEmergency = 0x41
    case stopEmergency = 0x42
    case followMe = 0x43
    case friendEmergency = 0x44

    var logName: String {
        switch self {
        case .ackEmergency:
            return "ACK_EMERGENCY"
        case .stopEmergency:
            return "STOP_EMERGENCY"
        case .followMe:
            return "FOLLOW_ME"
        case .friendEmergency:
            return "FRIEND_EMERGENCY"
        }
    }
}

@MainActor
final class JoyaBluetoothManager: NSObject, ObservableObject {
    @Published private(set) var connectionState: JoyaConnectionState = .idle
    @Published private(set) var activityState: JoyaActivityState = .none
    @Published private(set) var logs: [LogEntry] = []
    @Published private(set) var lastMessage = "Sin mensajes todavia"

    // Firmware v2 exposes a custom service instead of the Nordic UART Service.
    private let joyaServiceUUID = CBUUID(string: "A407E00A-00C1-464D-9173-2CB8BE585343")
    private let joyaTXUUID = CBUUID(string: "A407E00A-00C1-464D-9173-2CB8BE585344")
    private let joyaRXUUID = CBUUID(string: "A407E00A-00C1-464D-9173-2CB8BE585345")
    private let joyaRXAuthUUID = CBUUID(string: "A407E00A-00C1-464D-9173-2CB8BE585346")
    private let savedPeripheralKey = "joya.savedPeripheralID"
    private let legacyAppIDKey = "joya.appID"
    private let authAppIDKey = "joya.authAppID.v2"
    // Keep this aligned with firmware SIZE_APP_ID. The current debug firmware expects 5 bytes.
    private let authAppIDSize = 5
    private let connectionTimeoutSeconds: TimeInterval = 8

    private var central: CBCentralManager?
    private var peripheral: CBPeripheral?
    private var rxCharacteristic: CBCharacteristic?
    private var txCharacteristic: CBCharacteristic?
    private var rxAuthCharacteristic: CBCharacteristic?
    private var connectionTimeoutWorkItem: DispatchWorkItem?
    private var activeConnectionAttemptID: UUID?
    private var shouldAutoReconnect = true
    private var hasTriedRestore = false

    // CoreBluetooth can surface the same ready state through multiple callbacks,
    // so keep the authentication write idempotent per connection.
    private var didSendAuthAppID = false
    private var didCompleteAuthentication = false

    private var authAppID: Data {
        if let existing = UserDefaults.standard.data(forKey: authAppIDKey),
           existing.count == authAppIDSize {
            return existing
        }

        let source = UserDefaults.standard.string(forKey: legacyAppIDKey) ?? UUID().uuidString
        var bytes = Array(source.replacingOccurrences(of: "-", with: "").utf8.prefix(authAppIDSize))

        while bytes.count < authAppIDSize {
            bytes.append(0x30)
        }

        let generated = Data(bytes)
        UserDefaults.standard.set(generated, forKey: authAppIDKey)
        return generated
    }

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
        addLog("App abierta")
    }

    deinit {
        connectionTimeoutWorkItem?.cancel()
    }

    var isConnected: Bool {
        connectionState == .connected
    }

    var canConnect: Bool {
        switch connectionState {
        case .idle, .disconnected, .failed:
            return true
        default:
            return false
        }
    }

    var canRestartSearch: Bool {
        switch connectionState {
        case .scanning, .connecting, .discovering:
            return true
        default:
            return false
        }
    }

    func connectButtonTapped() {
        shouldAutoReconnect = true
        addLog("Usuario toco conectar dispositivo")
        startScanning(reason: "boton conectar")
    }

    func restartSearchTapped() {
        shouldAutoReconnect = true
        addLog("Usuario pidio reiniciar busqueda BLE")

        if let peripheral {
            central?.cancelPeripheralConnection(peripheral)
            self.peripheral = nil
        }

        startScanning(reason: "boton reintentar busqueda")
    }

    func disconnectForTest() {
        shouldAutoReconnect = false
        activityState = .none
        addLog("Usuario pidio desconectar Joya")

        if let peripheral {
            cancelConnectionTimeout(reason: "desconexion manual")
            central?.cancelPeripheralConnection(peripheral)
        } else {
            setConnectionState(.disconnected, reason: "desconexion manual sin peripheral activo")
        }
    }

    func cancelRoutine() {
        // Routine end is now reported by firmware; this only clears app-side state.
        activityState = .none
        addLog("Usuario cancelo rutina desde la app")
    }

    func cancelEmergency() {
        activityState = .none
        addLog("Usuario cancelo emergencia desde la app")
        send(.stopEmergency)
    }

    func sendFriendComingForYou() {
        addLog("Usuario aviso que un amigo va en camino")
        send(.followMe)
    }

    func sendFriendEmergency() {
        addLog("Usuario aviso emergencia de un amigo")
        send(.friendEmergency)
    }

    func clearLogs() {
        logs.removeAll()
        addLog("Log limpiado")
    }

    private func startScanning(reason: String) {
        guard central?.state == .poweredOn else {
            setConnectionState(.bluetoothOff, reason: "scan pedido con Bluetooth \(centralStateDescription)")
            addLog("No se puede escanear: Bluetooth no esta activo (\(centralStateDescription))")
            return
        }

        cancelConnectionTimeout(reason: "empezar scan: \(reason)")
        central?.stopScan()
        rxCharacteristic = nil
        txCharacteristic = nil
        rxAuthCharacteristic = nil
        didSendAuthAppID = false
        didCompleteAuthentication = false
        setConnectionState(.scanning, reason: reason)
        central?.scanForPeripherals(
            withServices: [joyaServiceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
        addLog("Scan iniciado por servicio Joya \(joyaServiceUUID.uuidString); isScanning=\(central?.isScanning == true)")
    }

    private func restoreSavedPeripheralIfNeeded() {
        guard !hasTriedRestore else { return }
        hasTriedRestore = true

        if let savedID = UserDefaults.standard.string(forKey: savedPeripheralKey) {
            UserDefaults.standard.removeObject(forKey: savedPeripheralKey)
            addLog("Peripheral guardado descartado para evitar pairing viejo: \(savedID)")
        } else {
            addLog("No hay Joya guardado todavia")
        }

        setConnectionState(.idle, reason: "reconexion siempre por scan")
    }

    private func connect(to discoveredPeripheral: CBPeripheral, source: String) {
        central?.stopScan()
        peripheral = discoveredPeripheral
        peripheral?.delegate = self
        setConnectionState(.connecting, reason: "connect desde \(source)")
        addLog("Connect pedido desde \(source): \(describe(discoveredPeripheral)); central=\(centralStateDescription)")
        scheduleConnectionTimeout(for: discoveredPeripheral, source: source)
        central?.connect(discoveredPeripheral, options: nil)
    }

    private func scheduleConnectionTimeout(for peripheral: CBPeripheral, source: String) {
        let attemptID = UUID()
        let peripheralID = peripheral.identifier
        let workItem = DispatchWorkItem { [weak self] in
            self?.connectionAttemptTimedOut(attemptID: attemptID, peripheralID: peripheralID)
        }

        cancelConnectionTimeout(reason: "nuevo intento de conexion")
        activeConnectionAttemptID = attemptID
        connectionTimeoutWorkItem = workItem
        DispatchQueue.main.asyncAfter(deadline: .now() + connectionTimeoutSeconds, execute: workItem)
        addLog("Timeout armado: intento=\(shortID(attemptID)) peripheral=\(shortID(peripheralID)) source=\(source) segundos=\(Int(connectionTimeoutSeconds))")
    }

    private func cancelConnectionTimeout(reason: String) {
        guard connectionTimeoutWorkItem != nil || activeConnectionAttemptID != nil else { return }

        addLog("Timeout cancelado: intento=\(shortID(activeConnectionAttemptID)) motivo=\(reason)")
        connectionTimeoutWorkItem?.cancel()
        connectionTimeoutWorkItem = nil
        activeConnectionAttemptID = nil
    }

    private func connectionAttemptTimedOut(attemptID: UUID, peripheralID: UUID) {
        addLog("Timeout disparado: intento=\(shortID(attemptID)) estado=\(connectionState.logName) peripheralActual=\(shortID(peripheral?.identifier)) esperado=\(shortID(peripheralID))")

        guard activeConnectionAttemptID == attemptID else {
            addLog("Timeout ignorado: intento viejo")
            return
        }

        activeConnectionAttemptID = nil
        connectionTimeoutWorkItem = nil

        guard connectionState == .connecting,
              let timedOutPeripheral = peripheral,
              timedOutPeripheral.identifier == peripheralID else {
            addLog("Timeout ignorado: la app ya no esta esperando esa conexion")
            return
        }

        addLog("Conexion BLE sin respuesta; reiniciando busqueda")
        UserDefaults.standard.removeObject(forKey: savedPeripheralKey)
        central?.cancelPeripheralConnection(timedOutPeripheral)
        peripheral = nil
        startScanning(reason: "timeout conectando a \(shortID(peripheralID))")
    }

    private func send(_ command: JoyaOutgoingCommand) {
        guard let rxCharacteristic else {
            addLog("No se pudo enviar: canal RX no listo (\(command.logName))")
            return
        }

        write(Data([command.rawValue]), to: rxCharacteristic, label: "\(command.logName) \(hexByte(command.rawValue))")
    }

    private func sendAuthAppIDIfNeeded() {
        guard !didSendAuthAppID else {
            addLog("APP_ID omitido: ya se envio uno en esta conexion")
            return
        }

        guard let rxAuthCharacteristic else {
            addLog("No se pudo autenticar: canal RX AUTH no listo")
            return
        }

        let appID = authAppID
        didSendAuthAppID = true
        write(appID, to: rxAuthCharacteristic, label: "APP_ID bytes=\(hexString(appID))")
    }

    private func write(_ data: Data, to characteristic: CBCharacteristic, label: String) {
        guard let peripheral else {
            addLog("No se pudo enviar: peripheral BLE no listo (\(label))")
            return
        }

        let writeType: CBCharacteristicWriteType = characteristic.properties.contains(.writeWithoutResponse)
            ? .withoutResponse
            : .withResponse
        peripheral.writeValue(data, for: characteristic, type: writeType)
        addLog("App -> Joya: \(label) type=\(writeType.logName) peripheral=\(peripheral.state.logName) characteristic=\(characteristic.uuid.uuidString)[\(characteristic.properties.logName)]")
    }

    private func handleIncoming(_ data: Data) {
        guard !data.isEmpty else {
            addLog("Mensaje BLE recibido vacio")
            return
        }

        let bytes = Array(data)
        let message = bytes.map(commandDescription(for:)).joined(separator: ", ")
        lastMessage = message
        addLog("Joya -> App: \(message)")

        for byte in bytes {
            handleIncoming(byte)
        }
    }

    private func handleIncoming(_ byte: UInt8) {
        guard let command = JoyaIncomingCommand(rawValue: byte) else {
            addLog("Comando Joya desconocido: \(hexByte(byte))")
            return
        }

        switch command {
        case .startRoutine:
            activityState = .routine
            addLog("Boton Joya: rutina iniciada")
        case .endRoutine:
            activityState = .none
            addLog("Boton Joya: rutina finalizada")
        case .emergency:
            handleEmergencyOnFromJoya()
        case .ack:
            handleAckFromJoya()
        case .nack:
            handleNackFromJoya()
        }
    }

    private func handleAckFromJoya() {
        if !didSendAuthAppID {
            addLog("ACK de autenticacion recibido; enviando APP_ID")
            sendAuthAppIDIfNeeded()
        } else if !didCompleteAuthentication {
            didCompleteAuthentication = true
            setConnectionState(.connected, reason: "ACK de APP_ID recibido")
            addLog("Autenticacion Joya completada")
        } else {
            addLog("ACK recibido")
        }
    }

    private func handleNackFromJoya() {
        if !didCompleteAuthentication {
            didSendAuthAppID = false
            setConnectionState(.failed("Joya rechazo el APP_ID guardado."), reason: "NACK durante autenticacion")
        } else {
            addLog("NACK recibido desde Joya")
        }
    }

    private func handleEmergencyOnFromJoya() {
        let wasEmergencyActive = activityState == .emergency
        activityState = .emergency
        send(.ackEmergency)

        if wasEmergencyActive {
            addLog("Boton Joya: emergencia ya estaba activa; ACK reenviado")
        } else {
            addLog("Boton Joya: emergencia iniciada; ACK enviado")
        }
    }

    private func savePeripheralID(_ peripheral: CBPeripheral) {
        UserDefaults.standard.removeObject(forKey: savedPeripheralKey)
        addLog("Peripheral conectado observado sin guardar: \(shortID(peripheral.identifier))")
    }

    private func setConnectionState(_ newState: JoyaConnectionState, reason: String) {
        let oldState = connectionState
        connectionState = newState

        if oldState == newState {
            addLog("Estado sigue \(newState.logName): \(reason)")
        } else {
            addLog("Estado \(oldState.logName) -> \(newState.logName): \(reason)")
        }
    }

    private func addLog(_ message: String) {
        logs.insert(LogEntry(date: Date(), message: message), at: 0)
        if logs.count > 200 {
            logs.removeLast(logs.count - 200)
        }
    }

    private var centralStateDescription: String {
        guard let central else { return "central=nil" }

        return "\(central.state.logName), isScanning=\(central.isScanning)"
    }

    private func describe(_ peripheral: CBPeripheral) -> String {
        let name = peripheral.name ?? "sin nombre"
        return "id=\(shortID(peripheral.identifier)) name=\(name) state=\(peripheral.state.logName)"
    }

    private func describeAdvertisement(_ advertisementData: [String: Any]) -> String {
        var parts: [String] = []

        if let localName = advertisementData[CBAdvertisementDataLocalNameKey] as? String {
            parts.append("localName=\(localName)")
        }

        if let isConnectable = advertisementData[CBAdvertisementDataIsConnectable] as? Bool {
            parts.append("connectable=\(isConnectable)")
        }

        if let services = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID], !services.isEmpty {
            parts.append("services=\(services.map(\.uuidString).joined(separator: ","))")
        }

        if let overflow = advertisementData[CBAdvertisementDataOverflowServiceUUIDsKey] as? [CBUUID], !overflow.isEmpty {
            parts.append("overflow=\(overflow.map(\.uuidString).joined(separator: ","))")
        }

        return parts.isEmpty ? "adv=sin campos utiles" : parts.joined(separator: " ")
    }

    private func shortID(_ id: UUID?) -> String {
        guard let id else { return "nil" }
        return String(id.uuidString.prefix(8))
    }

    private func commandDescription(for byte: UInt8) -> String {
        if let command = JoyaIncomingCommand(rawValue: byte) {
            return "\(command.logName) \(hexByte(byte))"
        }

        return "UNKNOWN \(hexByte(byte))"
    }

    private func hexByte(_ byte: UInt8) -> String {
        String(format: "0x%02X", byte)
    }

    private func hexString(_ data: Data) -> String {
        data.map { String(format: "%02X", $0) }.joined(separator: " ")
    }
}

private extension CBManagerState {
    var logName: String {
        switch self {
        case .unknown:
            return "unknown"
        case .resetting:
            return "resetting"
        case .unsupported:
            return "unsupported"
        case .unauthorized:
            return "unauthorized"
        case .poweredOff:
            return "poweredOff"
        case .poweredOn:
            return "poweredOn"
        @unknown default:
            return "unknown-new"
        }
    }
}

private extension CBPeripheralState {
    var logName: String {
        switch self {
        case .disconnected:
            return "disconnected"
        case .connecting:
            return "connecting"
        case .connected:
            return "connected"
        case .disconnecting:
            return "disconnecting"
        @unknown default:
            return "unknown-new"
        }
    }
}

private extension CBCharacteristicProperties {
	var logName: String {
		var parts: [String] = []

        if contains(.read) { parts.append("read") }
        if contains(.write) { parts.append("write") }
        if contains(.writeWithoutResponse) { parts.append("writeWithoutResponse") }
        if contains(.notify) { parts.append("notify") }
        if contains(.indicate) { parts.append("indicate") }

		return parts.isEmpty ? "none" : parts.joined(separator: "|")
	}
}

private extension CBCharacteristicWriteType {
	var logName: String {
		switch self {
		case .withResponse:
			return "withResponse"
		case .withoutResponse:
			return "withoutResponse"
		@unknown default:
			return "unknown"
		}
	}
}

extension JoyaBluetoothManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        addLog("Central state update: \(central.state.logName)")

        switch central.state {
        case .poweredOn:
            addLog("Bluetooth activo")
            restoreSavedPeripheralIfNeeded()
        case .poweredOff:
            setConnectionState(.bluetoothOff, reason: "central poweredOff")
            addLog("Bluetooth apagado")
        case .unauthorized:
            setConnectionState(.failed("La app no tiene permiso para usar Bluetooth."), reason: "central unauthorized")
            addLog("Bluetooth sin permiso")
        case .unsupported:
            setConnectionState(.failed("Este dispositivo no soporta Bluetooth LE."), reason: "central unsupported")
            addLog("Bluetooth LE no soportado")
        case .resetting:
            addLog("Bluetooth reiniciandose")
        case .unknown:
            addLog("Estado Bluetooth desconocido")
        @unknown default:
            addLog("Estado Bluetooth nuevo/no reconocido")
        }
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        let name = peripheral.name
            ?? advertisementData[CBAdvertisementDataLocalNameKey] as? String
            ?? "Joya"

        addLog("Detectado \(name) RSSI \(RSSI): \(describe(peripheral)); \(describeAdvertisement(advertisementData))")
        connect(to: peripheral, source: "scan discovery")
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        cancelConnectionTimeout(reason: "didConnect \(shortID(peripheral.identifier))")
        savePeripheralID(peripheral)
        setConnectionState(.discovering, reason: "didConnect \(shortID(peripheral.identifier))")
        addLog("Conexion BLE establecida: \(describe(peripheral))")
        peripheral.discoverServices([joyaServiceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        cancelConnectionTimeout(reason: "didFailToConnect \(shortID(peripheral.identifier))")
        setConnectionState(.failed(error?.localizedDescription ?? "Fallo la conexion."), reason: "didFailToConnect")
        addLog("Fallo conexion: \(describe(peripheral)); error=\(error?.localizedDescription ?? "sin detalle")")

        if shouldAutoReconnect {
            startScanning(reason: "auto reconnect despues de didFailToConnect")
        }
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        cancelConnectionTimeout(reason: "didDisconnect \(shortID(peripheral.identifier))")
        rxCharacteristic = nil
        txCharacteristic = nil
        rxAuthCharacteristic = nil
        didSendAuthAppID = false
        didCompleteAuthentication = false
        activityState = .none
        setConnectionState(.disconnected, reason: "didDisconnect")
        addLog("Desconectado: \(describe(peripheral)); error=\(error?.localizedDescription ?? "sin error")")

        if shouldAutoReconnect {
            startScanning(reason: "auto reconnect despues de didDisconnect")
        }
    }
}

extension JoyaBluetoothManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            setConnectionState(.failed(error.localizedDescription), reason: "didDiscoverServices error")
            addLog("Error descubriendo servicios: \(error.localizedDescription)")
            return
        }

        guard let services = peripheral.services else {
            addLog("didDiscoverServices sin lista de servicios")
            return
        }

        addLog("Servicios descubiertos: \(services.map { $0.uuid.uuidString }.joined(separator: ","))")
        for service in services where service.uuid == joyaServiceUUID {
            addLog("Servicio Joya encontrado")
            peripheral.discoverCharacteristics([joyaRXUUID, joyaTXUUID, joyaRXAuthUUID], for: service)
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: Error?
    ) {
        if let error {
            setConnectionState(.failed(error.localizedDescription), reason: "didDiscoverCharacteristics error")
            addLog("Error descubriendo caracteristicas: \(error.localizedDescription)")
            return
        }

        let characteristics = service.characteristics ?? []
        addLog("Caracteristicas descubiertas para \(service.uuid.uuidString): \(characteristics.map { "\($0.uuid.uuidString)[\($0.properties.logName)]" }.joined(separator: ","))")

        for characteristic in characteristics {
            if characteristic.uuid == joyaRXUUID {
                rxCharacteristic = characteristic
                addLog("Canal RX listo")
            } else if characteristic.uuid == joyaTXUUID {
                txCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
                addLog("Canal TX listo, activando notificaciones")
            } else if characteristic.uuid == joyaRXAuthUUID {
                rxAuthCharacteristic = characteristic
                addLog("Canal RX AUTH listo")
            }
        }

        if rxCharacteristic != nil && txCharacteristic != nil && rxAuthCharacteristic != nil {
            addLog("Canales BLE listos; esperando ACK de autenticacion")
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateNotificationStateFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        if let error {
            addLog("No se pudieron activar notificaciones en \(characteristic.uuid.uuidString): \(error.localizedDescription)")
            return
        }

        if characteristic.uuid == joyaTXUUID {
            addLog(characteristic.isNotifying ? "Notificaciones activas; esperando ACK de Joya" : "Notificaciones apagadas")
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == joyaRXUUID || characteristic.uuid == joyaRXAuthUUID else { return }

        let channelName = characteristic.uuid == joyaRXAuthUUID ? "RX AUTH" : "RX"

        if let error {
            addLog("Write \(channelName) fallo: \(error.localizedDescription)")
        } else {
            addLog("Write \(channelName) OK")
        }
    }

    func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        addLog("Peripheral listo para writeWithoutResponse: \(describe(peripheral))")
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            addLog("Error recibiendo mensaje: \(error.localizedDescription)")
            return
        }

        guard characteristic.uuid == joyaTXUUID,
              let data = characteristic.value else {
            addLog("Mensaje BLE recibido desde una caracteristica inesperada")
            return
        }

        handleIncoming(data)
    }
}
