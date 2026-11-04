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
                if Self.screenshot { trySection }
                Section("키보드 켜기") {
                    Button("설정 열기") { UIApplication.shared.open(URL(string: UIApplication.openSettingsURLString)!) }
                        .accessibilityIdentifier("openSettings")
                    DisclosureGroup("설정 방법", isExpanded: $showingSteps) {
                        step("1. 위의 「설정 열기」를 누르고 「키보드」로 들어갑니다.", image: 1)
                        step("2. 한영일과 전체 접근 허용을 켭니다.", image: 2)
                        step("3. 확인 창에서 허용을 누릅니다. 전체 접근 허용은 이 앱의 언어 설정을 키보드가 읽는 데 필요하고, 입력한 내용은 어디로도 보내지 않습니다.")
                        Text("4. 아무 입력 칸에서 🌐 키를 길게 눌러 한영일을 고릅니다.")
                    }
                }
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
                    Toggle("Return은 입력 중인 단어만 확정", isOn: $preferences.returnConfirmsOnly)
                    Toggle("일본어 뒤 숫자를 전각으로 (１２３)", isOn: $preferences.fullWidthDigits)
                    Toggle("일본어 뒤 스페이스를 전각으로", isOn: $preferences.fullWidthSpace)
                    Toggle("일본어 라이브 변환", isOn: $preferences.liveConversion)
                } header: {
                    Text("입력 옵션")
                } footer: {
                    Text("Mac용 Seamtype과 같은 옵션입니다. 라이브 변환을 켜면 치는 동안 한자로 바뀌고, 끄면 위쪽 막대에서 한자를 고릅니다.")
                }

                Section {
                    Toggle("추천 단어", isOn: $preferences.suggestions)
                    Toggle("자동 고침", isOn: $preferences.autocorrect)
                    Toggle("가장자리 터치 보정", isOn: $preferences.touchCorrection)
                    Toggle("다음 단어 예측 (일본어)", isOn: $preferences.nextWords)
                } header: {
                    Text("추천과 교정")
                } footer: {
                    Text("키를 칠 때마다 계산합니다. 입력이 느리게 느껴지면 꺼 보세요. 끈 기능은 계산하지 않고, 추천 단어와 자동 고침을 둘 다 끄면 맞춤법 사전도 읽지 않습니다.")
                }

                Section {
                    Toggle("키 소리", isOn: $sound)
                    Toggle("햅틱", isOn: $haptics)
                } header: {
                    Text("키 피드백")
                } footer: {
                    Text("소리는 무음 모드에서 나지 않습니다. 햅틱은 전체 접근 허용이 필요합니다.")
                }

                if !Self.screenshot { trySection }
                Section {
                    NavigationLink("오픈소스 라이선스") {
                        ScrollView {
                            Text(Self.licenses).font(.caption.monospaced()).textSelection(.enabled).padding()
                        }
                        .navigationTitle("오픈소스 라이선스")
                    }
                }
            }
            .navigationTitle("한영일")
            .onChange(of: preferences) { $0.save() }
            .sheet(isPresented: $choosingSetup) { setupSheet }
            .onAppear { trying = Self.screenshot }
        }
    }

    /// The steps start open until the keyboard is added (iOS lists added keyboards in AppleKeyboards).
    @State private var showingSteps = !((UserDefaults.standard.array(forKey: "AppleKeyboards") as? [String])?
        .contains { $0.hasPrefix("com.cizmo.koenja.keyboard") } ?? false)

    /// A step with the system's screen for it, captured in the app's language (UITests/SetupShots).
    private func step(_ text: LocalizedStringKey, image: Int? = nil) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(text)
            let language = Bundle.main.preferredLocalizations.first ?? "en"
            if let image, let shot = UIImage(named: "setup_\(image)_\(language)") ?? UIImage(named: "setup_\(image)_en") {
                Image(uiImage: shot).resizable().scaledToFit().frame(maxWidth: 260)
                    .clipShape(RoundedRectangle(cornerRadius: 12))
            }
        }
        .padding(.vertical, 4)
    }

    private static let licenses = Bundle.main.url(forResource: "Licenses", withExtension: "txt")
        .flatMap { try? String(contentsOf: $0, encoding: .utf8) } ?? ""

    private static let screenshot = ProcessInfo.processInfo.arguments.contains("-focusTry")

    private var trySection: some View {
        Section("써 보기") {
            TextField("dkssudgktpdy / hello / kanjihenkan", text: $text, axis: .vertical)
                .accessibilityIdentifier("tryField")
                .focused($trying)
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
