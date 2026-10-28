import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: page

    property alias cfg_Fps: fps.value
    property alias cfg_FishCount: fishCount.value
    property alias cfg_PauseOnMaximized: pauseOnMaximized.checked
    property alias cfg_ShowDebug: showDebug.checked

    QQC2.SpinBox {
        id: fps
        Kirigami.FormData.label: "Kare hızı (FPS):"
        from: 5
        to: 60
    }

    QQC2.SpinBox {
        id: fishCount
        Kirigami.FormData.label: "Balık sayısı:"
        from: 1
        to: 40
    }

    QQC2.CheckBox {
        id: pauseOnMaximized
        Kirigami.FormData.label: "Performans:"
        text: "Ekranı kaplayan pencere varken duraklat"
    }

    QQC2.CheckBox {
        id: showDebug
        Kirigami.FormData.label: "Geliştirici:"
        text: "Tanılama kutusunu göster"
    }
}
