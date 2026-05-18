//
//  ContentView.swift
//  JoyaPhoneConnection
//
//  Created by Gonzalo Minuto on 5/12/26.
//

import SwiftUI
import UIKit

struct ContentView: View {
    @StateObject private var joya = JoyaBluetoothManager()
    @State private var showingLog = false

    var body: some View {
        ZStack {
            Color.white.ignoresSafeArea()

            VStack(spacing: 28) {
                header

                Spacer(minLength: 16)

                statusBlock

                activityBlock

                Spacer(minLength: 16)

                actionButtons

                Button {
                    showingLog = true
                } label: {
                    Label("Ver log", systemImage: "list.bullet.rectangle")
                        .font(.body.weight(.medium))
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
            }
            .padding(24)
        }
        .sheet(isPresented: $showingLog) {
            LogSheet(logs: joya.logs) {
                joya.clearLogs()
            }
            .presentationDetents([.medium, .large])
            .presentationDragIndicator(.visible)
        }
    }

    private var header: some View {
        VStack(spacing: 8) {
            Text("Joya")
                .font(.largeTitle.bold())

            Text("Firmware Test")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
    }

    private var statusBlock: some View {
        VStack(spacing: 12) {
            Circle()
                .fill(statusColor)
                .frame(width: 14, height: 14)

            Text(joya.connectionState.title)
                .font(.title2.bold())
                .multilineTextAlignment(.center)

            Text(joya.connectionState.detail)
                .font(.body)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 320)
        }
    }

    private var activityBlock: some View {
        VStack(spacing: 8) {
            Text(joya.activityState.title)
                .font(.headline)
                .foregroundStyle(activityColor)

            Text("Ultimo mensaje: \(joya.lastMessage)")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .lineLimit(3)
                .frame(maxWidth: 320)

            Text("Ultimo evento: \(joya.logs.first?.message ?? "Sin eventos tecnicos todavia")")
                .font(.caption)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .lineLimit(4)
                .frame(maxWidth: 320)
        }
        .padding(.top, 8)
    }

    private var actionButtons: some View {
        VStack(spacing: 12) {
            if joya.canConnect {
                Button {
                    joya.connectButtonTapped()
                } label: {
                    Text("Conectar dispositivo")
                        .font(.body.weight(.semibold))
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
            }

            if joya.activityState == .routine {
                Button {
                    joya.cancelRoutine()
                } label: {
                    Text("Cancelar rutina")
                        .font(.body.weight(.semibold))
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
            }

            if joya.activityState == .emergency {
                Button {
                    joya.sendFriendComingForYou()
                } label: {
                    Text("Amigo en camino")
                        .font(.body.weight(.semibold))
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)

                Button(role: .destructive) {
                    joya.cancelEmergency()
                } label: {
                    Text("Cancelar emergencia")
                        .font(.body.weight(.semibold))
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
            }

            if joya.canRestartSearch {
                Button {
                    joya.restartSearchTapped()
                } label: {
                    Text("Reintentar busqueda")
                        .font(.body.weight(.semibold))
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
            }

            if joya.isConnected {
                Button {
                    joya.testHaptic()
                } label: {
                    Text("Test haptic")
                        .font(.body.weight(.semibold))
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)

                Button(role: .destructive) {
                    joya.disconnectForTest()
                } label: {
                    Text("Desconectar Joya")
                        .font(.body.weight(.semibold))
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
            }
        }
        .controlSize(.large)
    }

    private var statusColor: Color {
        switch joya.connectionState {
        case .connected:
            return .green
        case .scanning, .connecting, .discovering:
            return .orange
        case .failed, .bluetoothOff:
            return .red
        default:
            return .gray
        }
    }

    private var activityColor: Color {
        switch joya.activityState {
        case .none:
            return .secondary
        case .routine:
            return .blue
        case .emergency:
            return .red
        }
    }
}

private struct LogSheet: View {
    let logs: [LogEntry]
    let clearLogs: () -> Void

    private static let formatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm:ss"
        return formatter
    }()

    var body: some View {
        NavigationStack {
            List {
                if logs.isEmpty {
                    Text("Sin eventos todavia")
                        .foregroundStyle(.secondary)
                } else {
                    ForEach(logs) { entry in
                        VStack(alignment: .leading, spacing: 4) {
                            Text(Self.formatter.string(from: entry.date))
                                .font(.caption)
                                .foregroundStyle(.secondary)

                            Text(entry.message)
                                .font(.body)
                        }
                        .padding(.vertical, 4)
                    }
                }
            }
            .navigationTitle("Log")
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("Copiar") {
                        UIPasteboard.general.string = logs
                            .map { "\(Self.formatter.string(from: $0.date)) \($0.message)" }
                            .joined(separator: "\n")
                    }
                    .disabled(logs.isEmpty)
                }

                ToolbarItem(placement: .topBarTrailing) {
                    Button("Limpiar", action: clearLogs)
                }
            }
        }
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
