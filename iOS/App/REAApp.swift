import SwiftUI

@main
struct REAApp: App {
    var body: some Scene {
        WindowGroup { SettingsView() }
    }
}

struct SettingsView: View {
    @State private var preferences = LanguagePreferences.load()
    @State private var choosingSetup = !LanguagePreferences.isConfigured
    @State private var text = ""
    /// `-focusTry` (debug launches): opens the keyboard in the try field, for screenshots.
    @FocusState private var trying: Bool
    @AppStorage(KeyFeedback.soundKey, store: LanguagePreferences.defaults) private var sound = true
    @AppStorage(KeyFeedback.hapticsKey, store: LanguagePreferences.defaults) private var haptics = true

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    ForEach(Array(preferences.order.enumerated()), id: \.element) { rank, language in
                        HStack {
                            Text("\(rank + 1)").foregroundStyle(.secondary).monospacedDigit()
                            Text(language.name)
                        }
                        .deleteDisabled(language == .english)
                    }
                    .onMove { preferences.order.move(fromOffsets: $0, toOffset: $1) }
                    .onDelete { preferences.order.remove(atOffsets: $0) }
                    ForEach(LanguagePreferences.Language.allCases.filter { !preferences.order.contains($0) }) { language in
                        Button("\(language.name) 추가", systemImage: "plus.circle") { preferences.order.append(language) }
                    }
                    Button("주 사용 환경 다시 고르기") { choosingSetup = true }
                } header: {
                    Text("주 사용 언어")
                } footer: {
                    Text("위에 있는 언어일수록 애매한 단어(go → 해)가 그 언어로 입력됩니다. 영어는 항상 켜져 있습니다.")
                }
                .environment(\.editMode, .constant(.active))

                Section {
                    Toggle("키 소리", isOn: $sound)
                    Toggle("햅틱", isOn: $haptics)
                } header: {
                    Text("키 피드백")
                } footer: {
                    Text("소리는 무음 모드에서 나지 않습니다. 햅틱은 전체 접근 허용이 필요합니다.")
                }

                Section("키보드 켜기") {
                    Text("설정 → 일반 → 키보드 → 키보드 → 새로운 키보드 추가… → 한영일")
                    Text("한영일 → 전체 접근 허용: 위 언어 설정을 키보드가 읽으려면 필요합니다. 입력한 내용은 어디로도 보내지 않습니다.")
                    Button("설정 열기") { UIApplication.shared.open(URL(string: UIApplication.openSettingsURLString)!) }
                }
                Section("써 보기") {
                    TextField("dkssudgktpdy / hello / kanjihenkan", text: $text, axis: .vertical)
                        .accessibilityIdentifier("tryField")
                        .focused($trying)
                }
            }
            .navigationTitle("한영일")
            .onChange(of: preferences) { $0.save() }
            .sheet(isPresented: $choosingSetup) { setupSheet }
            .onAppear { trying = ProcessInfo.processInfo.arguments.contains("-focusTry") }
        }
    }

    /// First run: pick the usual setup; the order can be fine-tuned afterwards.
    private var setupSheet: some View {
        NavigationStack {
            List(LanguagePreferences.presets, id: \.title) { preset in
                Button {
                    preferences.order = preset.order
                    preferences.save()
                    choosingSetup = false
                } label: {
                    HStack {
                        Text(preset.title).foregroundStyle(.primary)
                        Spacer()
                        if preset.order == preferences.order && LanguagePreferences.isConfigured { Image(systemName: "checkmark") }
                    }
                }
            }
            .navigationTitle("주로 어떻게 입력하나요?")
            .navigationBarTitleDisplayMode(.inline)
        }
        .presentationDetents([.medium])
        .interactiveDismissDisabled(!LanguagePreferences.isConfigured)
    }
}
