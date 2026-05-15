//
//  JoyaBluetoothManager.swift
//  JoyaFirmwareTest
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

@MainActor
final class JoyaBluetoothManager: NSObject, ObservableObject {
    @Published private(set) var connectionState: JoyaConnectionState = .idle
    @Published private(set) var activityState: JoyaActivityState = .none
    @Published private(set) var logs: [LogEntry] = []
    @Published private(set) var lastMessage = "Sin mensajes todavia"

    private let nusServiceUUID = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    private let nusRXUUID = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
    private let nusTXUUID = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
    private let savedPeripheralKey = "joya.savedPeripheralID"
    private let appIDKey = "joya.appID"

    private var central: CBCentralManager?
    private var peripheral: CBPeripheral?
    private var rxCharacteristic: CBCharacteristic?
    private var txCharacteristic: CBCharacteristic?
    private var shouldAutoReconnect = true
    private var hasTriedRestore = false

    // CoreBluetooth can surface the same ready state through multiple callbacks.
    // Keep the setup handshake idempotent so Joya does not receive duplicate CLAIMs.
    private var didSendPing = false
    private var didSendClaim = false

    private var appID: String {
        if let existing = UserDefaults.standard.string(forKey: appIDKey) {
            return existing
        }

        let generated = UUID().uuidString
        UserDefaults.standard.set(generated, forKey: appIDKey)
        return generated
    }

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
        addLog("App abierta")
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

    func connectButtonTapped() {
        shouldAutoReconnect = true
        addLog("Usuario toco conectar dispositivo")
        startScanning()
    }

    func disconnectForTest() {
        shouldAutoReconnect = false
        activityState = .none
        addLog("Usuario pidio desconectar Joya")

        if let peripheral {
            central?.cancelPeripheralConnection(peripheral)
        } else {
            connectionState = .disconnected
        }
    }

    func cancelRoutine() {
        activityState = .none
        addLog("Usuario cancelo rutina desde la app")
        send("CANCEL_ROUTINE")
    }

    func cancelEmergency() {
        activityState = .none
        addLog("Usuario cancelo emergencia desde la app")
        send("CANCEL_EMERGENCY")
    }

    func clearLogs() {
        logs.removeAll()
        addLog("Log limpiado")
    }

    private func startScanning() {
        guard central?.state == .poweredOn else {
            connectionState = .bluetoothOff
            addLog("No se puede escanear: Bluetooth no esta activo")
            return
        }

        rxCharacteristic = nil
        txCharacteristic = nil
        didSendPing = false
        didSendClaim = false
        connectionState = .scanning
        central?.scanForPeripherals(
            withServices: [nusServiceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
        addLog("Escaneando por servicio NUS de Joya")
    }

    private func restoreSavedPeripheralIfNeeded() {
        guard !hasTriedRestore else { return }
        hasTriedRestore = true

        guard let savedID = UserDefaults.standard.string(forKey: savedPeripheralKey),
              let uuid = UUID(uuidString: savedID) else {
            addLog("No hay Joya guardado todavia")
            connectionState = .idle
            return
        }

        let matches = central?.retrievePeripherals(withIdentifiers: [uuid]) ?? []
        guard let savedPeripheral = matches.first else {
            addLog("No se pudo restaurar el peripheral guardado")
            connectionState = .idle
            return
        }

        addLog("Intentando reconectar con Joya guardado")
        connect(to: savedPeripheral)
    }

    private func connect(to discoveredPeripheral: CBPeripheral) {
        central?.stopScan()
        peripheral = discoveredPeripheral
        peripheral?.delegate = self
        connectionState = .connecting
        addLog("Conectando a \(discoveredPeripheral.name ?? "Joya")")
        central?.connect(discoveredPeripheral, options: nil)
    }

    private func send(_ text: String) {
        guard let peripheral, let rxCharacteristic else {
            addLog("No se pudo enviar: canal BLE no listo (\(text))")
            return
        }

        guard let data = text.data(using: .utf8) else {
            addLog("No se pudo codificar mensaje: \(text)")
            return
        }

        let writeType: CBCharacteristicWriteType = rxCharacteristic.properties.contains(.write)
            ? .withResponse
            : .withoutResponse
        peripheral.writeValue(data, for: rxCharacteristic, type: writeType)
        addLog("App -> Joya: \(text)")
    }

    private func handleIncoming(_ text: String) {
        lastMessage = text
        addLog("Joya -> App: \(text)")

        if text.hasPrefix("HELLO:") {
            sendPingIfNeeded()
        } else if text == "PONG:claimed=0" {
            sendClaimIfNeeded()
        } else if text == "PONG:claimed=1" {
            connectionState = .connected
        } else if text.hasPrefix("CLAIM_OK") {
            connectionState = .connected
            addLog("Claim completado")
        } else if text == "EVENT:ROUTINE_START" || text == "EVENT:BUTTON_PRESS" || text == "EVENT:SINGLE_CLICK" {
            activityState = .routine
            addLog("Boton Joya: rutina iniciada")
        } else if text == "EVENT:ROUTINE_CANCEL" || text == "EVENT:HOLD" || text == "EVENT:LONG_PRESS" {
            activityState = .none
            addLog("Boton Joya: rutina cancelada")
        } else if text == "EVENT:EMERGENCY_START" || text == "EVENT:TRIPLE_CLICK" {
            activityState = .emergency
            addLog("Boton Joya: emergencia iniciada")
        } else if text == "EVENT:EMERGENCY_CANCEL" {
            activityState = .none
            addLog("Boton Joya: emergencia cancelada")
        } else if text.hasPrefix("ACK:") {
            addLog("Confirmacion recibida: \(text)")
        } else if text.hasPrefix("ERR:") {
            if text == "ERR:ALREADY_CLAIMED", didSendClaim {
                connectionState = .connected
                addLog("Joya ya estaba claimed; seguimos conectado")
            } else {
                connectionState = .failed(text)
            }
        }
    }

    private func sendPingIfNeeded() {
        guard !didSendPing else { return }
        didSendPing = true
        send("PING")
    }

    private func sendClaimIfNeeded() {
        guard !didSendClaim else {
            addLog("CLAIM omitido: ya se envio uno en esta conexion")
            return
        }

        didSendClaim = true
        send("CLAIM:\(appID)")
    }

    private func savePeripheralID(_ peripheral: CBPeripheral) {
        UserDefaults.standard.set(peripheral.identifier.uuidString, forKey: savedPeripheralKey)
        addLog("Peripheral guardado para reconexion")
    }

    private func addLog(_ message: String) {
        logs.insert(LogEntry(date: Date(), message: message), at: 0)
        if logs.count > 200 {
            logs.removeLast(logs.count - 200)
        }
    }
}

extension JoyaBluetoothManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            addLog("Bluetooth activo")
            restoreSavedPeripheralIfNeeded()
        case .poweredOff:
            connectionState = .bluetoothOff
            addLog("Bluetooth apagado")
        case .unauthorized:
            connectionState = .failed("La app no tiene permiso para usar Bluetooth.")
            addLog("Bluetooth sin permiso")
        case .unsupported:
            connectionState = .failed("Este dispositivo no soporta Bluetooth LE.")
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

        addLog("Detectado \(name) RSSI \(RSSI)")
        connect(to: peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        savePeripheralID(peripheral)
        connectionState = .discovering
        addLog("Conexion BLE establecida")
        peripheral.discoverServices([nusServiceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        connectionState = .failed(error?.localizedDescription ?? "Fallo la conexion.")
        addLog("Fallo conexion: \(error?.localizedDescription ?? "sin detalle")")

        if shouldAutoReconnect {
            startScanning()
        }
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        rxCharacteristic = nil
        txCharacteristic = nil
        didSendPing = false
        didSendClaim = false
        activityState = .none
        connectionState = .disconnected
        addLog("Desconectado: \(error?.localizedDescription ?? "sin error")")

        if shouldAutoReconnect {
            startScanning()
        }
    }
}

extension JoyaBluetoothManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            connectionState = .failed(error.localizedDescription)
            addLog("Error descubriendo servicios: \(error.localizedDescription)")
            return
        }

        guard let services = peripheral.services else { return }
        for service in services where service.uuid == nusServiceUUID {
            addLog("Servicio NUS encontrado")
            peripheral.discoverCharacteristics([nusRXUUID, nusTXUUID], for: service)
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: Error?
    ) {
        if let error {
            connectionState = .failed(error.localizedDescription)
            addLog("Error descubriendo caracteristicas: \(error.localizedDescription)")
            return
        }

        for characteristic in service.characteristics ?? [] {
            if characteristic.uuid == nusRXUUID {
                rxCharacteristic = characteristic
                addLog("Canal RX listo")
            } else if characteristic.uuid == nusTXUUID {
                txCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
                addLog("Canal TX listo, activando notificaciones")
            }
        }

        if rxCharacteristic != nil && txCharacteristic != nil {
            connectionState = .connected
            sendPingIfNeeded()
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateNotificationStateFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        if let error {
            addLog("No se pudieron activar notificaciones: \(error.localizedDescription)")
            return
        }

        if characteristic.uuid == nusTXUUID {
            addLog(characteristic.isNotifying ? "Notificaciones activas" : "Notificaciones apagadas")
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            addLog("Error recibiendo mensaje: \(error.localizedDescription)")
            return
        }

        guard characteristic.uuid == nusTXUUID,
              let data = characteristic.value,
              let text = String(data: data, encoding: .utf8) else {
            addLog("Mensaje BLE recibido sin texto valido")
            return
        }

        handleIncoming(text.trimmingCharacters(in: .whitespacesAndNewlines))
    }
}
