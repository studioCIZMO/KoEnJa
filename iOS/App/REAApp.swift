import SwiftUI

@main
struct REAApp: App {
    @State private var text = ""

    var body: some Scene {
        WindowGroup {
            NavigationStack {
                Form {
                    Section("키보드 켜기") {
                        Text("설정 → 일반 → 키보드 → 키보드 → 새로운 키보드 추가… → REA")
                        Button("설정 열기") { UIApplication.shared.open(URL(string: UIApplication.openSettingsURLString)!) }
                    }
                    Section("써 보기") {
                        TextField("dkssudgktpdy / hello / kyouhagoogledekensaku", text: $text, axis: .vertical)
                    }
                }
                .navigationTitle("REA")
            }
        }
    }
}
