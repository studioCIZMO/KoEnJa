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
                    Toggle("주변 키 후보", isOn: $preferences.nearbyKeys)
                    Toggle("내 터치에 맞추기", isOn: $preferences.adaptToTouch)
                    Toggle("위로 쓸어 기호 입력", isOn: $preferences.swipeSymbols)
                    Button("터치 학습 지우기", role: .destructive) { TouchCalibration.clear() }
                } header: {
                    Text("오타 도움")
                } footer: {
                    Text("주변 키 후보: 옆 키 쪽을 누른 단어에 그 키로 만든 단어를 후보로 보여 줍니다(자동으로 바꾸지는 않습니다). 내 터치에 맞추기: 고른 후보로 키마다 내가 누르는 위치를 배웁니다. 스페이스를 누른 채 좌우로 밀면 커서가 움직입니다.")
                }

                shapeSection
                shortcutSection

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

    // MARK: Keyboard shape

    @State private var shape = KeyShape.load()

    private var shapeSection: some View {
        Section {
            ForEach(KeyShape.settings, id: \.key) { setting in
                let (lo, hi, step) = setting.range
                VStack(alignment: .leading) {
                    let value = shape[keyPath: setting.path]
                    Text("\(NSLocalizedString(Self.shapeNames[setting.key]!, comment: ""))  \(["kbHeight", "kbSideKeys"].contains(setting.key) ? String(format: "×%.2f", value) : String(format: "%.0f pt", value))")
                    Slider(value: Binding(get: { shape[keyPath: setting.path] }, set: {
                        shape[keyPath: setting.path] = $0
                        LanguagePreferences.defaults?.set($0, forKey: setting.key)
                    }), in: lo...hi, step: step)
                }
            }
            VStack(alignment: .leading) {
                Text("색  \(shape.hue.map { "\(Int($0))°" } ?? String(localized: "기본"))")
                // 0: the system keyboard's grays; then every 10° of hue
                Slider(value: Binding(get: { shape.hue.map { $0 / 10 + 1 } ?? 0 }, set: {
                    shape.hue = $0 < 0.5 ? nil : ($0.rounded() - 1) * 10
                    LanguagePreferences.defaults?.set(shape.hue ?? -1, forKey: KeyShape.hueKey)
                }), in: 0...36, step: 1)
            }
            Button("모양 기본값으로") { KeyShape.reset(); shape = KeyShape() }
        } header: {
            Text("키보드 모양")
        } footer: {
            Text("키보드를 다음에 열 때 바뀝니다.")
        }
    }

    private static let shapeNames = ["kbHeight": "키 높이", "kbBottom": "아래 여백", "kbGap": "키 간격",
                                     "kbRowGap": "줄 간격", "kbRadius": "모서리", "kbSideKeys": "Shift·삭제 키 폭"]

    // MARK: Text shortcuts

    @State private var shortcuts = Shortcuts.load().map { Shortcut(keys: $0.keys, phrase: $0.phrase) }
    @State private var newKeys = ""
    @State private var newPhrase = ""

    struct Shortcut: Hashable { var keys, phrase: String }

    private var shortcutSection: some View {
        Section {
            ForEach(shortcuts, id: \.self) { s in
                HStack { Text(s.keys).monospaced(); Spacer(); Text(s.phrase).foregroundStyle(.secondary).lineLimit(1) }
            }
            .onDelete { shortcuts.remove(atOffsets: $0); saveShortcuts() }
            TextField("줄임말 (예: brb)", text: $newKeys)
                .textInputAutocapitalization(.never).autocorrectionDisabled()
            TextField("문구 (예: Be right back)", text: $newPhrase)
            Button("추가") {
                let keys = newKeys.trimmingCharacters(in: .whitespaces).lowercased()
                shortcuts.removeAll { $0.keys == keys }
                shortcuts.append(Shortcut(keys: keys, phrase: newPhrase))
                saveShortcuts()
                (newKeys, newPhrase) = ("", "")
            }
            .disabled(!Shortcuts.isValid(newKeys.trimmingCharacters(in: .whitespaces).lowercased()) || newPhrase.trimmingCharacters(in: .whitespaces).isEmpty)
        } header: {
            Text("텍스트 단축어")
        } footer: {
            Text("줄임말을 치면 문구가 후보 맨 앞에 나옵니다. 줄임말은 영문 소문자 32자까지입니다. iOS 설정의 텍스트 대치도 그대로 쓸 수 있습니다.")
        }
    }

    private func saveShortcuts() { Shortcuts.save(shortcuts.map { ($0.keys, $0.phrase) }) }

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
