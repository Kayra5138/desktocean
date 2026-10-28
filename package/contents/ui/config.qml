import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: page

    property alias cfg_Fps: fps.value
    property alias cfg_PauseOnMaximized: pauseOnMaximized.checked
    property alias cfg_FollowTimeOfDay: followTimeOfDay.checked
    property alias cfg_ShowDebug: showDebug.checked

    QQC2.SpinBox {
        id: fps
        Kirigami.FormData.label: "Frame rate (FPS):"
        from: 5
        to: 60
    }

    QQC2.CheckBox {
        id: pauseOnMaximized
        Kirigami.FormData.label: "Performance:"
        text: "Pause while a maximized or fullscreen window is open"
    }

    QQC2.CheckBox {
        id: followTimeOfDay
        Kirigami.FormData.label: "Day and night:"
        text: "Follow the clock (fish rest at night)"
    }

    QQC2.CheckBox {
        id: showDebug
        Kirigami.FormData.label: "Developer:"
        text: "Show diagnostics overlay"
    }
}
